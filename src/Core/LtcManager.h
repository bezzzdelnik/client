#pragma once

#include "Shared.h"

#include "LtcDevice.h"

#include <QtCore/QObject>

class CORE_EXPORT LtcManager : public QObject
{
    Q_OBJECT

    public:
        explicit LtcManager();

        static LtcManager& getInstance();

        void initialize();
        void uninitialize();
        void reinitialize();

        LtcDevice::Ptr getLtcDevice();

    private:
        LtcDevice::Ptr device;
};
