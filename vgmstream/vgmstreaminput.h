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

#pragma once

#include <fooyin/core/coresettings.h>
#include <fooyin/core/engine/audioinput.h>

extern "C" {
#include <vgmstream/libvgmstream.h>
}

namespace Fooyin::VGMStreamInput {
class VGMStreamDecoder : public Fooyin::AudioDecoder
{
public:
    explicit VGMStreamDecoder();

    [[nodiscard]] QStringList extensions() const override;
    [[nodiscard]] bool isSeekable() const override;
    [[nodiscard]] bool trackHasChanged() const override;
    [[nodiscard]] Fooyin::Track changedTrack() const override;

    std::optional<Fooyin::AudioFormat> init(const Fooyin::AudioSource& source, const Fooyin::Track& track, DecoderOptions options) override;
    void start() override;
    void stop() override;

    void seek(uint64_t pos) override;

    Fooyin::AudioBuffer readBuffer(size_t bytes) override;

private:
    int vgmstream_init();
    void vgmstream_cleanup();

    DecoderOptions m_options;
    Fooyin::FySettings m_settings;
    Fooyin::AudioFormat m_format;
    QString m_path;
    int m_subsong;
    size_t bytePos, bytesRemain;
    libvgmstream_t* m_vgm;
    libstreamfile_t* m_sf;
    Fooyin::Track m_changedTrack;
};
 
class VGMStreamReader : public AudioReader
{
public:
    explicit VGMStreamReader();
    ~VGMStreamReader();

    [[nodiscard]] QStringList extensions() const override;
    [[nodiscard]] bool canReadCover() const override;
    [[nodiscard]] bool canWriteMetaData() const override;
    [[nodiscard]] int subsongCount() const override;

    bool init(const AudioSource& source) override;
    bool readTrack(const Fooyin::AudioSource& source, Fooyin::Track& track) override;

private:
    Fooyin::FySettings m_settings;
    libvgmstream_config_t m_vcfg;
    libvgmstream_t* m_vgm;
    libstreamfile_t* m_sf;
    QString m_path;
    int m_subsongCount;
};
} // namespace Fooyin::VGMStreamInput