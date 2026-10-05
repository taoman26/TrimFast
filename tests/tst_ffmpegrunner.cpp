// Integration test: really runs ffmpeg. Needs TRIMFAST_TEST_VIDEO (an H.264/AAC clip of
// >= 5 s with a keyframe every 1 s: scripts/make_test_media.sh) plus ffmpeg and
// ffprobe on PATH; skipped otherwise.
#include "core/FFmpegRunner.h"
#include "core/OutputPath.h"
#include "core/ToolLocator.h"

#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>

namespace {

struct Packet
{
    double pts;
    qint64 size;
};

QByteArray runTool(const QString &tool, const QStringList &args)
{
    QProcess p;
    p.start(ToolLocator::find(tool), args);
    p.waitForFinished(30000);
    return p.readAllStandardOutput();
}

// Video packets (pts-ordered) of a file.
QList<Packet> videoPackets(const QString &path)
{
    const QByteArray out = runTool("ffprobe", {"-v", "error", "-select_streams", "v:0", "-show_entries",
                                               "packet=pts_time,size", "-of", "csv=p=0", path});
    QList<Packet> packets;
    for (const QByteArray &line : out.split('\n')) {
        const QList<QByteArray> f = line.trimmed().split(',');
        if (f.size() == 2)
            packets.append({f.at(0).toDouble(), f.at(1).toLongLong()});
    }
    std::sort(packets.begin(), packets.end(), [](const Packet &a, const Packet &b) { return a.pts < b.pts; });
    return packets;
}

// "codec_type:codec_name" of every stream, e.g. {"video:h264", "audio:aac"}.
QStringList streamKinds(const QString &path)
{
    const QByteArray out = runTool("ffprobe", {"-v", "error", "-show_entries", "stream=codec_type,codec_name",
                                               "-of", "csv=p=0", path});
    QStringList kinds;
    for (const QByteArray &line : out.split('\n')) {
        const QList<QByteArray> f = line.trimmed().split(',');
        if (f.size() == 2)
            kinds << QString::fromLatin1(f.at(1) + ":" + f.at(0));
    }
    kinds.sort();
    return kinds;
}

double durationOf(const QString &path)
{
    return runTool("ffprobe", {"-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", path})
        .trimmed()
        .toDouble();
}

} // namespace

