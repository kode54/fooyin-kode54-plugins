/*
 * MIDI Plugin
 * Copyright 2025, Christopher Snowhill <kode54@gmail.com>
 *
 * The MIDI plugin is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * The MIDI plugin is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with the MIDI plugin.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#pragma once

#include <fooyin/core/engine/inputplugin.h>
#include <fooyin/core/plugins/plugin.h>

namespace Fooyin::MIDIInput {
class MIDIInputPlugin : public QObject,
                        public Fooyin::Plugin,
                        public Fooyin::InputPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.fooyin.fooyin.plugin/1.0" FILE "midiinput.json")
    Q_INTERFACES(Fooyin::Plugin Fooyin::InputPlugin)

public:
    [[nodiscard]] QString inputName() const override;
    [[nodiscard]] Fooyin::InputCreator inputCreator() const override;

    [[nodiscard]] bool hasSettings() const override;
    void showSettings(QWidget* parent) override;
};
}