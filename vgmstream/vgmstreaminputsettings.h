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

#include <core/coresettings.h>

#include <QDialog>

class QCheckBox;
class QSpinBox;
class QDoubleSpinBox;

namespace Fooyin::VGMStreamInput {
class VGMStreamInputSettings : public QDialog
{
    Q_OBJECT

public:
    explicit VGMStreamInputSettings(QWidget* parent = nullptr);

    void accept() override;

private:
    FySettings m_settings;
    QSpinBox* m_loopCount;
    QSpinBox* m_fadeLength;
};
} // namespace Fooyin::VGMStreamInput
