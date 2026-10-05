// Pure tests (no ffmpeg needed): argument building, progress parsing, output naming.
#include "core/FFmpegRunner.h"
#include "core/OutputPath.h"

#include <QSet>
#include <QtTest>

namespace {

FFmpegRunner::Job sampleJob()
{
    FFmpegRunner::Job j;
    j.inputPath = "/videos/in.mp4";
    j.outputPath = "/videos/in_trim.mp4";
    j.startMs = 1500;
    j.durationMs = 2250;
    return j;
}

int indexOf(const QStringList &args, const QString &what, int from = 0)
{
    return int(args.indexOf(what, from));
}

} // namespace

class TstFfmpegArgs : public QObject
{
    Q_OBJECT

private slots:
    void streamCopyIsAlwaysUsed()
    {
        const QStringList a = FFmpegRunner::buildArgs(sampleJob());
        const int c = indexOf(a, "-c");
        QVERIFY(c >= 0);
        QCOMPARE(a.at(c + 1), QString("copy"));
        QCOMPARE(a.count("-c"), 1);
        QCOMPARE(a.at(indexOf(a, "-map") + 1), QString("0"));
        QCOMPARE(a.at(indexOf(a, "-map_metadata") + 1), QString("0"));
        QCOMPARE(a.at(indexOf(a, "-map_chapters") + 1), QString("0"));
        QVERIFY(a.contains("-copy_unknown"));
        QCOMPARE(a.at(indexOf(a, "-avoid_negative_ts") + 1), QString("make_zero"));
    }

    // The core guarantee: only known-safe options can ever appear, so nothing that
    // re-encodes (codec/filter/quality/rate/format conversion) can sneak in.
    void onlyAllowListedOptions()
    {
        static const QSet<QString> allowed = {
            "-hide_banner", "-nostdin", "-v", "-nostats", "-progress", "-ss", "-i", "-t", "-map",
            "-c", "-map_metadata", "-map_chapters", "-copy_unknown", "-avoid_negative_ts", "-f",
            "-n", "-y"};
        QList<FFmpegRunner::Job> jobs;
        jobs << sampleJob();
        FFmpegRunner::Job insv = sampleJob();
        insv.outputPath = "/v/a.insv";
        insv.format = "mp4";
        jobs << insv;
        FFmpegRunner::Job over = sampleJob();
        over.overwrite = true;
        jobs << over;

        for (const FFmpegRunner::Job &job : jobs) {
            const QStringList a = FFmpegRunner::buildArgs(job);
            for (int i = 0; i < a.size(); ++i) {
                if (a.at(i).startsWith('-'))
                    QVERIFY2(allowed.contains(a.at(i)), qPrintable("unexpected option: " + a.at(i)));
            }
        }
    }

    void trimRangeAndInputOrder()
    {
        const QStringList a = FFmpegRunner::buildArgs(sampleJob());
        // -ss must come before -i (fast keyframe seek on the input).
        const int ss = indexOf(a, "-ss");
        const int in = indexOf(a, "-i");
        QVERIFY(ss >= 0 && in > ss);
        QCOMPARE(a.at(ss + 1), QString("1.500"));
        QCOMPARE(a.at(in + 1), QString("/videos/in.mp4"));
        QCOMPARE(a.at(indexOf(a, "-t") + 1), QString("2.250"));
        QCOMPARE(a.last(), QString("/videos/in_trim.mp4")); // output is the last argument
    }

    void overwriteProtection()
    {
        FFmpegRunner::Job j = sampleJob();
        QStringList a = FFmpegRunner::buildArgs(j);
        QVERIFY(a.contains("-n"));
        QVERIFY(!a.contains("-y"));
        j.overwrite = true;
        a = FFmpegRunner::buildArgs(j);
        QVERIFY(a.contains("-y"));
        QVERIFY(!a.contains("-n"));
    }

    void containerFormatOnlyWhenRequested()
    {
        FFmpegRunner::Job j = sampleJob();
        QVERIFY(!FFmpegRunner::buildArgs(j).contains("-f"));
        j.format = "mp4";
        const QStringList a = FFmpegRunner::buildArgs(j);
        QCOMPARE(a.at(indexOf(a, "-f") + 1), QString("mp4"));
    }

