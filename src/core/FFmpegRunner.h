#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <optional>

class QProcess;
class QTimer;

// Lossless cut with FFmpeg stream copy. This is the only place that builds
// ffmpeg arguments, and it has no way to ask for re-encoding: the codec is
// always "copy" and there are no filter/codec/quality parameters in Job.
class FFmpegRunner : public QObject
{
    Q_OBJECT

public:
    struct Job
    {
        QString inputPath;
        QString outputPath;
        qint64 startMs = 0;    // IN (a keyframe, see MarkerManager)
        qint64 durationMs = 0; // OUT - IN
        QString format;        // optional `-f` container (e.g. "mp4" for .insv)
        bool overwrite = false; // false: never touch an existing output file
    };

    enum class Status { Success, Failed, Cancelled };

    struct Result
    {
        Status status = Status::Failed;
        QString message;     // human readable (error text / summary)
        QString outputPath;
    };

    explicit FFmpegRunner(QObject *parent = nullptr);
    ~FFmpegRunner() override;

    // Starts the job. Validation problems are reported through finished()
    // (never synchronously). Ignored with a Failed result if already running.
    void start(const Job &job);
    // Stops a running job and deletes the partial output.
    void cancel();
    bool isRunning() const { return m_process != nullptr; }

    // --- pure helpers, unit-tested ---
    // Full argument list for `ffmpeg` (without the executable).
    static QStringList buildArgs(const Job &job);
    // 1500 -> "1.500"; locale independent.
    static QString formatSeconds(qint64 ms);
    // Parses a line of `-progress pipe:1` output: the encoded time in ms for
    // out_time_us / out_time_ms lines (both are microseconds in ffmpeg); nullopt
    // for other lines and for "N/A".
    static std::optional<qint64> parseOutTimeMs(const QByteArray &line);
    static bool isProgressEnd(const QByteArray &line); // "progress=end"

signals:
    // percent is 0..100 (99 until the process reports the end).
    void progress(int percent, qint64 outTimeMs);
    void finished(const FFmpegRunner::Result &result);

private:
    void failLater(const QString &message, const QString &outputPath);
    void emitFinished(const Result &result);
    void onReadyRead();
    void onProcessFinished(int exitCode, int exitStatus);
    void removePartialOutput();
    static QString lastLines(const QByteArray &text, int count);

    QProcess *m_process = nullptr;
    Job m_job;
    QByteArray m_outBuffer;
    QByteArray m_errBuffer;
    bool m_cancelled = false;
    bool m_sawEnd = false;
    int m_lastPercent = -1;
};

Q_DECLARE_METATYPE(FFmpegRunner::Result)
