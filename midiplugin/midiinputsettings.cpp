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

#include "midiinputsettings.h"

#include "midiinputdefs.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>

using namespace Qt::StringLiterals;

namespace Fooyin::MIDIInput {
MIDIInputSettings::MIDIInputSettings(QWidget* parent)
    : QDialog{parent}
    , m_loopCount{new QSpinBox(this)}
    , m_fadeLength{new QSpinBox(this)}
    , m_soundfontLocation{new QLineEdit(this)}
{
    setWindowTitle(tr("%1 Settings").arg(u"MIDI Input"_s));
    setModal(true);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QObject::connect(buttons, &QDialogButtonBox::accepted, this, &MIDIInputSettings::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, this, &MIDIInputSettings::reject);

    auto* lengthGroup  = new QGroupBox(tr("Length"), this);
    auto* lengthLayout = new QGridLayout(lengthGroup);

    auto* loopLabel     = new QLabel(tr("Loop count") + u":"_s, this);

    m_loopCount->setRange(1, 16);
    m_loopCount->setSingleStep(1);
    m_loopCount->setSuffix(u" "_s + tr("times"));
 
    auto* fadeLabel = new QLabel(tr("Fade length") + u":"_s, this);

    m_fadeLength->setRange(0, 10000);
    m_fadeLength->setSingleStep(500);
    m_fadeLength->setSuffix(u" "_s + tr("ms"));

    int row{0};
    lengthLayout->addWidget(loopLabel, row, 0);
    lengthLayout->addWidget(m_loopCount, row++, 1);
    lengthLayout->addWidget(fadeLabel, row, 0);
    lengthLayout->addWidget(m_fadeLength, row++, 1);
    lengthLayout->setColumnStretch(2, 1);
    lengthLayout->setRowStretch(row++, 1);

    auto* generalGroup  = new QGroupBox(tr("General"), this);
    auto* generalLayout = new QGridLayout(generalGroup);

    auto* soundfontPathLabel = new QLabel(tr("Soundfont bank") + u":"_s, this);
    auto* soundfontHintLabel = new QLabel(u"🛈 "_s
                                        + tr("MIDI files require a SoundFont bank or banks to play."),
                                    this);
    soundfontHintLabel->setWordWrap(true);

    auto* browseButton = new QPushButton(tr("&Browse…"), this);
    QObject::connect(browseButton, &QPushButton::pressed, this, &MIDIInputSettings::getSoundfontPath);

    m_soundfontLocation->setMinimumWidth(200);

    row = 0;
    generalLayout->addWidget(soundfontPathLabel, row, 0);
    generalLayout->addWidget(m_soundfontLocation, row, 1);
    generalLayout->addWidget(browseButton, row++, 2);
    generalLayout->addWidget(soundfontHintLabel, row++, 0, 1, 3);
    generalLayout->setColumnStretch(1, 1);
    generalLayout->setRowStretch(row++, 1);

    auto* layout = new QGridLayout(this);
    layout->setSizeConstraint(QLayout::SetFixedSize);

    row = 0;
    layout->addWidget(lengthGroup, row++, 0, 1, 4);
    layout->addWidget(generalGroup, row++, 0, 1, 4);
    layout->addWidget(buttons, row++, 0, 1, 4, Qt::AlignBottom);
    layout->setColumnStretch(2, 1);

    m_loopCount->setValue(m_settings.value(LoopCountSetting, DefaultLoopCount).toInt());
    m_fadeLength->setValue(m_settings.value(FadeLengthSetting, DefaultFadeLength).toInt());
    m_soundfontLocation->setText(m_settings.value(SoundfontPathSetting).toString());
}
 
void MIDIInputSettings::accept()
{
    m_settings.setValue(LoopCountSetting, m_loopCount->value());
    m_settings.setValue(FadeLengthSetting, m_fadeLength->value());
    m_settings.setValue(SoundfontPathSetting, m_soundfontLocation->text());

    done(Accepted);
}
 
void MIDIInputSettings::getSoundfontPath()
{
    const QString soundfontPath = QFileDialog::getOpenFileName(this, tr("Select Soundfont bank"), QDir::homePath(), tr("Soundfont Banks (*.sf2 *.sf2pack *.sf3)"));
    if(soundfontPath.isEmpty()) {
        return;
    }

    m_soundfontLocation->setText(soundfontPath);
}
} // namespace Fooyin::MIDIInput
