#pragma once

#include "core/FFmpegRunner.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

// Runs one or more exports (one per file; two for an Insta360 pair) as a unit:
//   * the files are cut one after the other with FFmpegRunner, into temporary names
//     next to the final ones;
//   * each finished file gets post-processing (the Insta360 trailer is carried over)
//     and is verified against its input with ffprobe;
//   * only when every file succeeded are the temporary files renamed to their final
//     names. On failure or cancel nothing is left behind and existing files are
//     untouched ("all or nothing").
class ExportCoordinator : public QObject
{
    Q_OBJECT

public:
    struct Item
    {
        FFmpegRunner::Job job; // job.outputPath is the FINAL name; the coordinator picks the temp name
    };

    enum class Status { Success, Failed, Cancelled };

    struct Result
    {
        Status status = Status::Failed;
        QString message;
        QStringList outputs;  // final paths (Success only)
        QStringList notes;    // informational (e.g. the Insta360 trailer keeps the original timeline)
        QStringList warnings; // verification differences between input and output
    };

    explicit ExportCoordinator(QObject *parent = nullptr);
    ~ExportCoordinator() override;

    // Never synchronous: problems come through finished().
    void start(const QList<Item> &items);
    void cancel();
    bool isRunning() const { return m_running; }

    // Name used while a file is being written (keeps the extension so that ffmpeg
    // can still pick the container).
    static QString temporaryPathFor(const QString &finalPath);

signals:
    // percent: 0..100 over all files; fileIndex/fileCount: which file (0-based) of how many.
    void progress(int percent, int fileIndex, int fileCount);
    void finished(const ExportCoordinator::Result &result);

private:
    void startCurrent();
    void onRunnerFinished(const FFmpegRunner::Result &result);
    void finishAll();
    void abort(Status status, const QString &message);
    void removeTemporaries();
    void emitLater(const Result &result);
    static qint64 videoStartMs(const QString &path);

    FFmpegRunner *m_runner = nullptr;
    QList<Item> m_items;
    int m_index = 0;
    bool m_running = false;
    bool m_cancelRequested = false;
    QStringList m_notes;
    QStringList m_warnings;
};

Q_DECLARE_METATYPE(ExportCoordinator::Result)
