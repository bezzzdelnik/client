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
    Q_UNUSED(deviceId);
    // Always expose 1..8 tracks so multichannel devices (e.g. Blackmagic)
    // can select LTC on track 3+ even when Qt reports only stereo.
    return MaxAudioTracks;
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
    this->channelIndex = qBound(0, channel - 1, MaxAudioTracks - 1);

    QAudioDevice device = resolveDevice(deviceId);
    if (device.isNull())
    {
        qWarning() << "LtcDevice: no audio input available";
        setActive(false);
        return;
    }

    QAudioFormat format;
    format.setSampleRate(48000);
    format.setChannelCount(MaxAudioTracks);
    format.setSampleFormat(QAudioFormat::Int16);

    // Always try to open 8 tracks first (Blackmagic / multi-channel devices).
    // Fall back only if the device refuses to start.
    this->audioSource = new QAudioSource(device, format, this);
    this->audioIODevice = this->audioSource->start();

    if (!this->audioIODevice)
    {
        delete this->audioSource;
        this->audioSource = nullptr;

        format = device.preferredFormat();
        if (format.sampleRate() <= 0)
            format.setSampleRate(48000);
        if (format.channelCount() < MaxAudioTracks)
            format.setChannelCount(MaxAudioTracks);
        if (format.sampleFormat() != QAudioFormat::Int16 && format.sampleFormat() != QAudioFormat::Float)
            format.setSampleFormat(QAudioFormat::Int16);

        this->audioSource = new QAudioSource(device, format, this);
        this->audioIODevice = this->audioSource->start();
    }

    if (!this->audioIODevice)
    {
        qWarning() << "LtcDevice: failed to start audio capture on" << device.description()
                    << "requested track" << (this->channelIndex + 1);
        stop();
        return;
    }

    format = this->audioSource->format();
    if (this->channelIndex >= format.channelCount())
    {
        qWarning() << "LtcDevice: track" << (this->channelIndex + 1)
                    << "not available, device opened with" << format.channelCount() << "channel(s)";
    }

    this->sampleRate = format.sampleRate() > 0 ? format.sampleRate() : 48000;
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
