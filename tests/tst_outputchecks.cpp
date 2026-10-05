// OutputVerifier::compare, OutputCheck and OutputPath::suggestUnique (no ffmpeg needed).
#include "core/InsvPairResolver.h"
#include "core/OutputCheck.h"
#include "core/OutputPath.h"
#include "core/OutputVerifier.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace {

QByteArray probeJson(const QString &streams, const QString &chapters = "[]", const QString &tags = "{}")
{
    return QString(R"({"streams": %1, "chapters": %2, "format": {"tags": %3}})")
        .arg(streams, chapters, tags)
        .toUtf8();
}

const QString kVideo = R"({"codec_type":"video","codec_name":"h264","width":1920,"height":1080})";
const QString kAudio = R"({"codec_type":"audio","codec_name":"aac"})";
const QString kSub = R"({"codec_type":"subtitle","codec_name":"mov_text"})";

void touch(const QString &path)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
}

} // namespace

class TstOutputChecks : public QObject
{
    Q_OBJECT

private slots:
    // ---- OutputVerifier::compare ----
    void identicalIsClean()
    {
        const QByteArray j = probeJson("[" + kVideo + "," + kAudio + "]");
        QVERIFY(OutputVerifier::compare(j, j).isEmpty());
    }

    void missingStreamIsReported()
    {
        const auto w = OutputVerifier::compare(probeJson("[" + kVideo + "," + kAudio + "," + kSub + "]"),
                                               probeJson("[" + kVideo + "," + kAudio + "]"));
        QCOMPARE(w.size(), 1);
        QVERIFY(w.first().contains("subtitle:mov_text"));
    }

    void changedCodecIsReportedBothWays()
    {
        const QString hevc = R"({"codec_type":"video","codec_name":"hevc","width":1920,"height":1080})";
        const auto w = OutputVerifier::compare(probeJson("[" + kVideo + "]"), probeJson("[" + hevc + "]"));
        QCOMPARE(w.size(), 2); // h264 missing, hevc unexpected
    }

    void chaptersLostIsReported()
    {
        const QByteArray in = probeJson("[" + kVideo + "]", R"([{"id":1},{"id":2}])");
        QVERIFY(OutputVerifier::compare(in, probeJson("[" + kVideo + "]", R"([{"id":1}])")).isEmpty()); // trimmed: fine
        const auto w = OutputVerifier::compare(in, probeJson("[" + kVideo + "]"));
        QCOMPARE(w.size(), 1);
        QVERIFY(w.first().contains("Chapters"));
    }

    void tagsAreComparedButVolatileOnesIgnored()
    {
        const QString inTags = R"({"title":"My trip","creation_time":"2026-09-23","encoder":"cam","major_brand":"avc1"})";
        const QString okTags = R"({"title":"My trip","creation_time":"2026-09-23","encoder":"Lavf62","major_brand":"isom"})";
        const QByteArray in = probeJson("[" + kVideo + "]", "[]", inTags);
        QVERIFY(OutputVerifier::compare(in, probeJson("[" + kVideo + "]", "[]", okTags)).isEmpty());

        auto w = OutputVerifier::compare(in, probeJson("[" + kVideo + "]", "[]", R"({"creation_time":"2026-09-23"})"));
        QCOMPARE(w.size(), 1);
        QVERIFY(w.first().contains("title"));
        w = OutputVerifier::compare(in, probeJson("[" + kVideo + "]", "[]", R"({"title":"Other","creation_time":"2026-09-23"})"));
        QCOMPARE(w.size(), 1);
        QVERIFY(w.first().contains("changed"));
    }

