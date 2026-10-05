#include "core/FFmpegRunner.h"

#include "core/ToolLocator.h"

#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QProcess>
#include <algorithm>

FFmpegRunner::FFmpegRunner(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<FFmpegRunner::Result>();
}

FFmpegRunner::~FFmpegRunner()
{
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(2000);
        removePartialOutput();
    }
}

QString FFmpegRunner::formatSeconds(qint64 ms)
{
    if (ms < 0)
        ms = 0;
    return QStringLiteral("%1.%2").arg(ms / 1000).arg(ms % 1000, 3, 10, QLatin1Char('0'));
}

QStringList FFmpegRunner::buildArgs(const Job &job)
{
    // Absolute paths: a name starting with "-" can never be mistaken for an option,
    // and "name:..." can never be mistaken for a protocol.
    const QString input = QFileInfo(job.inputPath).absoluteFilePath();
    const QString output = QFileInfo(job.outputPath).absoluteFilePath();

    QStringList args = {
        QStringLiteral("-hide_banner"),
        QStringLiteral("-nostdin"),
        QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-nostats"),
        QStringLiteral("-progress"), QStringLiteral("pipe:1"),
        // Input options: seek (on a keyframe) before opening the input.
        QStringLiteral("-ss"), formatSeconds(job.startMs),
        QStringLiteral("-i"), input,
        QStringLiteral("-t"), formatSeconds(job.durationMs),
        // Everything is copied: video, audio, subtitles, data, attachments ...
        QStringLiteral("-map"), QStringLiteral("0"),
        QStringLiteral("-c"), QStringLiteral("copy"),
        // ... together with metadata and chapters, and streams ffmpeg does not know (gpmd etc.).
        QStringLiteral("-map_metadata"), QStringLiteral("0"),
        QStringLiteral("-map_chapters"), QStringLiteral("0"),
        QStringLiteral("-copy_unknown"),
        QStringLiteral("-avoid_negative_ts"), QStringLiteral("make_zero"),
    };
    if (!job.format.isEmpty())
        args << QStringLiteral("-f") << job.format;
    // -n: ffmpeg itself refuses to overwrite (second line of defence after start()).
    args << (job.overwrite ? QStringLiteral("-y") : QStringLiteral("-n"));
    args << output;
    return args;
}

std::optional<qint64> FFmpegRunner::parseOutTimeMs(const QByteArray &rawLine)
{
    const QByteArray line = rawLine.trimmed();
    for (const char *key : {"out_time_us=", "out_time_ms="}) {
        const QByteArray k(key);
        if (!line.startsWith(k))
            continue;
        bool ok = false;
        const qint64 us = line.mid(k.size()).toLongLong(&ok);
        if (!ok || us < 0)
            return std::nullopt; // "N/A" etc.
        return us / 1000;
    }
    return std::nullopt;
}

bool FFmpegRunner::isProgressEnd(const QByteArray &line)
{
    return line.trimmed() == "progress=end";
}

QString FFmpegRunner::lastLines(const QByteArray &text, int count)
{
    QStringList lines = QString::fromLocal8Bit(text).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (QString &l : lines)
        l = l.trimmed();
    lines.removeAll(QString());
    if (lines.size() > count)
        lines = lines.mid(lines.size() - count);
    return lines.join(QLatin1Char('\n'));
}

void FFmpegRunner::emitFinished(const Result &result)
{
    emit finished(result);
}

void FFmpegRunner::failLater(const QString &message, const QString &outputPath)
{
    Result r;
    r.status = Status::Failed;
    r.message = message;
    r.outputPath = outputPath;
    QMetaObject::invokeMethod(this, [this, r] { emitFinished(r); }, Qt::QueuedConnection);
}

void FFmpegRunner::removePartialOutput()
{
    if (!m_job.outputPath.isEmpty())
        QFile::remove(m_job.outputPath);
}

