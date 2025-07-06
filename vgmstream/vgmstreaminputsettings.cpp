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

#include "vgmstreaminputsettings.h"

#include "vgmstreaminputdefs.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QSpinBox>

using namespace Qt::StringLiterals;

namespace Fooyin::VGMStreamInput {
VGMStreamInputSettings::VGMStreamInputSettings(QWidget* parent)
    : QDialog{parent}
    , m_loopCount{new QSpinBox(this)}
    , m_fadeLength{new QSpinBox(this)}
{
    setWindowTitle(tr("%1 Settings").arg(u"VGMStream Input"_s));
    setModal(true);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QObject::connect(buttons, &QDialogButtonBox::accepted, this, &VGMStreamInputSettings::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, this, &VGMStreamInputSettings::reject);

    auto* lengthGroup  = new QGroupBox(tr("Length"), this);
    auto* lengthLayout = new QGridLayout(lengthGroup);

    auto* loopCountLabel = new QLabel(tr("Loop count") + u":"_s, this);

    m_loopCount->setRange(0, 10);
    m_loopCount->setSingleStep(1);
    m_loopCount->setSuffix(u" "_s + tr("loops"));

    auto* fadeLabel = new QLabel(tr("Fade length") + u":"_s, this);

    m_fadeLength->setRange(0, 10000);
    m_fadeLength->setSingleStep(500);
    m_fadeLength->setSuffix(u" "_s + tr("ms"));

    int row{0};
    lengthLayout->addWidget(loopCountLabel, row, 0);
    lengthLayout->addWidget(m_loopCount, row++, 1);
    lengthLayout->addWidget(fadeLabel, row, 0);
    lengthLayout->addWidget(m_fadeLength, row++, 1);
    lengthLayout->setColumnStretch(2, 1);
    lengthLayout->setRowStretch(row++, 1);

    auto* layout = new QGridLayout(this);
    layout->setSizeConstraint(QLayout::SetFixedSize);

    row = 0;
    layout->addWidget(lengthGroup, row++, 0, 1, 4);
    layout->addWidget(buttons, row++, 0, 1, 4, Qt::AlignBottom);
    layout->setColumnStretch(2, 1);

    m_loopCount->setValue(m_settings.value(LoopCount, DefaultLoopCount).toInt());
    m_fadeLength->setValue(m_settings.value(FadeLength, DefaultFadeLength).toInt());
}
 
void VGMStreamInputSettings::accept()
{
    m_settings.setValue(LoopCount, m_loopCount->value());
    m_settings.setValue(FadeLength, m_fadeLength->value());

    done(Accepted);
}

} // namespace Fooyin::VGMStreamInput
