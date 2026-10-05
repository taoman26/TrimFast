#include "core/ExportCoordinator.h"

#include "core/Insta360Trailer.h"
#include "core/Mp4Atoms.h"
#include "core/OutputVerifier.h"
#include "core/ToolLocator.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QProcess>

ExportCoordinator::ExportCoordinator(QObject *parent)
    : QObject(parent)
    , m_runner(new FFmpegRunner(this))
{
    qRegisterMetaType<ExportCoordinator::Result>();
    connect(m_runner, &FFmpegRunner::progress, this, [this](int percent, qint64) {
        const int count = static_cast<int>(m_items.size());
        emit progress((m_index * 100 + percent) / qMax(1, count), m_index, count);
    });
    connect(m_runner, &FFmpegRunner::finished, this, &ExportCoordinator::onRunnerFinished);
}

ExportCoordinator::~ExportCoordinator()
{
    if (m_running) {
        m_runner->cancel(); // FFmpegRunner's destructor kills the process
        removeTemporaries();
    }
}

QString ExportCoordinator::temporaryPathFor(const QString &finalPath)
{
    const QFileInfo fi(finalPath);
    QString name = fi.completeBaseName() + QStringLiteral(".trimfast-partial");
    if (!fi.suffix().isEmpty())
        name += QLatin1Char('.') + fi.suffix();
    return fi.absoluteDir().filePath(name);
}

// Presentation time of the first video frame of `path` in ms (0 when unknown): the motion data has
// to be moved to start there.
qint64 ExportCoordinator::videoStartMs(const QString &path)
{
    const QString tool = ToolLocator::find(QStringLiteral("ffprobe"));
    if (tool.isEmpty())
        return 0;
    QProcess p;
    p.start(tool, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-select_streams"),
                   QStringLiteral("v:0"), QStringLiteral("-show_entries"), QStringLiteral("stream=start_time"),
                   QStringLiteral("-of"), QStringLiteral("csv=p=0"), path});
    if (!p.waitForFinished(30000) || p.exitCode() != 0)
        return 0;
    bool ok = false;
    const double seconds = QString::fromLocal8Bit(p.readAllStandardOutput()).trimmed().toDouble(&ok);
    return ok && seconds > 0.0 ? qRound64(seconds * 1000.0) : 0;
}

void ExportCoordinator::emitLater(const Result &result)
{
    QMetaObject::invokeMethod(this, [this, result] { emit finished(result); }, Qt::QueuedConnection);
}

void ExportCoordinator::start(const QList<Item> &items)
{
    Result bad;
    bad.status = Status::Failed;
    if (m_running) {
        bad.message = tr("An export is already running");
        emitLater(bad);
        return;
    }
    if (items.isEmpty()) {
        bad.message = tr("Nothing to export");
        emitLater(bad);
        return;
    }
    // Refuse early (before any work) when a final name is taken and may not be replaced,
    // or when two items would write the same file.
    QStringList seen;
    for (const Item &it : items) {
        const QString out = QFileInfo(it.job.outputPath).absoluteFilePath();
        if (seen.contains(out)) {
            bad.message = tr("Two exports would write the same file: %1").arg(out);
            emitLater(bad);
            return;
        }
        seen << out;
        if (!it.job.overwrite && QFileInfo::exists(out)) {
            bad.message = tr("The output file already exists: %1").arg(out);
            emitLater(bad);
            return;
        }
    }

    m_items = items;
    m_index = 0;
    m_cancelRequested = false;
    m_notes.clear();
    m_warnings.clear();
    m_running = true;
    startCurrent();
}

void ExportCoordinator::startCurrent()
{
    FFmpegRunner::Job job = m_items.at(m_index).job;
    job.outputPath = temporaryPathFor(job.outputPath);
    job.overwrite = true; // the temporary name is ours (a stale one from a crash is replaced)
    m_runner->start(job);
}

void ExportCoordinator::cancel()
{
    if (!m_running)
        return;
    m_cancelRequested = true;
    m_runner->cancel();
}

void ExportCoordinator::removeTemporaries()
{
    for (const Item &it : std::as_const(m_items))
        QFile::remove(temporaryPathFor(it.job.outputPath));
}

void ExportCoordinator::abort(Status status, const QString &message)
{
    removeTemporaries();
    m_running = false;
    Result r;
    r.status = status;
    r.message = message;
    emit finished(r);
}