void FFmpegRunner::start(const Job &job)
{
    if (m_process) {
        failLater(tr("An export is already running"), job.outputPath);
        return;
    }
    if (job.durationMs <= 0) {
        failLater(tr("Nothing to export: the selected range is empty"), job.outputPath);
        return;
    }
    if (!QFileInfo::exists(job.inputPath)) {
        failLater(tr("Input file not found: %1").arg(job.inputPath), job.outputPath);
        return;
    }
    if (QFileInfo(job.inputPath).absoluteFilePath() == QFileInfo(job.outputPath).absoluteFilePath()) {
        failLater(tr("The output must not be the input file"), job.outputPath);
        return;
    }
    if (!job.overwrite && QFileInfo::exists(job.outputPath)) {
        failLater(tr("The output file already exists: %1").arg(job.outputPath), job.outputPath);
        return;
    }
    const QString tool = ToolLocator::find(QStringLiteral("ffmpeg"));
    if (tool.isEmpty()) {
        failLater(tr("ffmpeg was not found. Please install FFmpeg or set its path."), job.outputPath);
        return;
    }

    m_job = job;
    m_outBuffer.clear();
    m_errBuffer.clear();
    m_cancelled = false;
    m_sawEnd = false;
    m_lastPercent = -1;

    m_process = new QProcess(this);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &FFmpegRunner::onReadyRead);
    connect(m_process, &QProcess::readyReadStandardError, this, [this] {
        m_errBuffer += m_process->readAllStandardError();
        if (m_errBuffer.size() > 65536)
            m_errBuffer = m_errBuffer.right(65536);
    });
    connect(m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        onProcessFinished(code, static_cast<int>(status));
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart || !m_process)
            return;
        m_process->deleteLater();
        m_process = nullptr;
        Result r;
        r.status = Status::Failed;
        r.message = tr("Could not start ffmpeg");
        r.outputPath = m_job.outputPath;
        emitFinished(r);
    });

    m_process->start(tool, buildArgs(job));
}

void FFmpegRunner::cancel()
{
    if (!m_process || m_cancelled)
        return;
    m_cancelled = true;
    // The partial file is thrown away anyway, so there is no point in a graceful stop.
    m_process->kill();
}

void FFmpegRunner::onReadyRead()
{
    m_outBuffer += m_process->readAllStandardOutput();
    qsizetype nl;
    while ((nl = m_outBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_outBuffer.left(nl);
        m_outBuffer.remove(0, nl + 1);

        if (isProgressEnd(line)) {
            m_sawEnd = true;
            continue;
        }
        const auto ms = parseOutTimeMs(line);
        if (!ms)
            continue;
        int percent = static_cast<int>(std::clamp<qint64>(*ms * 100 / m_job.durationMs, 0, 99));
        if (percent != m_lastPercent) {
            m_lastPercent = percent;
            emit progress(percent, *ms);
        }
    }
}

void FFmpegRunner::onProcessFinished(int exitCode, int exitStatus)
{
    if (!m_process)
        return;
    // Anything still buffered (the last progress block).
    m_errBuffer += m_process->readAllStandardError();
    m_process->deleteLater();
    m_process = nullptr;

    Result r;
    r.outputPath = m_job.outputPath;

    if (m_cancelled) {
        removePartialOutput();
        r.status = Status::Cancelled;
        r.message = tr("Export cancelled");
        emitFinished(r);
        return;
    }

    const bool crashed = exitStatus != static_cast<int>(QProcess::NormalExit);
    const QFileInfo out(m_job.outputPath);
    if (crashed || exitCode != 0 || !out.exists() || out.size() == 0) {
        removePartialOutput();
        r.status = Status::Failed;
        const QString detail = lastLines(m_errBuffer, 6);
        r.message = detail.isEmpty() ? tr("ffmpeg failed (exit code %1)").arg(exitCode)
                                     : tr("ffmpeg failed:\n%1").arg(detail);
        emitFinished(r);
        return;
    }

    emit progress(100, m_job.durationMs);
    r.status = Status::Success;
    r.message = tr("Exported %1").arg(out.fileName());
    emitFinished(r);
}
