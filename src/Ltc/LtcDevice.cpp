#include "LtcDevice.h"

#include <ltc.h>

#include <QAudioDevice>
#include <QMediaDevices>
#include <QtEndian>
#include <QDebug>

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

    const int maxChannels = device.maximumChannelCount();
    if (maxChannels > 0)
        return qMax(1, maxChannels);

    const int preferred = device.preferredFormat().channelCount();
    return qMax(1, preferred > 0 ? preferred : 2);
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

    const int deviceChannels = channelCountForDevice(deviceId);
    if (this->channelIndex >= deviceChannels)
        this->channelIndex = 0;

    QAudioFormat format;
    format.setSampleRate(48000);
    format.setChannelCount(deviceChannels);
    format.setSampleFormat(QAudioFormat::Int16);

    if (!device.isFormatSupported(format))
    {
        format = device.preferredFormat();
        if (format.channelCount() < 1)
            format.setChannelCount(deviceChannels);
        if (format.sampleFormat() != QAudioFormat::Int16 && format.sampleFormat() != QAudioFormat::Float)
            format.setSampleFormat(QAudioFormat::Int16);
    }

    if (this->channelIndex >= format.channelCount())
        this->channelIndex = 0;

    this->sampleRate = format.sampleRate() > 0 ? format.sampleRate() : 48000;
    const int apv = qMax(1, this->sampleRate / this->frameRate);

    this->decoder = ltc_decoder_create(apv, 32);
    if (!this->decoder)
    {
        qWarning() << "LtcDevice: failed to create LTC decoder";
        return;
    }

    this->samplePosition = 0;
    this->audioSource = new QAudioSource(device, format, this);
    this->audioIODevice = this->audioSource->start();
    if (!this->audioIODevice)
    {
        qWarning() << "LtcDevice: failed to start audio capture on" << device.description();
        stop();
        return;
    }

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
    const int selected = qBound(0, this->channelIndex, channelCount - 1);

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