    void sphericalSideDataMustSurvive()
    {
        const QString sphericalVideo =
            R"({"codec_type":"video","codec_name":"h264","width":1920,"height":1080,)"
            R"("side_data_list":[{"side_data_type":"Spherical Mapping"}]})";
        const auto w = OutputVerifier::compare(probeJson("[" + sphericalVideo + "]"), probeJson("[" + kVideo + "]"));
        QCOMPARE(w.size(), 1);
        QVERIFY(w.first().contains("Spherical Mapping"));
        QVERIFY(OutputVerifier::compare(probeJson("[" + sphericalVideo + "]"), probeJson("[" + sphericalVideo + "]")).isEmpty());
    }

    void garbageDoesNotCrash()
    {
        // Unparseable JSON compares as "nothing" and must not throw or crash.
        (void)OutputVerifier::compare("not json", "also not json");
        QVERIFY(OutputVerifier::compare("", "").isEmpty());
    }

    // ---- OutputCheck ----
    void estimate()
    {
        QCOMPARE(OutputCheck::estimateBytes(1000, 500, 1000), qint64(515)); // half + 3 %
        QCOMPARE(OutputCheck::estimateBytes(1000, 2000, 1000), qint64(1030)); // never more than the whole
        QCOMPARE(OutputCheck::estimateBytes(1000, 500, 0), qint64(1030)); // unknown length: assume all
        QCOMPARE(OutputCheck::estimateBytes(1000, 1000, 1000, 100), qint64(1130)); // + trailer
        QCOMPARE(OutputCheck::estimateBytes(0, 1, 1, 77), qint64(77));
    }

    void fatLimit()
    {
        const qint64 fiveGiB = qint64(5) * 1024 * 1024 * 1024;
        const qint64 small = 100 * 1024 * 1024;
        QVERIFY(!OutputCheck::evaluate(fiveGiB, fiveGiB, "vfat", -1).ok());
        QVERIFY(!OutputCheck::evaluate(fiveGiB, fiveGiB, "FAT32", -1).ok());
        QVERIFY(!OutputCheck::evaluate(fiveGiB, fiveGiB, "msdos", -1).ok());
        QVERIFY(OutputCheck::evaluate(small, small, "vfat", -1).ok());          // small files are fine on FAT
        QVERIFY(OutputCheck::evaluate(fiveGiB, fiveGiB, "exfat", -1).ok());     // exFAT has no 4 GiB limit
        QVERIFY(OutputCheck::evaluate(fiveGiB, fiveGiB, "ext4", -1).ok());
        QVERIFY(OutputCheck::evaluate(fiveGiB, fiveGiB, "bfs", -1).ok());       // Haiku's native FS
        QVERIFY(OutputCheck::evaluate(fiveGiB, fiveGiB, "", -1).ok());          // unknown FS: do not block
    }

    void freeSpace()
    {
        QVERIFY(!OutputCheck::evaluate(500, 1000, "ext4", 5000).needsConfirmation());
        QVERIFY(!OutputCheck::evaluate(500, 1000, "ext4", 1000).needsConfirmation()); // exactly enough
        const auto r = OutputCheck::evaluate(500, 1000, "ext4", 999);
        QVERIFY(r.ok());                   // not a certain failure: the user may go on
        QVERIFY(r.needsConfirmation());
        QVERIFY(r.cautions.first().contains("free space"));
        QVERIFY(!OutputCheck::evaluate(500, 1000, "ext4", -1).needsConfirmation()); // unknown: not checked
    }

    // Haiku's packagefs reports "0 of 0 bytes": that is "unknown", not "full".
    void untrustworthyFreeSpaceIsUnknown()
    {
        QCOMPARE(OutputCheck::usableFreeBytes(0, 0), qint64(-1));
        QCOMPARE(OutputCheck::usableFreeBytes(-1, 1000), qint64(-1));
        QCOMPARE(OutputCheck::usableFreeBytes(0, -5), qint64(-1));
        QCOMPARE(OutputCheck::usableFreeBytes(27867138048, 42949672960), qint64(27867138048)); // real Haiku bfs
        QCOMPARE(OutputCheck::usableFreeBytes(0, 1000), qint64(0)); // a genuinely full disk
        // ... which then asks for confirmation instead of blocking.
        QVERIFY(OutputCheck::evaluate(500, 1000, "bfs", OutputCheck::usableFreeBytes(0, 1000)).needsConfirmation());
        QVERIFY(!OutputCheck::evaluate(500, 1000, "packagefs", OutputCheck::usableFreeBytes(0, 0)).needsConfirmation());
    }

