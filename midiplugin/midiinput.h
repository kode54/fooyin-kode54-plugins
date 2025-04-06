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

#pragma once

#include <fooyin/core/coresettings.h>
#include <fooyin/core/engine/audioinput.h>

#include "MIDIPlayer.h"

class BMPlayer;

namespace Fooyin::MIDIInput {
class MIDIDecoder : public Fooyin::AudioDecoder
{
public:
    MIDIDecoder();

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
    Fooyin::FySettings m_settings;
    Fooyin::AudioFormat m_format;
    midi_container* m_midiFile;
    BMPlayer* bmplayer;
    MIDIPlayer* m_midiPlayer;
    Fooyin::Track m_changedTrack;

    bool repeatOne;
    long totalFrames;
	long framesLength;
	long framesFade;
	long framesRead;
    long loopStart;
    long loopEnd;
};
 
class MIDIReader : public AudioReader
{
public:
    [[nodiscard]] QStringList extensions() const override;
    [[nodiscard]] bool canReadCover() const override;
    [[nodiscard]] bool canWriteMetaData() const override;

    bool readTrack(const Fooyin::AudioSource& source, Fooyin::Track& track) override;
};
} // namespace Fooyin::MIDIInput
