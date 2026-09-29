#pragma once

#include "Shared.h"
#include "ui_LiveTimecodeWidget.h"

#include <QtWidgets/QWidget>

class WIDGETS_EXPORT LiveTimecodeWidget : public QWidget, Ui::LiveTimecodeWidget
{
    Q_OBJECT

    public:
        explicit LiveTimecodeWidget(QWidget* parent = 0);

    private:
        void applyStyle(bool active);

        Q_SLOT void timecodeChanged(const QString& timecode, bool active);
};
