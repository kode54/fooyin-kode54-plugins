/*
 * VGMStream Plugin
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

#include "vgmstreaminput.h"

#include "vgmstreaminputdefs.h"

#include <QDir>
#include <QRegularExpression>

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(VGMSTREAM_INPUT, "fy.vgmstreaminput")

using namespace Qt::StringLiterals;

namespace {

QStringList fileExtensions()
{
    QStringList extensions;
    int size = 0;
    const char** list = libvgmstream_get_extensions(&size);
    for(int i = 0; i < size; ++i) {
        extensions.emplace_back(QString::fromLocal8Bit(list[i]));
    }
    return extensions;
}

} // namespace

namespace Fooyin::VGMStreamInput {
VGMStreamDecoder::VGMStreamDecoder()
    : m_vgm{NULL}
    , m_sf{NULL}
{ }

QStringList VGMStreamDecoder::extensions() const
{
    return fileExtensions();
}

bool VGMStreamDecoder::isSeekable() const
{
    return true;
}

bool VGMStreamDecoder::trackHasChanged() const
{
    return m_changedTrack.isValid();
}

Fooyin::Track VGMStreamDecoder::changedTrack() const
{
    return m_changedTrack;
}

int VGMStreamDecoder::vgmstream_init()
{
    int loopCount = m_settings.value(LoopCount, DefaultLoopCount).toInt();
    double fadeLength = m_settings.value(FadeLength, DefaultFadeLength).toInt() / 1000.0;

    if(m_options & NoLooping) {
        loopCount = 1;
    }
    else if(m_options & NoInfiniteLooping && isRepeatingTrack()) {
        loopCount = DefaultLoopCount;
    }

    if(loopCount < 1) {
        loopCount = 1;
    } else if(loopCount > 10) {
        loopCount = 10;
    }

    libvgmstream_config_t vcfg = { 0 };

    vcfg.allow_play_forever = 1;
    vcfg.play_forever = !(m_options & NoInfiniteLooping) && isRepeatingTrack();
    vcfg.loop_count = loopCount;
    vcfg.fade_time = fadeLength;
    vcfg.fade_delay = 0;
    vcfg.ignore_loop = 0;
    vcfg.auto_downmix_channels = 6;

    m_sf = libstreamfile_open_from_stdio(m_path.toUtf8().constData());
    if(!m_sf)
        return -1;

    m_vgm = libvgmstream_create(m_sf, m_subsong, &vcfg);
    if(!m_vgm) {
        libstreamfile_close(m_sf);
        m_sf = NULL;
        return -1;
    }

    Fooyin::SampleFormat format;
    switch(m_vgm->format->sample_format) {
        case LIBVGMSTREAM_SFMT_PCM16:
            format = Fooyin::SampleFormat::S16;
            break;

        case LIBVGMSTREAM_SFMT_PCM24:
            format = Fooyin::SampleFormat::S24;
            break;

        case LIBVGMSTREAM_SFMT_PCM32:
            format = Fooyin::SampleFormat::S32;
            break;

        case LIBVGMSTREAM_SFMT_FLOAT:
            format = Fooyin::SampleFormat::F32;
            break;

        default:
            libvgmstream_free(m_vgm);
            libstreamfile_close(m_sf);
            m_vgm = NULL;
            m_sf = NULL;
            return -1;
    }

    libstreamfile_close(m_sf);
    m_sf = NULL;

    m_format.setSampleFormat(format);
    m_format.setSampleRate(m_vgm->format->sample_rate);
    m_format.setChannelCount(m_vgm->format->channels);

    bytePos = 0;
    bytesRemain = 0;

    return 0;
}

void VGMStreamDecoder::vgmstream_cleanup()
{
    if(m_vgm) {
        libvgmstream_free(m_vgm);
        m_vgm = NULL;
    }
    if(m_sf) {
        libstreamfile_close(m_sf);
        m_sf = NULL;
    }
}

std::optional<Fooyin::AudioFormat> VGMStreamDecoder::init(const Fooyin::AudioSource& source, const Fooyin::Track& track, DecoderOptions options)
{
    vgmstream_cleanup();

    if(track.isInArchive()) {
        return {};
    }

    m_path = track.filepath();
    m_subsong = track.subsong() + 1;

    if(vgmstream_init() < 0) {
        return {};
    }

    return m_format;
}

void VGMStreamDecoder::start()
{
    vgmstream_cleanup();
    vgmstream_init();
}

void VGMStreamDecoder::stop()
{
    vgmstream_cleanup();
    m_changedTrack = {};
}

void VGMStreamDecoder::seek(uint64_t pos)
{
    uint64_t framesTarget = m_format.framesForDuration(pos);
    libvgmstream_seek(m_vgm, framesTarget);
    bytesRemain = 0;
}

Fooyin::AudioBuffer VGMStreamDecoder::readBuffer(size_t bytes)
{
    const auto startTime = static_cast<uint64_t>(m_format.durationForFrames(libvgmstream_get_play_position(m_vgm)));

    AudioBuffer buffer{m_format, startTime};
    buffer.resize(bytes);

    size_t bytesDone = 0;
    while(bytesDone < bytes) {
        if(!bytesRemain) {
            for(;;) {
                if(m_vgm->decoder->done) break;

                int err = libvgmstream_render(m_vgm);
                if(err < 0)
                    break;
        
                bytesRemain = m_vgm->decoder->buf_bytes;
                if(!bytesRemain)
                    continue;

                bytePos = 0;

                break;
            }
        }

        const size_t bytesFree = bytes - bytesDone;
        const size_t maxBytes = std::min(bytesRemain, bytesFree);
        if(maxBytes) {
            memcpy(buffer.data() + bytesDone, (uint8_t*)(m_vgm->decoder->buf) + bytePos, maxBytes);
            bytesRemain -= maxBytes;
            bytePos += maxBytes;
            bytesDone += maxBytes;
        } else if(bytesFree && bytesDone) {
            memset(buffer.data() + bytesDone, 0, bytesFree);
            break;
        } else {
            return {};
        }
    }

    return buffer;
}

VGMStreamReader::VGMStreamReader()
    : m_subsongCount{1}
    , m_sf{NULL}
    , m_vgm{NULL}
{ }

VGMStreamReader::~VGMStreamReader()
{
    if(m_vgm) {
        libvgmstream_free(m_vgm);
    }
    if(m_sf) {
        libstreamfile_close(m_sf);
    }
}

QStringList VGMStreamReader::extensions() const
{
    return fileExtensions();
}

bool VGMStreamReader::canReadCover() const
{
    return false;
}

bool VGMStreamReader::canWriteMetaData() const
{
    return false;
}

int VGMStreamReader::subsongCount() const
{
    return m_subsongCount;
}

bool VGMStreamReader::init(const AudioSource& source)
{
    m_path = source.filepath;

    const FySettings settings;

    int loopCount = m_settings.value(LoopCount, DefaultLoopCount).toInt();
    double fadeLength = m_settings.value(FadeLength, DefaultFadeLength).toInt() / 1000.0;

    if(loopCount < 1) {
        loopCount = 1;
    } else if(loopCount > 10) {
        loopCount = 10;
    }

    m_vcfg = { 0 };

    m_vcfg.allow_play_forever = 1;
    m_vcfg.play_forever = isRepeatingTrack();
    m_vcfg.loop_count = loopCount;
    m_vcfg.fade_time = fadeLength;
    m_vcfg.fade_delay = 0;
    m_vcfg.ignore_loop = 0;
    m_vcfg.auto_downmix_channels = 6;

    m_sf = libstreamfile_open_from_stdio(m_path.toUtf8().constData());
    if(!m_sf)
        return {};

    m_vgm = libvgmstream_create(m_sf, 0, &m_vcfg);
    if(!m_vgm) {
        libstreamfile_close(m_sf);
        m_sf = NULL;
        return {};
    }

    m_subsongCount = m_vgm->format->subsong_count;
    if(m_subsongCount == 0)
        m_subsongCount = 1;
    if(m_vgm->format->subsong_index > 0)
        m_subsongCount = 1;

    return true;
}

bool VGMStreamReader::readTrack(const Fooyin::AudioSource& source, Fooyin::Track& track)
{
    if(!m_sf)
        return {};

    if(m_vgm) {
        libvgmstream_free(m_vgm);
        m_vgm = NULL;
    }

    const int subsong = track.subsong() + 1;
    m_vgm = libvgmstream_create(m_sf, subsong, &m_vcfg);
    if(!m_vgm)
        return {};

    int bitDepth;
    switch(m_vgm->format->sample_format) {
        case LIBVGMSTREAM_SFMT_PCM16:
            bitDepth = 16;
            break;

        case LIBVGMSTREAM_SFMT_PCM24:
            bitDepth = 24;
            break;

        case LIBVGMSTREAM_SFMT_PCM32:
        case LIBVGMSTREAM_SFMT_FLOAT:
            bitDepth = 32;
            break;

        default:
            return {};
    }

    track.setSampleRate(m_vgm->format->sample_rate);
    track.setChannels(m_vgm->format->channels);
    track.setBitDepth(bitDepth);
    track.setEncoding(u"Lossy"_s);

    if(track.duration() == 0) {
        uint64_t samples = m_vgm->format->play_samples;
        samples = samples * 1000 / m_vgm->format->sample_rate;
        track.setDuration(samples);
    }

    QFileInfo fileInfo(m_path);
    QString metaFilename = fileInfo.path() + "/" + "!tags.m3u";

    libstreamfile_t *sf_tags = libstreamfile_open_from_stdio(metaFilename.toUtf8().constData());
    if(sf_tags) {
        libvgmstream_tags_t* tags = NULL;

        tags = libvgmstream_tags_init(sf_tags);
        if(tags) {
            libvgmstream_tags_find(tags, fileInfo.fileName().toUtf8().constData());
            while(libvgmstream_tags_next_tag(tags)) {
                const QString tag   = QString::fromUtf8(tags->key);
                const QString value = QString::fromUtf8(tags->val);
                if(tag.startsWith("REPLAYGAIN_"_L1, Qt::CaseInsensitive)) {
                    double dvalue = value.toDouble();
                    if(!QString::compare(tag, "REPLAYGAIN_TRACK_GAIN"_L1, Qt::CaseInsensitive)) {
                        track.setRGTrackGain(dvalue);
                    } else if(!QString::compare(tag, "REPLAYGAIN_TRACK_PEAK"_L1, Qt::CaseInsensitive)) {
                        track.setRGTrackPeak(dvalue);
                    } else if(!QString::compare(tag, "REPLAYGAIN_ALBUM_GAIN"_L1, Qt::CaseInsensitive)) {
                        track.setRGAlbumGain(dvalue);
                    } else if(!QString::compare(tag, "REPLAYGAIN_ALBUM_PEAK"_L1, Qt::CaseInsensitive)) {
                        track.setRGAlbumPeak(dvalue);
                    }
                } else if(!QString::compare(tag, "ALBUM"_L1, Qt::CaseInsensitive)) {
                    track.setAlbum(value);
                } else if(!QString::compare(tag, "ARTIST"_L1, Qt::CaseInsensitive)) {
                    track.setArtists({value});
                } else if(!QString::compare(tag, "DATE"_L1, Qt::CaseInsensitive)) {
                    track.setDate(value);
                } else if(!QString::compare(tag, "TRACK"_L1, Qt::CaseInsensitive) || !QString::compare(tag, "TRACKNUMBER"_L1, Qt::CaseInsensitive)) {
                    track.setTrackNumber(value);
                } else if(!QString::compare(tag, "DISC"_L1, Qt::CaseInsensitive) || !QString::compare(tag, "DISCNUMBER"_L1, Qt::CaseInsensitive)) {
                    track.setDiscNumber(value);
                } else if(!QString::compare(tag, "TITLE"_L1, Qt::CaseInsensitive)) {
                    track.setTitle(value);
                } else {
                    track.addExtraTag(tag, value);
                }
            }

            libvgmstream_tags_free(tags);
        }

        libstreamfile_close(sf_tags);
    }
 
    return true;
}
} // namespace Fooyin::VGMStreamInput
