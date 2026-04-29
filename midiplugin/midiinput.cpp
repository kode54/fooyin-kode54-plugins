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

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(MIDI_INPUT, "fy.midiinput")
 
using namespace Qt::StringLiterals;
 
constexpr auto SampleRate = 44100;
constexpr auto FilterMode = MIDIPlayer::filter_default;
constexpr auto FilterReverbChorus = false;
constexpr auto BufferLen = 1024;

namespace {
double subsong_start_seconds(const SS_MIDIFile *midi, size_t subsong) {
    if(!midi || midi->format != 2 || subsong == 0) return 0.0;
    if(subsong >= midi->track_count) return 0.0;
    const SS_MIDITrack *prev = &midi->tracks[subsong - 1];
    if(prev->event_count == 0) return 0.0;
    return ss_midi_ticks_to_seconds(midi, prev->events[prev->event_count - 1].ticks);
}

double subsong_end_seconds(const SS_MIDIFile *midi, size_t subsong) {
    if(!midi) return 0.0;
    if(midi->format != 2)
        return midi->duration;
    if(subsong >= midi->track_count) return midi->duration;
    const SS_MIDITrack *tr = &midi->tracks[subsong];
    if(tr->event_count == 0) return subsong_start_seconds(midi, subsong);
    return ss_midi_ticks_to_seconds(midi, tr->events[tr->event_count - 1].ticks);
}

QStringList fileExtensions()
{
    static const QStringList extensions = {u"mid"_s, u"midi"_s, u"kar"_s, u"rmi"_s, u"mids"_s, u"mds"_s, u"hmi"_s, u"hmp"_s, u"hmq"_s, u"mus"_s, u"xmi"_s, u"lds"_s, u"xmf"_s, u"mxmf"_s};
    return extensions;
}

QStringList soundfontExtensions()
{
    static const QStringList extensions = {u"sf2"_s, u"sf2pack"_s, u"sf3"_s, u"sf4"_s, u"dls"_s, u"sflist"_s, u"json"_s};
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

void configurePlayer(SpessaPlayer* player, QString fileBank, bool is_gs)
{
    using namespace Fooyin::MIDIInput;

    player->setSampleRate(SampleRate);
    player->setFilterMode(FilterMode, FilterReverbChorus);

    const Fooyin::FySettings setting;

    QString soundfontPath = setting.value(SoundfontPathSetting).toString();
    QString soundfontGSPath = setting.value(SoundfontGSPathSetting).toString();
    if(!soundfontGSPath.isEmpty() && is_gs)
    {
        player->setSoundFont(soundfontGSPath.toUtf8().constData());
    }
    else if(!soundfontPath.isEmpty())
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

    SS_File *midiFile = ss_file_open_from_memory((const uint8_t *)data.constData(), data.size(), false);
    if(!midiFile) {
        return {};
    }

    m_midiFile = ss_midi_load(midiFile, track.extension().toUtf8().constData());
    ss_file_close(midiFile);
    if(!m_midiFile)
    {
        return {};
    }

    if(ss_midi_has_emidi(m_midiFile)) {
        ss_midi_remove_emidi_non_gm(m_midiFile);
    }

    if(m_midiFile->duration <= 0.0)
    {
        return {};
    }

    spessaplayer = new SpessaPlayer;
    configurePlayer(spessaplayer, findFilebank(track.filepath()), ss_midi_has_gs(m_midiFile));

    m_midiPlayer = spessaplayer;

    int loopCount = m_settings.value(LoopCountSetting, DefaultLoopCount).toInt();
    if(options & NoLooping) {
        loopCount = 1;
    }
    repeatOne = !(options & NoInfiniteLooping) && isRepeatingTrack();
    if(options & NoInfiniteLooping && isRepeatingTrack()) {
        loopCount = DefaultLoopCount;
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

    int subsong = track.subsong();

    double subsong_begin = subsong_start_seconds(m_midiFile, (size_t)subsong);
    double subsong_end = subsong_end_seconds(m_midiFile, (size_t)subsong);

    framesLength = subsong_end - subsong_begin;

    loopStart = -1;
    loopEnd = -1;

    if(m_midiFile->loop.end > 0) {
        loopStart = ss_midi_ticks_to_seconds(m_midiFile, m_midiFile->loop.start);
        loopEnd = ss_midi_ticks_to_seconds(m_midiFile, m_midiFile->loop.end);

        /* Express loop boundaries relative to subsong start. */
        loopStart -= subsong_begin;
        loopEnd -= subsong_begin;
        if(loopStart < 0.0) loopStart = 0.0;
        if(loopEnd < 0.0) loopEnd = 0.0;
    }

    if(loopStart == -1) loopStart = 0;
    if(loopEnd = -1) loopEnd = framesLength;

    bool isLooped;
    if(loopStart != 0 || loopEnd != framesLength)
    {
        framesFade = m_settings.value(FadeLengthSetting, DefaultFadeLength).toInt() / 1000.0;
        framesLength = loopStart + (loopEnd - loopStart) * loopCount;
        isLooped = true;
    } else {
        framesFade = 0;
        isLooped = false;
    }

    framesRead = 0;
    framesLength = round(framesLength * SampleRate);

    totalFrames = framesLength + framesFade;

    unsigned int loop_mode = repeatOne ? MIDIPlayer::loop_mode_enable | MIDIPlayer::loop_mode_force : 0;

    if(!m_midiPlayer->Load(m_midiFile, (unsigned)subsong, loop_mode, framesFade))
    {
        return {};
    }

    m_midiPlayer->setFilterMode(MIDIPlayer::filter_default, false);

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
        ss_midi_free(m_midiFile);
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

    const auto startTime = static_cast<uint64_t>(m_format.durationForFrames(m_midiPlayer->Tell()));

    AudioBuffer buffer{m_format, startTime};
    buffer.resize(bytes);

    const int frames = m_format.framesForBytes(static_cast<int>(bytes));
    int framesWritten{0};
    while(framesWritten < frames) {
        const int framesToWrite = std::min(frames - framesWritten, BufferLen);
        const int bufferPos     = m_format.bytesForFrames(framesWritten);
        float* framesOut = (float *)(buffer.data() + bufferPos);
        unsigned long framesRendered = m_midiPlayer->Play(framesOut, framesToWrite);
        if(!framesRendered) break;
        framesWritten += framesRendered;
    }
    framesRead += framesWritten;

    if(!framesWritten) {
        return {};
    } else if(framesWritten < frames) {
        const int bufferPos = m_format.bytesForFrames(framesWritten);
        memset(buffer.data() + bufferPos, 0, bytes - bufferPos);
    }
 
    return buffer;
}

MIDIReader::MIDIReader() {
    m_midiFile = NULL;
}

MIDIReader::~MIDIReader() {
    ss_midi_free(m_midiFile);
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

int MIDIReader::subsongCount() const
{
    return m_subsongCount;
}
 
bool MIDIReader::init(const AudioSource& source)
{
    const QByteArray data = source.device->peek(source.device->size());
    if(data.isEmpty()) {
        return false;
    }
 
    SS_File* file = ss_file_open_from_memory((const uint8_t *)data.constData(), data.size(), false);
    if(!file) {
        return false;
    }

    const QFileInfo info{source.filepath};
    m_midiFile = ss_midi_load(file, info.suffix().toUtf8().constData());
    ss_file_close(file);
    if(!m_midiFile) {
        return false;
    }

    if(m_midiFile->duration <= 0.0) {
        return false;
    }

    m_subsongCount = 1;

    if(m_midiFile->format == 2)
        m_subsongCount = m_midiFile->track_count;

    return true;
}

bool MIDIReader::readTrack(const Fooyin::AudioSource& source, Fooyin::Track& track) {
    const FySettings settings;
 
    int loopCount = settings.value(LoopCountSetting).toInt();
    if(loopCount == 0) {
        loopCount = DefaultLoopCount;
    }

    int subsong = track.subsong();
 
    double subsong_begin = subsong_start_seconds(m_midiFile, (size_t)subsong);
    double subsong_end = subsong_end_seconds(m_midiFile, (size_t)subsong);

    double framesLength = subsong_end - subsong_begin;

    double loopStart = -1;
    double loopEnd = -1;

    if(m_midiFile->loop.end > 0) {
        loopStart = ss_midi_ticks_to_seconds(m_midiFile, m_midiFile->loop.start);
        loopEnd = ss_midi_ticks_to_seconds(m_midiFile, m_midiFile->loop.end);

        /* Express loop boundaries relative to subsong start. */
        loopStart -= subsong_begin;
        loopEnd -= subsong_begin;
        if(loopStart < 0.0) loopStart = 0.0;
        if(loopEnd < 0.0) loopEnd = 0.0;
    }

    if(loopStart == -1) loopStart = 0;
    if(loopEnd == -1) loopEnd = framesLength;

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

    const SS_RMIDIInfo *ri = &m_midiFile->rmidi_info;

    track.setTitle(QString::fromLocal8Bit(QByteArrayView(ri->name, ri->name_len)));
    track.setArtists({QString::fromLocal8Bit(QByteArrayView(ri->artist, ri->artist_len))});
    track.setAlbum(QString::fromLocal8Bit(QByteArrayView(ri->album, ri->album_len)));
    track.setGenres({QString::fromLocal8Bit(QByteArrayView(ri->genre, ri->genre_len))});
    track.setComment(QString::fromLocal8Bit(QByteArrayView(ri->comment, ri->comment_len)));
    track.setDate(QString::fromLocal8Bit(QByteArrayView(ri->creation_date, ri->creation_date_len)));

    track.addExtraTag(u"copyright"_s, QString::fromLocal8Bit(QByteArrayView(ri->copyright, ri->copyright_len)));
    track.addExtraTag(u"engineer"_s, QString::fromLocal8Bit(QByteArrayView(ri->engineer, ri->engineer_len)));
    track.addExtraTag(u"software"_s, QString::fromLocal8Bit(QByteArrayView(ri->software, ri->software_len)));
    track.addExtraTag(u"subject"_s, QString::fromLocal8Bit(QByteArrayView(ri->subject, ri->subject_len)));

    return true;
}
} // namespace Fooyin::MIDIInput
 
