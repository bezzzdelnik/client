#pragma once

#include "Shared.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSource>
#include <QIODevice>
#include <QObject>
#include <QSharedPointer>
#include <QString>
#include <QTimer>

struct LTCDecoder;

class LTC_EXPORT LtcDevice : public QObject
{
    Q_OBJECT

    public:
        typedef QSharedPointer<LtcDevice> Ptr;

        explicit LtcDevice(QObject* parent = nullptr);
        ~LtcDevice() override;

        void start(const QString& deviceId, int channel = 1, int frameRate = 25);
        void stop();
        void reset(const QString& deviceId, int channel = 1, int frameRate = 25);

        bool isActive() const;
        QString currentTimecode() const;

        static QList<QAudioDevice> availableInputs();
        static int channelCountForDevice(const QString& deviceId);

        Q_SIGNAL void timecodeChanged(const QString& timecode, bool active);
        Q_SIGNAL void activeChanged(bool active);

    private:
        void processAudio();
        void setActive(bool active);
        void emitTimecode(const QString& timecode);
        QAudioDevice resolveDevice(const QString& deviceId) const;

        QAudioSource* audioSource = nullptr;
        QIODevice* audioIODevice = nullptr;
        LTCDecoder* decoder = nullptr;
        QTimer inactivityTimer;
        QTimer pollTimer;

        QString deviceId;
        QString lastTimecode = "00:00:00:00";
        bool active = false;
        qint64 samplePosition = 0;
        int sampleRate = 48000;
        int channelIndex = 0; // 0-based
        int frameRate = 25;
};
