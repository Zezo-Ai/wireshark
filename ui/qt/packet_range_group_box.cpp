/* packet_range_group_box.cpp
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "packet_range_group_box.h"
#include <ui_packet_range_group_box.h>
#include <wsutil/ws_assert.h>

PacketRangeGroupBox::PacketRangeGroupBox(QWidget *parent) :
    QGroupBox(parent),
    pr_ui_(new Ui::PacketRangeGroupBox),
    range_(NULL),
    syntax_state_(SyntaxLineEdit::Empty)
{
    pr_ui_->setupUi(this);
    setFlat(true);

    pr_ui_->displayedButton->setChecked(true);
    pr_ui_->allButton->setChecked(true);
}

PacketRangeGroupBox::~PacketRangeGroupBox()
{
    delete pr_ui_;
}

void PacketRangeGroupBox::initRange(packet_range_t *range, QString selRange) {
    if (!range) return;

    range_ = nullptr;
    // Set this before setting range_ so that on_dependedCheckBox_toggled
    // doesn't trigger an extra updateCounts().
    // (We could instead manually connect and disconnect signals and slots
    // after getting a range, instead of checking for range_ in all the
    // slots.)
    pr_ui_->dependedCheckBox->setChecked(range->include_dependents);

    range_ = range;

    if (range_->process_filtered) {
        pr_ui_->displayedButton->setChecked(true);
    } else {
        pr_ui_->capturedButton->setChecked(true);
    }

    if (selRange.length() > 0)
        packet_range_convert_selection_str(range_, selRange.toUtf8().constData());

    if (range_->user_range) {
        char* tmp_str = range_convert_range(NULL, range_->user_range);
        pr_ui_->rangeLineEdit->setText(tmp_str);
        wmem_free(NULL, tmp_str);
    }
    updateCounts();
}

bool PacketRangeGroupBox::isValid() {
    if (pr_ui_->rangeButton->isChecked() && syntax_state_ != SyntaxLineEdit::Empty) {
        return false;
    }
    return true;
}

void PacketRangeGroupBox::updateCounts() {
    SyntaxLineEdit::SyntaxState orig_ss = syntax_state_;
    bool displayed_checked = pr_ui_->displayedButton->isChecked();
    bool can_select;
    bool selected_packets;
    int ignored_cnt = 0, displayed_ignored_cnt = 0;
    int depended_cnt = 0, displayed_depended_cnt = 0;
    int label_count;

    if (!range_ || !range_->cf) return;

    if (range_->displayed_cnt != 0) {
        pr_ui_->displayedButton->setEnabled(true);
    } else {
        displayed_checked = false;
        pr_ui_->capturedButton->setChecked(true);
        pr_ui_->displayedButton->setEnabled(false);
    }

    // All / Captured
    pr_ui_->allCapturedLabel->setEnabled(!displayed_checked);
    label_count = range_->cf->count;
    if (range_->remove_ignored) {
        label_count -= range_->ignored_cnt;
    }
    pr_ui_->allCapturedLabel->setText(QStringLiteral("%1").arg(label_count));

    // All / Displayed
    pr_ui_->allDisplayedLabel->setEnabled(displayed_checked);
    if (range_->include_dependents) {
        label_count = range_->displayed_plus_dependents_cnt;
    } else {
        label_count = range_->displayed_cnt;
    }
    if (range_->remove_ignored) {
        label_count -= range_->displayed_ignored_cnt;
    }
    pr_ui_->allDisplayedLabel->setText(QStringLiteral("%1").arg(label_count));

    // Selected / Captured + Displayed
    can_select = (range_->selection_range_cnt > 0 || range_->displayed_selection_range_cnt > 0);
    if (can_select) {
        pr_ui_->selectedButton->setEnabled(true);
        pr_ui_->selectedCapturedLabel->setEnabled(!displayed_checked);
        pr_ui_->selectedDisplayedLabel->setEnabled(displayed_checked);

        if (range_->include_dependents) {
            label_count = range_->selected_plus_depends_cnt;
        } else {
            label_count = range_->selection_range_cnt;
        }
        if (range_->remove_ignored) {
            label_count -= range_->ignored_selection_range_cnt;
        }
        pr_ui_->selectedCapturedLabel->setText(QString::number(label_count));
        if (range_->include_dependents) {
            label_count = range_->displayed_selected_plus_depends_cnt;
        } else {
            label_count = range_->displayed_selection_range_cnt;
        }
        if (range_->remove_ignored) {
            label_count -= range_->displayed_ignored_selection_range_cnt;
        }
        pr_ui_->selectedDisplayedLabel->setText(QString::number(label_count));
    } else {
        if (range_->process == range_process_selected) {
            pr_ui_->allButton->setChecked(true);
        }
        pr_ui_->selectedButton->setEnabled(false);
        pr_ui_->selectedCapturedLabel->setEnabled(false);
        pr_ui_->selectedDisplayedLabel->setEnabled(false);

        pr_ui_->selectedCapturedLabel->setText("0");
        pr_ui_->selectedDisplayedLabel->setText("0");
    }

    // Marked / Captured + Displayed
    if (displayed_checked) {
        selected_packets = (range_->displayed_marked_cnt != 0);
    } else {
        selected_packets = (range_->cf->marked_count > 0);
    }
    if (selected_packets) {
        pr_ui_->markedButton->setEnabled(true);
        pr_ui_->markedCapturedLabel->setEnabled(!displayed_checked);
        pr_ui_->markedDisplayedLabel->setEnabled(displayed_checked);
    } else {
        if (range_->process == range_process_marked) {
            pr_ui_->allButton->setChecked(true);
        }
        pr_ui_->markedButton->setEnabled(false);
        pr_ui_->markedCapturedLabel->setEnabled(false);
        pr_ui_->markedDisplayedLabel->setEnabled(false);
    }
    if (range_->include_dependents) {
        label_count = range_->marked_plus_depends_cnt;
    } else {
        label_count = range_->cf->marked_count;
    }
    if (range_->remove_ignored) {
        label_count -= range_->ignored_marked_cnt;
    }
    pr_ui_->markedCapturedLabel->setText(QStringLiteral("%1").arg(label_count));
    if (range_->include_dependents) {
        label_count = range_->displayed_marked_plus_depends_cnt;
    } else {
        label_count = range_->displayed_marked_cnt;
    }
    if (range_->remove_ignored) {
        label_count -= range_->displayed_ignored_marked_cnt;
    }
    pr_ui_->markedDisplayedLabel->setText(QStringLiteral("%1").arg(label_count));

    // First to last marked / Captured + Displayed
    if (displayed_checked) {
        selected_packets = (range_->displayed_mark_range_cnt != 0);
    } else {
        selected_packets = (range_->mark_range_cnt != 0);
    }
    if (selected_packets) {
        pr_ui_->ftlMarkedButton->setEnabled(true);
        pr_ui_->ftlCapturedLabel->setEnabled(!displayed_checked);
        pr_ui_->ftlDisplayedLabel->setEnabled(displayed_checked);
    } else {
        if (range_->process == range_process_marked_range) {
            pr_ui_->allButton->setChecked(true);
        }
        pr_ui_->ftlMarkedButton->setEnabled(false);
        pr_ui_->ftlCapturedLabel->setEnabled(false);
        pr_ui_->ftlDisplayedLabel->setEnabled(false);
    }
    if (range_->include_dependents) {
        label_count = range_->mark_range_plus_depends_cnt;
    } else {
        label_count = range_->mark_range_cnt;
    }
    if (range_->remove_ignored) {
        label_count -= range_->ignored_mark_range_cnt;
    }
    pr_ui_->ftlCapturedLabel->setText(QStringLiteral("%1").arg(label_count));
    if (range_->include_dependents) {
        label_count = range_->displayed_mark_range_plus_depends_cnt;
    } else {
        label_count = range_->displayed_mark_range_cnt;
    }
    if (range_->remove_ignored) {
        label_count -= range_->displayed_ignored_mark_range_cnt;
    }
    pr_ui_->ftlDisplayedLabel->setText(QStringLiteral("%1").arg(label_count));

    // User specified / Captured + Displayed

    pr_ui_->rangeButton->setEnabled(true);
    pr_ui_->rangeCapturedLabel->setEnabled(!displayed_checked);
    pr_ui_->rangeDisplayedLabel->setEnabled(displayed_checked);

    packet_range_convert_str(range_, pr_ui_->rangeLineEdit->text().toUtf8().constData());

    switch (packet_range_check(range_)) {

    case CVT_NO_ERROR:
        if (range_->include_dependents) {
            label_count = range_->user_range_plus_depends_cnt;
        } else {
            label_count = range_->user_range_cnt;
        }
        if (range_->remove_ignored) {
            label_count -= range_->ignored_user_range_cnt;
        }
        pr_ui_->rangeCapturedLabel->setText(QStringLiteral("%1").arg(label_count));
        if (range_->include_dependents) {
            label_count = range_->displayed_user_range_plus_depends_cnt;
        } else {
            label_count = range_->displayed_user_range_cnt;
        }
        if (range_->remove_ignored) {
            label_count -= range_->displayed_ignored_user_range_cnt;
        }
        pr_ui_->rangeDisplayedLabel->setText(QStringLiteral("%1").arg(label_count));
        syntax_state_ = SyntaxLineEdit::Empty;
        break;

    case CVT_SYNTAX_ERROR:
        pr_ui_->rangeCapturedLabel->setText("<small><i>Bad range</i></small>");
        pr_ui_->rangeDisplayedLabel->setText("-");
        syntax_state_ = SyntaxLineEdit::Invalid;
        break;

    case CVT_NUMBER_TOO_BIG:
        pr_ui_->rangeCapturedLabel->setText("<small><i>Number too large</i></small>");
        pr_ui_->rangeDisplayedLabel->setText("-");
        syntax_state_ = SyntaxLineEdit::Invalid;
        break;

    default:
        ws_assert_not_reached();
        return;
    }

    // Ignored
    switch(range_->process) {
    case(range_process_all):
        ignored_cnt = range_->ignored_cnt;
        displayed_ignored_cnt = range_->displayed_ignored_cnt;
        break;
    case(range_process_selected):
        ignored_cnt = range_->ignored_selection_range_cnt;
        displayed_ignored_cnt = range_->displayed_ignored_selection_range_cnt;
        break;
    case(range_process_marked):
        ignored_cnt = range_->ignored_marked_cnt;
        displayed_ignored_cnt = range_->displayed_ignored_marked_cnt;
        break;
    case(range_process_marked_range):
        ignored_cnt = range_->ignored_mark_range_cnt;
        displayed_ignored_cnt = range_->displayed_ignored_mark_range_cnt;
        break;
    case(range_process_user_range):
        ignored_cnt = range_->ignored_user_range_cnt;
        displayed_ignored_cnt = range_->displayed_ignored_user_range_cnt;
        break;
    default:
        ws_assert_not_reached();
    }

    if (displayed_checked)
        selected_packets = (displayed_ignored_cnt != 0);
    else
        selected_packets = (ignored_cnt != 0);

    if (selected_packets) {
        pr_ui_->ignoredCheckBox->setEnabled(true);
        pr_ui_->ignoredCapturedLabel->setEnabled(!displayed_checked);
        pr_ui_->ignoredDisplayedLabel->setEnabled(displayed_checked);
    } else {
        pr_ui_->ignoredCheckBox->setEnabled(false);
        pr_ui_->ignoredCapturedLabel->setEnabled(false);
        pr_ui_->ignoredDisplayedLabel->setEnabled(false);
    }
    pr_ui_->ignoredCapturedLabel->setText(QStringLiteral("%1").arg(ignored_cnt));
    pr_ui_->ignoredDisplayedLabel->setText(QStringLiteral("%1").arg(displayed_ignored_cnt));

    // Depended upon / Displayed + Captured
    switch(range_->process) {
    case(range_process_all):
        depended_cnt = 0;
        displayed_depended_cnt = range_->displayed_plus_dependents_cnt - range_->displayed_cnt;
        break;
    case(range_process_selected):
        depended_cnt = range_->selected_plus_depends_cnt - range_->selection_range_cnt;
        displayed_depended_cnt = range_->displayed_selected_plus_depends_cnt - range_->displayed_selection_range_cnt;
        break;
    case(range_process_marked):
        depended_cnt = range_->marked_plus_depends_cnt - range_->cf->marked_count;
        displayed_depended_cnt = range_->displayed_marked_plus_depends_cnt - range_->displayed_marked_cnt;
        break;
    case(range_process_marked_range):
        depended_cnt = range_->mark_range_plus_depends_cnt - range_->mark_range_cnt;
        displayed_depended_cnt = range_->displayed_mark_range_plus_depends_cnt - range_->displayed_mark_range_cnt;
        break;
    case(range_process_user_range):
        depended_cnt = range_->user_range_plus_depends_cnt - range_->user_range_cnt;
        displayed_depended_cnt = range_->displayed_user_range_plus_depends_cnt - range_->displayed_user_range_cnt;
        break;
    default:
        depended_cnt = 0;
        displayed_depended_cnt = 0;
        break;
    }

    if (displayed_checked) {
        selected_packets = (displayed_depended_cnt != 0);
    } else {
        selected_packets = (depended_cnt != 0);
    }

    if (selected_packets) {
        pr_ui_->dependedCheckBox->setEnabled(true);
        pr_ui_->dependedCapturedLabel->setEnabled(!displayed_checked);
        pr_ui_->dependedDisplayedLabel->setEnabled(displayed_checked);
    } else {
        pr_ui_->dependedCheckBox->setEnabled(false);
        pr_ui_->dependedCapturedLabel->setEnabled(false);
        pr_ui_->dependedDisplayedLabel->setEnabled(false);
    }
    pr_ui_->dependedCapturedLabel->setText(QStringLiteral("%1").arg(depended_cnt));
    pr_ui_->dependedDisplayedLabel->setText(QStringLiteral("%1").arg(displayed_depended_cnt));

    if (orig_ss != syntax_state_) {
        pr_ui_->rangeLineEdit->setSyntaxState(syntax_state_);
        emit validityChanged(isValid());
    }
    emit rangeChanged();
}

// Slots

void PacketRangeGroupBox::on_rangeLineEdit_textChanged(const QString &)
{
    if (!pr_ui_->rangeButton->isChecked()) {
        pr_ui_->rangeButton->setChecked(true);
    } else {
        updateCounts();
    }
}

void PacketRangeGroupBox::processButtonToggled(bool checked, packet_range_e process) {
    if (checked && range_) {
        range_->process = process;
    }
    updateCounts();
}

void PacketRangeGroupBox::on_allButton_toggled(bool checked)
{
    processButtonToggled(checked, range_process_all);
}

void PacketRangeGroupBox::on_selectedButton_toggled(bool checked)
{
    processButtonToggled(checked, range_process_selected);
}

void PacketRangeGroupBox::on_markedButton_toggled(bool checked)
{
    processButtonToggled(checked, range_process_marked);
}

void PacketRangeGroupBox::on_ftlMarkedButton_toggled(bool checked)
{
    processButtonToggled(checked, range_process_marked_range);
}

void PacketRangeGroupBox::on_rangeButton_toggled(bool checked)
{
    processButtonToggled(checked, range_process_user_range);
}

void PacketRangeGroupBox::on_capturedButton_toggled(bool checked)
{
    if (checked) {
        if (range_) range_->process_filtered = false;
        updateCounts();
    }
}

void PacketRangeGroupBox::on_displayedButton_toggled(bool checked)
{
    if (checked) {
        if (range_) range_->process_filtered = true;
        updateCounts();
    }
}

void PacketRangeGroupBox::on_ignoredCheckBox_toggled(bool checked)
{
    if (range_) range_->remove_ignored = checked ? true : false;
    updateCounts();
}

void PacketRangeGroupBox::on_dependedCheckBox_toggled(bool checked)
{
    if (range_) {
        range_->include_dependents = checked ? true : false;
        updateCounts();
    }
}
