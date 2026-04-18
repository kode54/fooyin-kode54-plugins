/*
 * MIDI Plugin
 * Copyright © 2025, Christopher Snowhill <kode54@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "midiinput.h"

#include "midiinputdefs.h"
 
#include <QDir>
#include <QRegularExpression>

#include "SpessaPlayer.h"

#include "midi_processing/midi_processor.h"
 
#include <QLoggingCategory>
 
Q_LOGGING_CATEGORY(MIDI_INPUT, "fy.midiinput")
 
using namespace Qt::StringLiterals;
 
constexpr auto SampleRate = 44100;
constexpr auto FilterMode = MIDIPlayer::filter_default;
constexpr auto FilterReverbChorus = false;
constexpr auto BufferLen = 1024;

namespace {
QStringList fileExtensions()
{
    static const QStringList extensions = {u"mid"_s, u"midi"_s, u"kar"_s, u"rmi"_s, u"mids"_s, u"mds"_s, u"hmi"_s, u"hmp"_s, u"hmq"_s, u"mus"_s, u"xmi"_s, u"lds"_s};
    return extensions;
}

QStringList soundfontExtensions()
{
    static const QStringList extensions = {u"sf2"_s, u"sf2pack"_s, u"sf3"_s, u"sf4"_s, u"dls"_s};
    return extensions;
}

QString findFilebank(const QString& filepath)
{
    const QFileInfo info{filepath};
    const QDir dir{info.absolutePath()};
    const QFileInfo infodir{dir.absolutePath()};

    for(const auto ext : soundfontExtensions()) {
        const auto files = dir.entryInfoList({info.completeBaseName() + u"."_s + ext}, QDir::Files);
        if(files.isEmpty()) {
            continue;
        }
        return files.front().absoluteFilePath();
    }

    for(const auto ext : soundfontExtensions()) {
        const auto files = dir.entryInfoList({infodir.completeBaseName() + u"."_s + ext}, QDir::Files);
        if(files.isEmpty()) {
            continue;
        }
        return files.front().absoluteFilePath();
    }

    return {};
}

void configurePlayer(SpessaPlayer* player, QString fileBank)
{
    using namespace Fooyin::MIDIInput;

    player->setSampleRate(SampleRate);
    player->setFilterMode(FilterMode, FilterReverbChorus);

    const Fooyin::FySettings setting;

    QString soundfontPath = setting.value(SoundfontPathSetting).toString();
    if(!soundfontPath.isEmpty())
    {
        player->setSoundFont(soundfontPath.toUtf8().constData());
    }

    if(!fileBank.isEmpty())
    {
        player->setFileSoundFont(fileBank.toUtf8().constData());
    }

    int interpolation = setting.value(InterpolationSetting, DefaultInterpolation).toInt();
    int polyphony = setting.value(VoiceCountSetting, DefaultVoiceCount).toInt();

    SS_InterpolationType interp;

    switch(interpolation) {
        case 0:
            interp = SS_INTERP_NEAREST;
            break;
        default:
        case 1:
            interp = SS_INTERP_LINEAR;
            break;
        case 2:
            interp = SS_INTERP_HERMITE;
            break;
        case 3:
            interp = SS_INTERP_SINC;
            break;
    }

    player->setInterpolation(interp);
    player->setVoiceCount(polyphony);
}
} // namespace

namespace Fooyin::MIDIInput {
MIDIDecoder::MIDIDecoder()
{
    m_format.setSampleFormat(Fooyin::SampleFormat::F32);
    m_format.setSampleRate(SampleRate);
    m_format.setChannelCount(2);
    m_midiFile = NULL;
    m_midiPlayer = NULL;
    framesRead = -1;
    m_isDecoding = false;
}

QStringList MIDIDecoder::extensions() const
{
    return fileExtensions();
}

bool MIDIDecoder::isSeekable() const
{
    return true;
}

bool MIDIDecoder::trackHasChanged() const
{
    return m_changedTrack.isValid();
}
 
Fooyin::Track MIDIDecoder::changedTrack() const
{
    return m_changedTrack;
}
 
std::optional<Fooyin::AudioFormat> MIDIDecoder::init(const Fooyin::AudioSource& source, const Fooyin::Track& track, DecoderOptions options)
{
    m_options = options;

    const QByteArray data = source.device->readAll();
    if(data.isEmpty()) {
        return {};
    }

    spessaplayer = new SpessaPlayer;
    configurePlayer(spessaplayer, findFilebank(track.filepath()));;

    m_midiPlayer = spessaplayer;

    int loopCount = m_settings.value(LoopCountSetting, DefaultLoopCount).toInt();
    if(options & NoLooping) {
        loopCount = 1;
    }
    repeatOne = !(options & NoInfiniteLooping) && isRepeatingTrack();
    if(options & NoInfiniteLooping && isRepeatingTrack()) {
        loopCount = DefaultLoopCount;
    }
    
    std::vector<uint8_t> inputFile(data.begin(), data.end());
    m_midiFile = new midi_container;
    if(!midi_processor::process_file(inputFile, track.extension().toUtf8().constData(), *m_midiFile))
    {
        return {};
    }

    if(!m_midiFile->get_timestamp_end(0))
    {
        return {};
    }

    {
        const uint8_t *embedded_bank = NULL;
        size_t bank_size = 0;
        uint16_t bank_offset = 0;
        if (m_midiFile->get_embedded_bank(&embedded_bank, &bank_size, &bank_offset)) {
            if (embedded_bank && bank_size) {
                spessaplayer->setEmbeddedBank(embedded_bank, bank_size, bank_offset);
            }
        }
    }

    if(track.isInArchive() && source.archiveReader) {
        const QFileInfo fileInfo{track.pathInArchive()};

        bool found = false;
        for(const auto ext : soundfontExtensions()) {
            const QString soundfontPath = fileInfo.dir().relativeFilePath(fileInfo.completeBaseName() + u"."_s + ext);
            auto soundfontEntry = source.archiveReader->entry(soundfontPath);
            if(soundfontEntry.device) {
                const auto soundfontData = soundfontEntry.device->readAll();
                if(!soundfontData.isEmpty()) {
                    spessaplayer->setFileSoundFontData((const uint8_t *)soundfontData.constData(), soundfontData.size());
                    found = true;
                    break;
                }
            }
        }

        if(!found) {
            const QFileInfo infoarchive{track.archivePath()};
            for(const auto ext : soundfontExtensions()) {
                const QString soundfontPath = fileInfo.dir().relativeFilePath(infoarchive.completeBaseName() + u"."_s + ext);
                auto soundfontEntry = source.archiveReader->entry(soundfontPath);
                if(soundfontEntry.device) {
                    const auto soundfontData = soundfontEntry.device->readAll();
                    if(!soundfontData.isEmpty()) {
                        spessaplayer->setFileSoundFontData((const uint8_t *)soundfontData.constData(), soundfontData.size());
                        break;
                    }
                }
            }
        }
    }

    m_midiFile->scan_for_loops(true, true, true, true);

    framesLength = m_midiFile->get_timestamp_end(0, true);

    loopStart = m_midiFile->get_timestamp_loop_start(0, true);
    loopEnd = m_midiFile->get_timestamp_loop_end(0, true);

    if(loopStart == (double)(~0UL)) loopStart = 0;
    if(loopEnd == (double)(~0UL)) loopEnd = framesLength;

    bool isLooped;
    if(loopStart != 0 || loopEnd != framesLength)
    {
        framesFade = m_settings.value(FadeLengthSetting, DefaultFadeLength).toInt() / 1000.0;
        framesLength = loopStart + (loopEnd - loopStart) * loopCount;
        isLooped = true;
    } else {
        framesLength += 1.0;
        framesFade = 0;
        isLooped = false;
    }

    framesRead = 0;
    framesLength = round(framesLength * SampleRate);
    framesFade = round(framesFade * SampleRate);

    totalFrames = framesLength + framesFade;

    unsigned int loop_mode = framesFade ? MIDIPlayer::loop_mode_enable | MIDIPlayer::loop_mode_force : 0;
    unsigned int clean_flags = midi_container::clean_flag_emidi;

    if(!m_midiPlayer->Load(*m_midiFile, 0, loop_mode, clean_flags))
    {
        return {};
    }

    m_midiPlayer->setLoopMode((repeatOne || isLooped) ? (MIDIPlayer::loop_mode_enable | MIDIPlayer::loop_mode_force) : 0);

    return m_format;
}
 
void MIDIDecoder::start()
{
    m_isDecoding = true;
}
 
void MIDIDecoder::stop()
{
    if(m_midiPlayer) {
        delete m_midiPlayer;
        m_midiPlayer = NULL;
    }
    if(m_midiFile) {
        delete m_midiFile;
        m_midiFile = NULL;
    }
    m_changedTrack = {};
    m_isDecoding = false;
}

void MIDIDecoder::seek(uint64_t pos)
{
    framesRead = m_format.framesForDuration(pos);
    m_midiPlayer->Seek(framesRead);
}

Fooyin::AudioBuffer MIDIDecoder::readBuffer(size_t bytes)
{
    if(!m_isDecoding) {
        return {};
    }

    if(!repeatOne && framesRead >= totalFrames)
    {
        return {};
    }

    const auto startTime = static_cast<uint64_t>(m_format.durationForFrames(m_midiPlayer->Tell()));

    AudioBuffer buffer{m_format, startTime};
    buffer.resize(bytes);

    const int frames = m_format.framesForBytes(static_cast<int>(bytes));
    int framesWritten{0};
    while(framesWritten < frames) {
        const int framesToWrite = std::min(frames - framesWritten, BufferLen);
        const int bufferPos     = m_format.bytesForFrames(framesWritten);
        float* framesOut = (float *)(buffer.data() + bufferPos);
        m_midiPlayer->Play(framesOut, framesToWrite);
        framesWritten += framesToWrite;
    }
    if(!repeatOne && (framesWritten + framesRead > framesLength))
    {
        if(framesFade)
        {
            long fadeStart = (framesLength > framesRead) ? framesLength : framesRead;
            long fadeEnd = (framesRead + framesWritten > totalFrames) ? totalFrames : (framesRead + framesWritten);
            long fadePos;

            float* buff = (float *)(buffer.data()) + fadeStart - (size_t)framesRead;

            float fadeScale = (float)(framesFade - (fadeStart - framesLength)) / framesFade;
            float fadeStep = 1.0f / (float)framesFade;
            for(fadePos = fadeStart; fadePos < fadeEnd; ++fadePos)
            {
                buff[0] *= fadeScale;
                buff[1] *= fadeScale;
                buff += 2;
                fadeScale += fadeStep;
                if(fadeScale < 0.f)
                {
                    fadeScale = 0.f;
                    fadeStep = 0.f;
                }
            }

            if(framesRead + framesWritten > totalFrames) {
                size_t newFramesWritten = totalFrames - framesRead;
                float* buff = ((float *)(buffer.data() + m_format.bytesForFrames(newFramesWritten)));
                memset(buff, 0, m_format.bytesForFrames(framesWritten - newFramesWritten));
            }
        }
    }
    framesRead += framesWritten;
 
    return buffer;
}
 
QStringList MIDIReader::extensions() const
{
    return fileExtensions();
}

bool MIDIReader::canReadCover() const
{
    return false;
}

bool MIDIReader::canWriteMetaData() const
{
    return false;
}
 
bool MIDIReader::readTrack(const AudioSource& source, Track& track)
{
    midi_container midifile;
 
    const QByteArray data = source.device->peek(source.device->size());
    if(data.isEmpty()) {
        return false;
    }
 
    std::vector<uint8_t> inputFile(data.begin(), data.end());
    if(!midi_processor::process_file(inputFile, track.extension().toUtf8().constData(), midifile)) {
        return false;
    }

    const FySettings settings;
 
    int loopCount = settings.value(LoopCountSetting).toInt();
    if(loopCount == 0) {
        loopCount = DefaultLoopCount;
    }
 
    if(!midifile.get_timestamp_end(0)) {
        return false;
    }

    midifile.scan_for_loops(true, true, true, true);

    double framesLength = midifile.get_timestamp_end(0, true);

    double loopStart = midifile.get_timestamp_loop_start(0, true);
    double loopEnd = midifile.get_timestamp_loop_end(0, true);

    if(loopStart == (double)(~0UL)) loopStart = 0;
    if(loopEnd == (double)(~0UL)) loopEnd = framesLength;

    bool isLooped;
    long framesFade;
    if(loopStart != 0 || loopEnd != framesLength)
    {
        framesFade = settings.value(FadeLengthSetting, DefaultFadeLength).toInt();
        framesLength = loopStart + (loopEnd - loopStart) * loopCount;
        isLooped = true;
    } else {
        framesLength += 1.0;
        framesFade = 0;
        isLooped = false;
    }

    framesLength = round(framesLength * 1000.0);

    long totalFrames = framesLength + framesFade;
 
    track.setDuration(static_cast<uint64_t>(totalFrames));
    track.setSampleRate(SampleRate);
    track.setBitDepth(32);
    track.setChannels(2);
    track.setEncoding(u"Synthesized"_s);

    midi_meta_data metadata;
    midifile.get_meta_data(0, metadata);

    midi_meta_data_item item;
    bool remap_display_name = !metadata.get_item("title", item);

    for(size_t i = 0; i < metadata.get_count(); ++i) {
        const midi_meta_data_item &item = metadata[i];
        if(!strcasecmp(item.m_name.c_str(), "TITLE") ||
           remap_display_name && !strcasecmp(item.m_name.c_str(), "DISPLAY_NAME")) {
            track.setTitle(QString::fromLocal8Bit(item.m_value.c_str()));
        }
        else if(!strcasecmp(item.m_name.c_str(), "ARTIST")) {
            track.setArtists({QString::fromLocal8Bit(item.m_value.c_str())});
        }
        else if(!strcasecmp(item.m_name.c_str(), "ALBUM")) {
            track.setAlbum(QString::fromLocal8Bit(item.m_value.c_str()));
        }
        else if(!strcasecmp(item.m_name.c_str(), "DATE")) {
            track.setDate(QString::fromLocal8Bit(item.m_value.c_str()));
        }
        else if(!strcasecmp(item.m_name.c_str(), "GENRE")) {
            track.setGenres({QString::fromLocal8Bit(item.m_value.c_str())});
        }
        else if(!strcasecmp(item.m_name.c_str(), "COMMENT")) {
            track.setComment({QString::fromLocal8Bit(item.m_value.c_str())});
        }
        else {
            track.addExtraTag(QString::fromLocal8Bit(item.m_name.c_str()).toUpper(), QString::fromLocal8Bit(item.m_value.c_str()));
        }
    }
 
    return true;
}
} // namespace Fooyin::MIDIInput
 
