#pragma once

#include <QtCore/QString>

struct TimecodeValue
{
    int hours = 0;
    int minutes = 0;
    int seconds = 0;
    int frames = 0;
    bool hasFrame = true;
    bool valid = false;

    qint64 toComparable() const
    {
        return (((((qint64)hours * 60) + minutes) * 60) + seconds) * 100 + frames;
    }
};

inline TimecodeValue parseTriggerTimecode(const QString& text)
{
    TimecodeValue value;
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return value;

    const QStringList parts = trimmed.split(QLatin1Char(':'));
    if (parts.count() != 3 && parts.count() != 4)
        return value;

    bool okH = false;
    bool okM = false;
    bool okS = false;
    bool okF = true;

    value.hours = parts[0].toInt(&okH);
    value.minutes = parts[1].toInt(&okM);
    value.seconds = parts[2].toInt(&okS);

    if (parts.count() == 4)
    {
        value.frames = parts[3].toInt(&okF);
        value.hasFrame = true;
    }
    else
    {
        value.frames = 0;
        value.hasFrame = false;
    }

    value.valid = okH && okM && okS && okF
        && value.hours >= 0 && value.hours <= 23
        && value.minutes >= 0 && value.minutes <= 59
        && value.seconds >= 0 && value.seconds <= 59
        && value.frames >= 0 && value.frames <= 99;

    return value;
}

inline TimecodeValue parseLiveTimecode(const QString& text)
{
    TimecodeValue value = parseTriggerTimecode(text);
    if (value.valid)
        value.hasFrame = true;
    return value;
}

/**
 * Returns true when live TC crossed or landed on the trigger point.
 * If a specific frame was requested but skipped, the next received frame fires.
 * If no frame was specified, target frame is 0 (or the next received frame after it).
 */
inline bool shouldTriggerOnTimecode(const TimecodeValue& previous, const TimecodeValue& current, const TimecodeValue& target)
{
    if (!current.valid || !target.valid)
        return false;

    const qint64 targetValue = target.toComparable();
    const qint64 currentValue = current.toComparable();

    if (!previous.valid)
        return currentValue == targetValue;

    const qint64 previousValue = previous.toComparable();

    // Midnight / rewind wrap: treat as a fresh pass.
    if (currentValue < previousValue)
        return currentValue >= targetValue;

    return previousValue < targetValue && currentValue >= targetValue;
}
