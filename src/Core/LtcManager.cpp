#include "LtcManager.h"

#include "DatabaseManager.h"

#include <QtCore/QDebug>

Q_GLOBAL_STATIC(LtcManager, ltcManager)

namespace
{
    int readLtcChannel()
    {
        const int channel = DatabaseManager::getInstance().getConfigurationByName("LtcAudioChannel").getValue().toInt();
        return channel > 0 ? channel : 1;
    }

    int readLtcFrameRate()
    {
        const int frameRate = DatabaseManager::getInstance().getConfigurationByName("LtcFrameRate").getValue().toInt();
        if (frameRate == 25 || frameRate == 30 || frameRate == 50)
            return frameRate;
        return 25;
    }
}

LtcManager::LtcManager()
{
}

LtcManager& LtcManager::getInstance()
{
    return *ltcManager();
}

void LtcManager::initialize()
{
    const QString deviceId = DatabaseManager::getInstance().getConfigurationByName("LtcAudioDevice").getValue();

    this->device = LtcDevice::Ptr(new LtcDevice());
    this->device->start(deviceId, readLtcChannel(), readLtcFrameRate());
}

void LtcManager::uninitialize()
{
    if (this->device)
        this->device->stop();
}

void LtcManager::reinitialize()
{
    const QString deviceId = DatabaseManager::getInstance().getConfigurationByName("LtcAudioDevice").getValue();

    if (!this->device)
        this->device = LtcDevice::Ptr(new LtcDevice());

    this->device->reset(deviceId, readLtcChannel(), readLtcFrameRate());
}

LtcDevice::Ptr LtcManager::getLtcDevice()
{
    return this->device;
}
