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

#include "BMPlayer.h"

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
 
void configurePlayer(BMPlayer* player)
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
    bmplayer = new BMPlayer;
    configurePlayer(bmplayer);

    m_midiPlayer = bmplayer;

    int loopCount = m_settings.value(LoopCountSetting, DefaultLoopCount).toInt();
    if(options & NoLooping) {
        loopCount = 1;
    }
    if(options & NoInfiniteLooping && loopCount == 0) {
        loopCount = DefaultLoopCount;
    }
    repeatOne = loopCount == 0;
    
    const QByteArray data = source.device->readAll();
    if(data.isEmpty()) {
        return {};
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

    m_midiFile->scan_for_loops(true, true, true, true);

    framesLength = m_midiFile->get_timestamp_end(0, true);

    loopStart = m_midiFile->get_timestamp_loop_start(0, true);
    loopEnd = m_midiFile->get_timestamp_loop_end(0, true);

    if(loopStart == ~0UL) loopStart = 0;
    if(loopEnd == ~0UL) loopEnd = framesLength;

    bool isLooped;
    if(loopStart != 0 || loopEnd != framesLength)
    {
        framesFade = m_settings.value(FadeLengthSetting, DefaultFadeLength).toInt();
        framesLength = loopStart + (loopEnd - loopStart) * loopCount;
        isLooped = true;
    } else {
        framesLength += 1000;
        framesFade = 0;
        isLooped = false;
    }

    framesLength = m_format.framesForDuration(framesLength);
    framesFade = m_format.framesForDuration(framesFade);

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
    m_midiPlayer->Reset();
    seek(0);
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
}

void MIDIDecoder::seek(uint64_t pos)
{
    framesRead = m_format.framesForDuration(pos);
    m_midiPlayer->Seek(framesRead);
}

Fooyin::AudioBuffer MIDIDecoder::readBuffer(size_t bytes)
{
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

            float* buff = (float *)(buffer.data()) + fadeStart - framesRead;

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
                framesWritten = totalFrames - framesRead;
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

    long framesLength = midifile.get_timestamp_end(0, true);

    long loopStart = midifile.get_timestamp_loop_start(0, true);
    long loopEnd = midifile.get_timestamp_loop_end(0, true);

    if(loopStart == ~0UL) loopStart = 0;
    if(loopEnd == ~0UL) loopEnd = framesLength;

    bool isLooped;
    long framesFade;
    if(loopStart != 0 || loopEnd != framesLength)
    {
        framesFade = settings.value(FadeLengthSetting, DefaultFadeLength).toInt();
        framesLength = loopStart + (loopEnd - loopStart) * loopCount;
        isLooped = true;
    } else {
        framesLength += 1000;
        framesFade = 0;
        isLooped = false;
    }

    long totalFrames = framesLength + framesFade;
 
    track.setDuration(static_cast<uint64_t>(totalFrames));
    track.setSampleRate(static_cast<int>(SampleRate));
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
 