class TstFfmpegRunner : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_video = qEnvironmentVariable("TRIMFAST_TEST_VIDEO");
        if (m_video.isEmpty())
            QSKIP("TRIMFAST_TEST_VIDEO not set");
        if (ToolLocator::find("ffmpeg").isEmpty() || ToolLocator::find("ffprobe").isEmpty())
            QSKIP("ffmpeg/ffprobe not found");
    }

    FFmpegRunner::Result runJob(const FFmpegRunner::Job &job, QList<int> *percents = nullptr)
    {
        FFmpegRunner runner;
        QSignalSpy done(&runner, &FFmpegRunner::finished);
        QSignalSpy prog(&runner, &FFmpegRunner::progress);
        runner.start(job);
        if (!done.wait(30000))
            return {};
        if (percents) {
            for (const auto &args : prog)
                percents->append(args.at(0).toInt());
        }
        return done.first().at(0).value<FFmpegRunner::Result>();
    }

    void exportIsLossless()
    {
        QTemporaryDir dir;
        FFmpegRunner::Job job;
        job.inputPath = m_video;
        job.outputPath = dir.filePath("out.mp4");
        job.startMs = 1000; // a keyframe
        job.durationMs = 2000;

        QList<int> percents;
        const auto r = runJob(job, &percents);
        QVERIFY2(r.status == FFmpegRunner::Status::Success, qPrintable(r.message));
        QVERIFY(QFileInfo(job.outputPath).size() > 0);

        // Same streams, same codecs: nothing was converted.
        QCOMPARE(streamKinds(job.outputPath), streamKinds(m_video));
        // The end is only frame-accurate: ffmpeg applies -t to decode-order timestamps, so a
        // few trailing frames (B-frame reorder) may be included, and A/V start offsets add a bit.
        const double length = durationOf(job.outputPath);
        QVERIFY2(length >= 1.95 && length < 2.4, qPrintable(QString::number(length)));

        // The video packets are byte-for-byte the input's: the output starts with exactly the
        // input packets of 1.0 s <= pts < 3.0 s (same sizes, same order), plus at most a few
        // trailing ones. Nothing was re-encoded (a re-encode would change every size).
        QList<qint64> expected;
        for (const Packet &p : videoPackets(m_video)) {
            if (p.pts >= 0.999 && p.pts < 2.999)
                expected << p.size;
        }
        QList<qint64> actual;
        for (const Packet &p : videoPackets(job.outputPath))
            actual << p.size;
        QVERIFY2(!expected.isEmpty(), "no input packets in range");
        QVERIFY2(actual.size() >= expected.size() && actual.size() <= expected.size() + 3,
                 qPrintable(QString("packets: %1 (expected %2)").arg(actual.size()).arg(expected.size())));
        QCOMPARE(actual.mid(0, expected.size()), expected);

        // Progress ended at 100 and never went backwards.
        QVERIFY(!percents.isEmpty());
        QCOMPARE(percents.last(), 100);
        QVERIFY(std::is_sorted(percents.begin(), percents.end()));
    }

    void unusualOutputNames()
    {
        QTemporaryDir dir;
        FFmpegRunner::Job job;
        job.inputPath = m_video;
        job.outputPath = dir.filePath("出力 ファイル (trim) 'x'.mp4");
        job.startMs = 0;
        job.durationMs = 1000;
        const auto r = runJob(job);
        QVERIFY2(r.status == FFmpegRunner::Status::Success, qPrintable(r.message));
        QVERIFY(QFileInfo::exists(job.outputPath));
    }

    void insvExtensionNeedsForcedFormat()
    {
        QTemporaryDir dir;
        FFmpegRunner::Job job;
        job.inputPath = m_video;
        job.outputPath = dir.filePath("clip.insv");
        job.startMs = 0;
        job.durationMs = 1000;

        // Without -f ffmpeg cannot pick a muxer for ".insv" ...
        auto r = runJob(job);
        QCOMPARE(r.status, FFmpegRunner::Status::Failed);
        QVERIFY(!QFileInfo::exists(job.outputPath)); // ... and leaves nothing behind.

        // ... with the forced container it works.
        job.format = OutputPath::forcedFormat(job.outputPath);
        r = runJob(job);
        QVERIFY2(r.status == FFmpegRunner::Status::Success, qPrintable(r.message));
        QCOMPARE(streamKinds(job.outputPath), streamKinds(m_video));
    }

    void existingOutputIsNeverOverwrittenByDefault()
    {
        QTemporaryDir dir;
        const QString out = dir.filePath("keep.mp4");
        QFile f(out);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("precious");
        f.close();

        FFmpegRunner::Job job;
        job.inputPath = m_video;
        job.outputPath = out;
        job.startMs = 0;
        job.durationMs = 1000;

        auto r = runJob(job);
        QCOMPARE(r.status, FFmpegRunner::Status::Failed);
        QVERIFY(r.message.contains("already exists"));
        QFile check(out);
        QVERIFY(check.open(QIODevice::ReadOnly));
        QCOMPARE(check.readAll(), QByteArray("precious")); // untouched
        check.close();

        job.overwrite = true; // explicit consent
        r = runJob(job);
        QVERIFY2(r.status == FFmpegRunner::Status::Success, qPrintable(r.message));
        QVERIFY(QFileInfo(out).size() > 100);
    }

    void cancelRemovesThePartialFile()
    {
        QTemporaryDir dir;
        FFmpegRunner::Job job;
        job.inputPath = m_video;
        job.outputPath = dir.filePath("cancelled.mp4");
        job.startMs = 0;
        job.durationMs = 4000;

        FFmpegRunner runner;
        QSignalSpy done(&runner, &FFmpegRunner::finished);
        runner.start(job);
        QVERIFY(runner.isRunning());
        runner.cancel();
        QVERIFY(done.wait(10000));
        const auto r = done.first().at(0).value<FFmpegRunner::Result>();
        QCOMPARE(r.status, FFmpegRunner::Status::Cancelled);
        QVERIFY(!QFileInfo::exists(job.outputPath));
        QVERIFY(!runner.isRunning());
    }

    void failuresAreReportedAndCleanedUp()
    {
        QTemporaryDir dir;
        FFmpegRunner::Job job;
        job.outputPath = dir.filePath("never.mp4");
        job.startMs = 0;
        job.durationMs = 1000;

        job.inputPath = dir.filePath("missing.mp4");
        auto r = runJob(job);
        QCOMPARE(r.status, FFmpegRunner::Status::Failed);
        QVERIFY(r.message.contains("not found"));

        // A file that exists but is not a video: ffmpeg itself fails.
        const QString junk = dir.filePath("junk.mp4");
        QFile f(junk);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("this is not a video");
        f.close();
        job.inputPath = junk;
        r = runJob(job);
        QCOMPARE(r.status, FFmpegRunner::Status::Failed);
        QVERIFY(!r.message.isEmpty());
        QVERIFY(!QFileInfo::exists(job.outputPath));

        job.inputPath = m_video;
        job.durationMs = 0;
        r = runJob(job);
        QCOMPARE(r.status, FFmpegRunner::Status::Failed);
        QVERIFY(r.message.contains("empty"));

        job.durationMs = 1000;
        job.outputPath = job.inputPath; // would overwrite the source
        job.overwrite = true;
        r = runJob(job);
        QCOMPARE(r.status, FFmpegRunner::Status::Failed);
    }

    void missingFfmpegIsReported()
    {
        QTemporaryDir dir;
        ToolLocator::setOverride("ffmpeg", dir.filePath("no-such-ffmpeg"));
        FFmpegRunner::Job job;
        job.inputPath = m_video;
        job.outputPath = dir.filePath("x.mp4");
        job.durationMs = 1000;
        const auto r = runJob(job);
        ToolLocator::setOverride("ffmpeg", QString());
        QCOMPARE(r.status, FFmpegRunner::Status::Failed);
        QVERIFY(r.message.contains("ffmpeg"));
    }

private:
    QString m_video;
};

QTEST_MAIN(TstFfmpegRunner)
#include "tst_ffmpegrunner.moc"
