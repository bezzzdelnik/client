#include "LtcDevice.h"

#include <ltc.h>

#include <QAudio>
#include <QAudioDevice>
#include <QMediaDevices>
#include <QtEndian>
#include <QDebug>

namespace
{
    bool tryOpenFormat(const QAudioDevice& device, int channelCount, int sampleRate, QAudioFormat::SampleFormat sampleFormat)
    {
        QAudioFormat format;
        format.setSampleRate(sampleRate);
        format.setChannelCount(channelCount);
        format.setSampleFormat(sampleFormat);

        QAudioSource source(device, format);
        QIODevice* io = source.start();
        const bool ok = io && source.state() != QAudio::StoppedState;
        source.stop();
        return ok;
    }
}

LtcDevice::LtcDevice(QObject* parent)
    : QObject(parent)
{
    this->inactivityTimer.setInterval(250);
    this->inactivityTimer.setSingleShot(true);
    QObject::connect(&this->inactivityTimer, &QTimer::timeout, this, [this]() {
        setActive(false);
    });

    this->pollTimer.setInterval(10);
    QObject::connect(&this->pollTimer, &QTimer::timeout, this, &LtcDevice::processAudio);
}

LtcDevice::~LtcDevice()
{
    stop();
}

QList<QAudioDevice> LtcDevice::availableInputs()
{
    return QMediaDevices::audioInputs();
}

QAudioDevice LtcDevice::resolveDevice(const QString& deviceId) const
{
    const QList<QAudioDevice> inputs = QMediaDevices::audioInputs();
    for (const QAudioDevice& input : inputs)
    {
        if (QString::fromUtf8(input.id()) == deviceId || input.description() == deviceId)
            return input;
    }

    return QMediaDevices::defaultAudioInput();
}

int LtcDevice::channelCountForDevice(const QString& deviceId)
{
    QAudioDevice device;
    const QList<QAudioDevice> inputs = QMediaDevices::audioInputs();
    for (const QAudioDevice& input : inputs)
    {
        if (QString::fromUtf8(input.id()) == deviceId || input.description() == deviceId)
        {
            device = input;
            break;
        }
    }

    if (device.isNull())
        device = QMediaDevices::defaultAudioInput();

    if (device.isNull())
        return 2;

    const int preferredRate = device.preferredFormat().sampleRate() > 0
        ? device.preferredFormat().sampleRate()
        : 48000;

    // Probe highest channel count the device can actually open (up to 8).
    const int candidates[] = { 8, 6, 4, 2, 1 };
    for (int channelCount : candidates)
    {
        if (tryOpenFormat(device, channelCount, preferredRate, QAudioFormat::Int16)
            || tryOpenFormat(device, channelCount, preferredRate, QAudioFormat::Float))
        {
            return channelCount;
        }
    }

    const int reported = qMax(1, device.maximumChannelCount());
    const int preferred = qMax(1, device.preferredFormat().channelCount());
    return qMin(MaxAudioTracks, qMax(reported, preferred));
}

bool LtcDevice::isActive() const
{
    return this->active;
}

QString LtcDevice::currentTimecode() const
{
    return this->lastTimecode;
}

void LtcDevice::start(const QString& deviceId, int channel, int frameRate)
{
    stop();

    this->deviceId = deviceId;
    this->frameRate = (frameRate == 25 || frameRate == 30 || frameRate == 50) ? frameRate : 25;
    this->channelIndex = qMax(0, channel - 1);

    QAudioDevice device = resolveDevice(deviceId);
    if (device.isNull())
    {
        qWarning() << "LtcDevice: no audio input available";
        setActive(false);
        return;
    }

    const int availableChannels = channelCountForDevice(deviceId);
    if (this->channelIndex >= availableChannels)
    {
        qWarning() << "LtcDevice: track" << (this->channelIndex + 1)
                    << "is not available on" << device.description()
                    << "(device supports" << availableChannels << "channel(s))";
        setActive(false);
        emitTimecode("00:00:00:00");
        return;
    }

    const int neededChannels = this->channelIndex + 1;
    const int preferredRate = device.preferredFormat().sampleRate() > 0
        ? device.preferredFormat().sampleRate()
        : 48000;

    QList<int> channelCandidates;
    auto addCandidate = [&channelCandidates, neededChannels, availableChannels](int count) {
        count = qBound(neededChannels, count, availableChannels);
        if (count >= neededChannels && !channelCandidates.contains(count))
            channelCandidates.append(count);
    };

    addCandidate(availableChannels);
    addCandidate(neededChannels);
    addCandidate(qMax(1, device.preferredFormat().channelCount()));
    addCandidate(qMax(1, device.maximumChannelCount()));

    const QList<QAudioFormat::SampleFormat> sampleFormats = {
        QAudioFormat::Int16,
        QAudioFormat::Float
    };

    for (int channelCount : channelCandidates)
    {
        for (QAudioFormat::SampleFormat sampleFormat : sampleFormats)
        {
            QAudioFormat format;
            format.setSampleRate(preferredRate);
            format.setChannelCount(channelCount);
            format.setSampleFormat(sampleFormat);

            auto* source = new QAudioSource(device, format, this);
            QIODevice* io = source->start();
            if (io && source->state() != QAudio::StoppedState)
            {
                this->audioSource = source;
                this->audioIODevice = io;
                break;
            }

            source->stop();
            delete source;
        }

        if (this->audioIODevice)
            break;
    }

    if (!this->audioIODevice || !this->audioSource)
    {
        qWarning() << "LtcDevice: failed to start audio capture on" << device.description()
                    << "requested track" << neededChannels;
        stop();
        return;
    }

    const QAudioFormat format = this->audioSource->format();
    if (this->channelIndex >= format.channelCount())
    {
        qWarning() << "LtcDevice: opened only" << format.channelCount()
                    << "channel(s), track" << neededChannels << "unavailable";
        stop();
        emitTimecode("00:00:00:00");
        return;
    }

    qDebug() << "LtcDevice: capturing" << device.description()
             << "track" << neededChannels
             << "format channels" << format.channelCount()
             << "rate" << format.sampleRate()
             << "sampleFormat" << format.sampleFormat();

    this->sampleRate = format.sampleRate() > 0 ? format.sampleRate() : preferredRate;
    const int apv = qMax(1, this->sampleRate / this->frameRate);

    this->decoder = ltc_decoder_create(apv, 32);
    if (!this->decoder)
    {
        qWarning() << "LtcDevice: failed to create LTC decoder";
        stop();
        return;
    }

    this->samplePosition = 0;
    this->pollTimer.start();
    this->lastTimecode = "00:00:00:00";
    setActive(false);
    emitTimecode(this->lastTimecode);
}

