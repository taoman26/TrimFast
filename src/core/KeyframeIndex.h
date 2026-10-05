#pragma once

#include <QByteArray>
#include <QList>
#include <QtGlobal>
#include <optional>

// Sorted list of video keyframe times (ms) with neighbour queries.
// Used for Ctrl+Left/Right navigation and for snapping IN (Stream Copy can
// only start cleanly on a keyframe).
class KeyframeIndex
{
public:
    KeyframeIndex() = default;
    explicit KeyframeIndex(QList<qint64> timesMs); // sorted + deduplicated internally

    // Parses `ffprobe -show_entries packet=pts_time,flags -of csv=p=0` output:
    // lines like "0.640000,K__". Only lines whose flags contain 'K' count.
    // Lines with unknown timestamps ("N/A") are skipped.
    static KeyframeIndex parse(const QByteArray &csv);

    bool isEmpty() const { return m_times.isEmpty(); }
    qsizetype size() const { return m_times.size(); }
    const QList<qint64> &times() const { return m_times; }

    // Greatest keyframe strictly before `ms` / smallest strictly after.
    std::optional<qint64> previous(qint64 ms) const;
    std::optional<qint64> next(qint64 ms) const;
    // Greatest keyframe <= `ms` (IN snapping) / smallest >= `ms`.
    std::optional<qint64> floor(qint64 ms) const;
    std::optional<qint64> ceil(qint64 ms) const;

private:
    QList<qint64> m_times;
};
