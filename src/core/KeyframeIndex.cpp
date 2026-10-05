#include "core/KeyframeIndex.h"

#include <algorithm>
#include <cmath>

KeyframeIndex::KeyframeIndex(QList<qint64> timesMs)
    : m_times(std::move(timesMs))
{
    std::sort(m_times.begin(), m_times.end());
    m_times.erase(std::unique(m_times.begin(), m_times.end()), m_times.end());
}

KeyframeIndex KeyframeIndex::parse(const QByteArray &csv)
{
    QList<qint64> times;
    const QList<QByteArray> lines = csv.split('\n');
    for (const QByteArray &raw : lines) {
        const QByteArray line = raw.trimmed();
        const qsizetype comma = line.indexOf(',');
        if (comma <= 0)
            continue;
        if (!line.mid(comma + 1).contains('K'))
            continue;
        bool ok = false;
        const double sec = line.left(comma).toDouble(&ok);
        if (!ok || sec < 0.0)
            continue;
        times.append(std::llround(sec * 1000.0));
    }
    return KeyframeIndex(std::move(times));
}

std::optional<qint64> KeyframeIndex::previous(qint64 ms) const
{
    const auto it = std::lower_bound(m_times.begin(), m_times.end(), ms); // first >= ms
    if (it == m_times.begin())
        return std::nullopt;
    return *(it - 1);
}

std::optional<qint64> KeyframeIndex::next(qint64 ms) const
{
    const auto it = std::upper_bound(m_times.begin(), m_times.end(), ms); // first > ms
    if (it == m_times.end())
        return std::nullopt;
    return *it;
}

std::optional<qint64> KeyframeIndex::floor(qint64 ms) const
{
    const auto it = std::upper_bound(m_times.begin(), m_times.end(), ms); // first > ms
    if (it == m_times.begin())
        return std::nullopt;
    return *(it - 1);
}

std::optional<qint64> KeyframeIndex::ceil(qint64 ms) const
{
    const auto it = std::lower_bound(m_times.begin(), m_times.end(), ms); // first >= ms
    if (it == m_times.end())
        return std::nullopt;
    return *it;
}
