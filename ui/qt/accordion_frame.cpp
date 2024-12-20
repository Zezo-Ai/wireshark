/* accordion_frame.cpp
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config.h"

#include "accordion_frame.h"

#include "ui/util.h"
#include <ui/qt/utils/color_utils.h>

#include <QLayout>
#include <QPropertyAnimation>

const int duration_ = 150;

AccordionFrame::AccordionFrame(QWidget *parent) :
    QFrame(parent),
    frame_height_(0)
{
    updateStyleSheet();

    animation_ = new QPropertyAnimation(this, "maximumHeight", this);
    animation_->setDuration(duration_);
    animation_->setEasingCurve(QEasingCurve::InOutQuad);
    connect(animation_, &QPropertyAnimation::finished, this, &AccordionFrame::animationFinished);
}

void AccordionFrame::animatedShow()
{
    if (isVisible()) {
        show();
        return;
    }

    if (!display_is_remote()) {
        QWidget *parent = parentWidget();

        if (parent && parent->layout()) {
            // Force our parent layout to update its geometry. There are a number
            // of ways of doing this. Calling invalidate + activate seems to
            // be the best.
            show();
            parent->layout()->invalidate(); // Calls parent->layout()->update()
            parent->layout()->activate(); // Calculates sizes then calls parent->updateGeometry()
            frame_height_ = height();
            hide();
        }
        if (frame_height_ > 0) {
            animation_->setStartValue(0);
            animation_->setEndValue(frame_height_);
            animation_->start();
        }
    }
    show();
}

void AccordionFrame::animatedHide()
{
    if (!isVisible()) {
        hide();
        return;
    }

    if (!display_is_remote()) {
        animation_->setStartValue(frame_height_);
        animation_->setEndValue(0);
        animation_->start();
    } else {
        hide();
    }
}

void AccordionFrame::animationFinished()
{
    if (animation_->currentValue().toInt() < 1) {
        hide();
        setMaximumHeight(frame_height_);
    }
}

void AccordionFrame::updateStyleSheet()
{
    QString style_sheet(
        "QLineEdit#goToLineEdit {"
        "  max-width: 5em;"
        "}"
    );

#ifdef Q_OS_MAC
    style_sheet += QStringLiteral(
        "QLineEdit {"
        "  border: 1px solid palette(%1);"
        "  border-radius: 3px;"
        "  padding: 1px;"
        "}"
    ).arg(ColorUtils::themeIsDark() ? QStringLiteral("light") : QStringLiteral("dark"));
#endif

    setStyleSheet(style_sheet);
}
