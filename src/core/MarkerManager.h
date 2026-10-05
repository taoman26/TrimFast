#pragma once

#include "core/KeyframeIndex.h"

#include <QObject>
#include <QtGlobal>

// Holds the IN/OUT range of the loaded media and enforces its rules:
//   * 0 <= IN < OUT <= duration, always (invalid requests are rejected);
//   * IN snaps down to the previous keyframe (Stream Copy can only start
//     cleanly on a keyframe); OUT is taken as given.
// With media loaded the range defaults to the whole file (nothing trimmed).
class MarkerManager : public QObject
{
    Q_OBJECT

public:
    struct Result
    {
        enum Status {
            Ok,
            NoMedia,      // nothing loaded
            InvalidRange, // would make IN >= OUT; nothing changed
        };
        Status status = NoMedia;
        qint64 requested = 0;
        qint64 applied = 0;
        bool ok() const { return status == Ok; }
        bool snapped() const { return status == Ok && applied != requested; }
    };

    explicit MarkerManager(QObject *parent = nullptr);

    // Starts a new range [0, durationMs]. Unloads when durationMs <= 0.
    void reset(qint64 durationMs);
    // The playable length changed (e.g. the player reports its own duration).
    // A range that covered the whole file keeps covering it.
    void setDuration(qint64 durationMs);
    // Keyframes arrived (asynchronously, after the file was opened). IN is
    // re-snapped to them.
    void setKeyframes(const KeyframeIndex &keyframes);

    Result setIn(qint64 ms);
    Result setOut(qint64 ms);
    // Restores a saved range as is (no snapping), clamped to the duration.
    Result setRange(qint64 inMs, qint64 outMs);
    // Back to the whole file.
    void clearRange();

    bool hasMedia() const { return m_duration > 0; }
    qint64 duration() const { return m_duration; }
    qint64 in() const { return m_in; }
    qint64 out() const { return m_out; }
    qint64 length() const { return m_out - m_in; }
    bool isFullRange() const { return m_in == 0 && m_out == m_duration; }

signals:
    void markersChanged();

private:
    void assign(qint64 inMs, qint64 outMs);

    qint64 m_duration = 0;
    qint64 m_in = 0;
    qint64 m_out = 0;
    KeyframeIndex m_keyframes;
};
