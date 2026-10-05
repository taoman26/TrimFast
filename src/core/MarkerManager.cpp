#include "core/MarkerManager.h"

#include <algorithm>

MarkerManager::MarkerManager(QObject *parent)
    : QObject(parent)
{
}

void MarkerManager::assign(qint64 inMs, qint64 outMs)
{
    if (inMs == m_in && outMs == m_out)
        return;
    m_in = inMs;
    m_out = outMs;
    emit markersChanged();
}

void MarkerManager::reset(qint64 durationMs)
{
    m_duration = std::max<qint64>(0, durationMs);
    m_keyframes = KeyframeIndex();
    m_in = 0;
    m_out = m_duration;
    emit markersChanged(); // always: a different file may have identical numbers
}

void MarkerManager::setDuration(qint64 durationMs)
{
    durationMs = std::max<qint64>(0, durationMs);
    if (durationMs == m_duration)
        return;
    const bool wasFull = isFullRange();
    m_duration = durationMs;
    if (m_duration == 0) {
        assign(0, 0);
        return;
    }
    const qint64 out = wasFull ? m_duration : std::min(m_out, m_duration);
    const qint64 in = std::min(m_in, out - 1);
    assign(std::max<qint64>(0, in), out);
}

void MarkerManager::setKeyframes(const KeyframeIndex &keyframes)
{
    m_keyframes = keyframes;
    if (!hasMedia())
        return;
    if (const auto snapped = m_keyframes.floor(m_in); snapped && *snapped < m_out)
        assign(*snapped, m_out);
}

MarkerManager::Result MarkerManager::setIn(qint64 ms)
{
    Result r;
    r.requested = ms;
    if (!hasMedia())
        return r;
    ms = std::clamp<qint64>(ms, 0, m_duration);
    r.requested = ms;
    r.applied = m_in;
    if (ms >= m_out) {
        r.status = Result::InvalidRange;
        return r;
    }
    qint64 applied = ms;
    if (const auto snapped = m_keyframes.floor(ms))
        applied = *snapped;
    r.status = Result::Ok;
    r.applied = applied;
    assign(applied, m_out);
    return r;
}

MarkerManager::Result MarkerManager::setOut(qint64 ms)
{
    Result r;
    r.requested = ms;
    if (!hasMedia())
        return r;
    ms = std::clamp<qint64>(ms, 0, m_duration);
    r.requested = ms;
    r.applied = m_out;
    if (ms <= m_in) {
        r.status = Result::InvalidRange;
        return r;
    }
    r.status = Result::Ok;
    r.applied = ms;
    assign(m_in, ms);
    return r;
}

MarkerManager::Result MarkerManager::setRange(qint64 inMs, qint64 outMs)
{
    Result r;
    r.requested = inMs;
    if (!hasMedia())
        return r;
    inMs = std::clamp<qint64>(inMs, 0, m_duration);
    outMs = std::clamp<qint64>(outMs, 0, m_duration);
    r.requested = inMs;
    r.applied = m_in;
    if (inMs >= outMs) {
        r.status = Result::InvalidRange;
        return r;
    }
    r.status = Result::Ok;
    r.applied = inMs;
    assign(inMs, outMs);
    return r;
}

void MarkerManager::clearRange()
{
    if (hasMedia())
        assign(0, m_duration);
}
