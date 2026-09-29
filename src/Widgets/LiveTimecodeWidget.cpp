#include "LiveTimecodeWidget.h"

#include "LtcManager.h"

LiveTimecodeWidget::LiveTimecodeWidget(QWidget* parent)
    : QWidget(parent)
{
    setupUi(this);
    applyStyle(false);

    if (LtcManager::getInstance().getLtcDevice())
    {
        QObject::connect(LtcManager::getInstance().getLtcDevice().data(),
                         SIGNAL(timecodeChanged(QString,bool)),
                         this,
                         SLOT(timecodeChanged(QString,bool)));

        const bool active = LtcManager::getInstance().getLtcDevice()->isActive();
        this->labelTimecode->setText(LtcManager::getInstance().getLtcDevice()->currentTimecode());
        applyStyle(active);
    }
}

void LiveTimecodeWidget::timecodeChanged(const QString& timecode, bool active)
{
    this->labelTimecode->setText(active ? timecode : QString("00:00:00:00"));
    applyStyle(active);
}

void LiveTimecodeWidget::applyStyle(bool active)
{
    if (active)
    {
        this->labelTimecode->setStyleSheet(
            "color: #4caf50; font-size: 22px; font-weight: bold; font-family: 'Consolas', 'Courier New', monospace;");
    }
    else
    {
        this->labelTimecode->setStyleSheet(
            "color: #ff9800; font-size: 22px; font-weight: bold; font-family: 'Consolas', 'Courier New', monospace;");
        this->labelTimecode->setText("00:00:00:00");
    }
}
