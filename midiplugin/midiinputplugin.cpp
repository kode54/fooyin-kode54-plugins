/*
 * MIDI Plugin
 * Copyright 2025, Christopher Snowhill <kode54@gmail.com>
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

#include "midiinputplugin.h"

#include "midiinput.h"
#include "midiinputsettings.h"

using namespace Qt::StringLiterals;

namespace Fooyin::MIDIInput {

QString MIDIInputPlugin::inputName() const
{
    return u"MIDI Input"_s;
}

Fooyin::InputCreator MIDIInputPlugin::inputCreator() const
{
    Fooyin::InputCreator creator;
    creator.decoder = []() {
        return std::make_unique<MIDIDecoder>();
    };
    creator.reader = []() {
        return std::make_unique<MIDIReader>();
    };
    return creator;
}

bool MIDIInputPlugin::hasSettings() const
{
    return true;
}

void MIDIInputPlugin::showSettings(QWidget* parent)
{
    auto* dialog = new MIDIInputSettings(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}
}

#include "moc_midiinputplugin.cpp"