    void unusualPathsStayOneArgument()
    {
        FFmpegRunner::Job j = sampleJob();
        j.inputPath = "/videos/my clip (1) \"x\" 日本語.mp4";
        j.outputPath = "/out dir/出力 ファイル.mp4";
        const QStringList a = FFmpegRunner::buildArgs(j);
        QCOMPARE(a.at(indexOf(a, "-i") + 1), j.inputPath);
        QCOMPARE(a.last(), j.outputPath);
    }

    void relativeAndDashPathsBecomeAbsolute()
    {
        FFmpegRunner::Job j = sampleJob();
        j.inputPath = "-evil.mp4";
        j.outputPath = "-out.mp4";
        const QStringList a = FFmpegRunner::buildArgs(j);
        const QString in = a.at(indexOf(a, "-i") + 1);
        QVERIFY(QDir::isAbsolutePath(in));
        QVERIFY(!in.startsWith('-'));
        QVERIFY(QDir::isAbsolutePath(a.last()));
        QVERIFY(!a.last().startsWith('-'));
    }

    void formatSeconds_data()
    {
        QTest::addColumn<qint64>("ms");
        QTest::addColumn<QString>("expected");
        QTest::newRow("zero") << qint64(0) << "0.000";
        QTest::newRow("ms") << qint64(7) << "0.007";
        QTest::newRow("sec") << qint64(1500) << "1.500";
        QTest::newRow("long") << qint64(3725042) << "3725.042";
        QTest::newRow("negative") << qint64(-5) << "0.000";
    }
    void formatSeconds()
    {
        QFETCH(qint64, ms);
        QFETCH(QString, expected);
        QCOMPARE(FFmpegRunner::formatSeconds(ms), expected);
    }

    void progressParsing()
    {
        QCOMPARE(FFmpegRunner::parseOutTimeMs("out_time_us=1500000\n").value_or(-1), qint64(1500));
        QCOMPARE(FFmpegRunner::parseOutTimeMs("out_time_ms=2000000").value_or(-1), qint64(2000));
        QCOMPARE(FFmpegRunner::parseOutTimeMs("out_time_us=0").value_or(-1), qint64(0));
        QVERIFY(!FFmpegRunner::parseOutTimeMs("out_time_us=N/A").has_value());
        QVERIFY(!FFmpegRunner::parseOutTimeMs("out_time_us=-9223372036854775807").has_value());
        QVERIFY(!FFmpegRunner::parseOutTimeMs("out_time=00:00:01.500000").has_value());
        QVERIFY(!FFmpegRunner::parseOutTimeMs("frame=42").has_value());
        QVERIFY(!FFmpegRunner::parseOutTimeMs("").has_value());

        QVERIFY(FFmpegRunner::isProgressEnd("progress=end\n"));
        QVERIFY(!FFmpegRunner::isProgressEnd("progress=continue"));
    }

    void outputNaming()
    {
        QCOMPARE(OutputPath::suggest("/v/clip.mp4", "_trim"), QString("/v/clip_trim.mp4"));
        QCOMPARE(OutputPath::suggest("/v/a.b.c.mov", "_trim"), QString("/v/a.b.c_trim.mov"));
        QCOMPARE(OutputPath::suggest("/v/noext", "_cut"), QString("/v/noext_cut"));
        QCOMPARE(OutputPath::suggest("/v/日本語 動画.mp4", "_trim"), QString("/v/日本語 動画_trim.mp4"));
        QVERIFY(QDir::isAbsolutePath(OutputPath::suggest("rel.mp4", "_trim")));
    }

    void forcedFormat()
    {
        QCOMPARE(OutputPath::forcedFormat("/v/a.insv"), QString("mp4"));
        QCOMPARE(OutputPath::forcedFormat("/v/a.LRV"), QString("mp4"));
        QVERIFY(OutputPath::forcedFormat("/v/a.mp4").isEmpty());
        QVERIFY(OutputPath::forcedFormat("/v/a.mkv").isEmpty());
    }
};

QTEST_APPLESS_MAIN(TstFfmpegArgs)
#include "tst_ffmpegargs.moc"
