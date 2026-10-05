#include "core/TimeFormat.h"

#include <QStringList>

namespace TimeFormat {

QString format(qint64 ms)
{
    if (ms < 0)
        ms = 0;
    const qint64 millis = ms % 1000;
    const qint64 totalSeconds = ms / 1000;
    const qint64 seconds = totalSeconds % 60;
    const qint64 minutes = (totalSeconds / 60) % 60;
    const qint64 hours = totalSeconds / 3600;
    return QStringLiteral("%1:%2:%3.%4")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'))
        .arg(millis, 3, 10, QLatin1Char('0'));
}

std::optional<qint64> parse(const QString &text)
{
    const QString s = text.trimmed();
    if (s.isEmpty())
        return std::nullopt;

    QString secondsPart = s;
    QStringList head;
    const QStringList fields = s.split(QLatin1Char(':'));
    if (fields.size() > 3)
        return std::nullopt;
    secondsPart = fields.last();
    head = fields.mid(0, fields.size() - 1);

    // seconds[.fraction]
    const QStringList sf = secondsPart.split(QLatin1Char('.'));
    if (sf.size() > 2)
        return std::nullopt;

    auto toNumber = [](const QString &t, qint64 &out) {
        if (t.isEmpty())
            return false;
        for (const QChar c : t) {
            if (!c.isDigit())
                return false;
        }
        bool ok = false;
        out = t.toLongLong(&ok);
        return ok;
    };

    qint64 sec = 0;
    if (!toNumber(sf.at(0), sec))
        return std::nullopt;

    qint64 millis = 0;
    if (sf.size() == 2) {
        const QString frac = sf.at(1);
        if (frac.isEmpty() || frac.size() > 3)
            return std::nullopt;
        qint64 f = 0;
        if (!toNumber(frac, f))
            return std::nullopt;
        for (int i = frac.size(); i < 3; ++i)
            f *= 10;
        millis = f;
    }

    qint64 minutes = 0;
    qint64 hours = 0;
    if (head.size() >= 1 && !toNumber(head.last(), minutes))
        return std::nullopt;
    if (head.size() == 2 && !toNumber(head.first(), hours))
        return std::nullopt;

    // With a colon, the upper units are bounded; the leading field is not.
    if (head.size() >= 1 && sec >= 60)
        return std::nullopt;
    if (head.size() == 2 && minutes >= 60)
        return std::nullopt;

    return ((hours * 60 + minutes) * 60 + sec) * 1000 + millis;
}

} // namespace TimeFormat