void LtcDevice::stop()
{
    this->pollTimer.stop();
    this->inactivityTimer.stop();

    if (this->audioSource)
    {
        this->audioSource->stop();
        delete this->audioSource;
        this->audioSource = nullptr;
        this->audioIODevice = nullptr;
    }

    if (this->decoder)
    {
        ltc_decoder_free(this->decoder);
        this->decoder = nullptr;
    }

    setActive(false);
}

void LtcDevice::reset(const QString& deviceId, int channel, int frameRate)
{
    start(deviceId, channel, frameRate);
}

void LtcDevice::processAudio()
{
    if (!this->audioIODevice || !this->decoder)
        return;

    const QByteArray data = this->audioIODevice->readAll();
    if (data.isEmpty())
        return;

    const QAudioFormat format = this->audioSource->format();
    const int channelCount = qMax(1, format.channelCount());
    if (this->channelIndex < 0 || this->channelIndex >= channelCount)
        return;

    const int selected = this->channelIndex;

    if (format.sampleFormat() == QAudioFormat::Int16)
    {
        const qsizetype sampleCount = data.size() / (int)sizeof(qint16);
        QVector<short> mono(sampleCount / channelCount);
        const qint16* samples = reinterpret_cast<const qint16*>(data.constData());
        for (qsizetype i = 0, o = 0; i + channelCount - 1 < sampleCount; i += channelCount, ++o)
            mono[o] = samples[i + selected];

        ltc_decoder_write_s16(this->decoder, mono.data(), static_cast<size_t>(mono.size()), this->samplePosition);
        this->samplePosition += mono.size();
    }
    else if (format.sampleFormat() == QAudioFormat::Float)
    {
        const qsizetype sampleCount = data.size() / (int)sizeof(float);
        QVector<float> mono(sampleCount / channelCount);
        const float* samples = reinterpret_cast<const float*>(data.constData());
        for (qsizetype i = 0, o = 0; i + channelCount - 1 < sampleCount; i += channelCount, ++o)
            mono[o] = samples[i + selected];

        ltc_decoder_write_float(this->decoder, mono.data(), static_cast<size_t>(mono.size()), this->samplePosition);
        this->samplePosition += mono.size();
    }
    else
        return;

    LTCFrameExt frame;
    while (ltc_decoder_read(this->decoder, &frame))
    {
        SMPTETimecode stime;
        ltc_frame_to_time(&stime, &frame.ltc, 0);

        const QString timecode = QString("%1:%2:%3:%4")
            .arg(stime.hours, 2, 10, QChar('0'))
            .arg(stime.mins, 2, 10, QChar('0'))
            .arg(stime.secs, 2, 10, QChar('0'))
            .arg(stime.frame, 2, 10, QChar('0'));

        emitTimecode(timecode);
        setActive(true);
        this->inactivityTimer.start();
    }
}

void LtcDevice::setActive(bool active)
{
    if (this->active == active)
        return;

    this->active = active;
    emit activeChanged(this->active);
    emit timecodeChanged(this->lastTimecode, this->active);
}

void LtcDevice::emitTimecode(const QString& timecode)
{
    this->lastTimecode = timecode;
    emit timecodeChanged(this->lastTimecode, this->active);
}
