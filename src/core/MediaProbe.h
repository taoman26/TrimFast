#pragma once

#include "core/MediaInfo.h"

#include <QObject>
#include <QString>
#include <optional>

class QProcess;

// Runs ffprobe asynchronously and parses its JSON output.
class MediaProbe : public QObject
{
    Q_OBJECT

public:
    explicit MediaProbe(QObject *parent = nullptr);
    ~MediaProbe() override;

    // Starts probing `path`. A probe already in flight is cancelled.
    // Emits finished() or failed() exactly once per call (unless cancelled).
    void probe(const QString &path);
    void cancel();

    // Pure function, unit-tested: ffprobe `-of json` output -> MediaInfo.
    // `error` (optional) receives a reason on failure.
    static std::optional<MediaInfo> parse(const QByteArray &json, const QString &path,
                                          QString *error = nullptr);

signals:
    void finished(const MediaInfo &info);
    void failed(const QString &reason);

private:
    QProcess *m_process = nullptr;
    QString m_path;
    quint64 m_generation = 0;
};
