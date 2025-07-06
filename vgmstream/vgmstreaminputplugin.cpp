/*
 * VGMStream Plugin
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

#include "vgmstreaminputplugin.h"

#include "vgmstreaminput.h"
#include "vgmstreaminputsettings.h"

using namespace Qt::StringLiterals;

namespace Fooyin::VGMStreamInput {

QString VGMStreamInputPlugin::inputName() const
{
    return u"VGMStream Input"_s;
}

Fooyin::InputCreator VGMStreamInputPlugin::inputCreator() const
{
    Fooyin::InputCreator creator;
    creator.decoder = []() {
        return std::make_unique<VGMStreamDecoder>();
    };
    creator.reader = [this]() {
        return std::make_unique<VGMStreamReader>();
    };
    return creator;
}

bool VGMStreamInputPlugin::hasSettings() const
{
    return true;
}

void VGMStreamInputPlugin::showSettings(QWidget* parent)
{
    auto* dialog = new VGMStreamInputSettings(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}
}

#include "moc_vgmstreaminputplugin.cpp"