    // ---- OutputPath::suggestUnique ----
    void uniqueNames()
    {
        QTemporaryDir dir;
        const QString in = dir.filePath("clip.mp4");
        QCOMPARE(OutputPath::suggestUnique(in, "_trim"), dir.filePath("clip_trim.mp4"));

        touch(dir.filePath("clip_trim.mp4"));
        QCOMPARE(OutputPath::suggestUnique(in, "_trim"), dir.filePath("clip_trim_2.mp4"));
        touch(dir.filePath("clip_trim_2.mp4"));
        touch(dir.filePath("clip_trim_3.mp4"));
        QCOMPARE(OutputPath::suggestUnique(in, "_trim"), dir.filePath("clip_trim_4.mp4"));
    }

    // Insta360 files keep the camera's pattern (no suffix): the apps pair the lens files by name.
    void cameraNamedFilesAreShiftedNotSuffixed()
    {
        QTemporaryDir dir;
        const QString a = dir.filePath("VID_20260923_105655_00_082.insv");
        const QString b = dir.filePath("VID_20260923_105655_10_082.insv");
        QCOMPARE(OutputPath::suggestUnique(a, "_trim"), dir.filePath("VID_20260923_105656_00_082.insv"));
        QCOMPARE(OutputPath::suggestUnique(a, "_cut", b), dir.filePath("VID_20260923_105656_00_082.insv")); // suffix unused

        // Taken -> the next second; the partner's name counts too.
        touch(dir.filePath("VID_20260923_105656_00_082.insv"));
        QCOMPARE(OutputPath::suggestUnique(a, "_trim", b), dir.filePath("VID_20260923_105657_00_082.insv"));
        touch(dir.filePath("VID_20260923_105657_10_082.insv")); // only the partner's name is taken
        QCOMPARE(OutputPath::suggestUnique(a, "_trim", b), dir.filePath("VID_20260923_105658_00_082.insv"));

        // The result is a camera name too, and it pairs with the partner's.
        const QString out = OutputPath::suggestUnique(a, "_trim", b);
        QVERIFY(InsvPairResolver::followsCameraNaming(out));
    }

    void cameraNamedProxyAndOrdinaryInsvFiles()
    {
        QTemporaryDir dir;
        QCOMPARE(OutputPath::suggestUnique(dir.filePath("LRV_20260924_121256_11_100.insv"), "_trim"),
                 dir.filePath("LRV_20260924_121257_11_100.insv"));
        // Not a camera name: the usual suffix.
        QCOMPARE(OutputPath::suggestUnique(dir.filePath("holiday.insv"), "_trim"), dir.filePath("holiday_trim.insv"));
        QCOMPARE(OutputPath::suggestUnique(dir.filePath("VID_20260923_105655_00_082_old.insv"), "_trim"),
                 dir.filePath("VID_20260923_105655_00_082_old_trim.insv"));
    }

    void uniqueNamesForPairs()
    {
        QTemporaryDir dir;
        const QString a = dir.filePath("VID_1_00_1.insv");
        const QString b = dir.filePath("VID_1_10_1.insv");
        QCOMPARE(OutputPath::suggestUnique(a, "_trim", b), dir.filePath("VID_1_00_1_trim.insv"));
        // Only the partner's output exists: the primary's name must move on too.
        touch(dir.filePath("VID_1_10_1_trim.insv"));
        QCOMPARE(OutputPath::suggestUnique(a, "_trim", b), dir.filePath("VID_1_00_1_trim_2.insv"));
    }
};

QTEST_APPLESS_MAIN(TstOutputChecks)
#include "tst_outputchecks.moc"