void ExportCoordinator::onRunnerFinished(const FFmpegRunner::Result &result)
{
    if (!m_running)
        return;
    if (m_cancelRequested || result.status == FFmpegRunner::Status::Cancelled) {
        abort(Status::Cancelled, tr("Export cancelled"));
        return;
    }
    if (result.status != FFmpegRunner::Status::Success) {
        abort(Status::Failed, result.message);
        return;
    }

    const FFmpegRunner::Job &job = m_items.at(m_index).job;
    const QString temp = temporaryPathFor(job.outputPath);

    // Vendor atoms in the MP4 header (Insta360's `AMBA`) are dropped by ffmpeg: put them back. This
    // must come first, while the header is still the last thing in the file. A failure is only a
    // warning: the export itself is good.
    const QList<QByteArray> vendor = Mp4Atoms::vendorAtoms(job.inputPath);
    if (!vendor.isEmpty()) {
        QStringList names;
        for (const QByteArray &atom : vendor)
            names << QString::fromLatin1(atom.mid(4, 4));
        QString why;
        if (!Mp4Atoms::addUdtaAtoms(temp, vendor, &why)) {
            m_warnings << tr("%1: the camera's header data (%2) could not be carried over: %3")
                              .arg(QFileInfo(job.outputPath).fileName(), names.join(QStringLiteral(", ")), why);
        }
    }

    // Insta360: the trailer (calibration, motion data) is outside the MP4 data and ffmpeg drops it.
    // It is put back adapted to the exported clip: Insta360 Studio only stabilises a clip whose motion
    // data covers the clip (an untrimmed trailer switches the stabilisation off).
    if (const auto trailer = Insta360Trailer::detect(job.inputPath)) {
        const QString name = QFileInfo(job.outputPath).fileName();
        Insta360Trailer::Trim trim;
        trim.startMs = job.startMs;
        trim.durationMs = job.durationMs;
        trim.videoStartMs = videoStartMs(temp);
        trim.mp4Length = QFileInfo(temp).size(); // the trailer starts right after the MP4 part

        QString err;
        if (Insta360Trailer::appendTrimmed(job.inputPath, temp, trim, &err)) {
            m_notes << tr("%1: the Insta360 motion data was cut to the exported part, so the stabilisation "
                          "keeps working. The two preview pictures of the original recording that the camera "
                          "stores in the file were removed (they show footage you may have cut away).")
                           .arg(name);
        } else if (Insta360Trailer::append(job.inputPath, *trailer, temp, &err)) {
            // An unfamiliar layout (another camera?): keep the data rather than guess.
            m_warnings << tr("%1: the Insta360 motion data could not be adapted to the cut (%2). It was copied "
                             "unchanged, so the Insta360 software may not stabilise this clip, and the file still "
                             "holds the camera's preview pictures of the whole original recording.")
                              .arg(name, err);
        } else {
            abort(Status::Failed, tr("Could not carry over the Insta360 data: %1").arg(err));
            return;
        }
    }

    // The exported file must open on its own; differences from the input are warnings.
    const OutputVerifier::Report report = OutputVerifier::verify(job.inputPath, temp);
    if (!report.readable) {
        abort(Status::Failed, tr("The exported file %1 cannot be read back: %2")
                                  .arg(QFileInfo(job.outputPath).fileName(), report.error));
        return;
    }
    for (const QString &w : report.warnings)
        m_warnings << QStringLiteral("%1: %2").arg(QFileInfo(job.outputPath).fileName(), w);

    if (++m_index < m_items.size()) {
        startCurrent();
        return;
    }
    finishAll();
}

void ExportCoordinator::finishAll()
{
    // Every file succeeded: give them their final names.
    QStringList outputs;
    for (const Item &it : std::as_const(m_items)) {
        const QString finalPath = QFileInfo(it.job.outputPath).absoluteFilePath();
        const QString temp = temporaryPathFor(finalPath);
        if (QFileInfo::exists(finalPath)) {
            if (!it.job.overwrite) {
                // Appeared while we were working. Leave it alone.
                abort(Status::Failed, tr("The output file appeared meanwhile and was not overwritten: %1")
                                          .arg(finalPath));
                return;
            }
            QFile::remove(finalPath);
        }
        if (!QFile::rename(temp, finalPath)) {
            // Roll back the ones already renamed: all or nothing.
            for (const QString &done : std::as_const(outputs))
                QFile::remove(done);
            abort(Status::Failed, tr("Could not create %1").arg(finalPath));
            return;
        }
        outputs << finalPath;
    }

    m_running = false;
    Result r;
    r.status = Status::Success;
    r.outputs = outputs;
    r.notes = m_notes;
    r.warnings = m_warnings;
    r.message = outputs.size() == 1 ? tr("Exported %1").arg(QFileInfo(outputs.first()).fileName())
                                    : tr("Exported %1 files").arg(outputs.size());
    emit progress(100, static_cast<int>(m_items.size()) - 1, static_cast<int>(m_items.size()));
    emit finished(r);
}
