#include "core/InsvPairResolver.h"

#include <QtTest>
#include <ctime>

namespace {
MediaInfo info(int w, int h, double fps, qint64 durationMs)
{
    MediaInfo m;
    m.path = "x";
    StreamInfo v;
    v.type = "video";
    v.codec = "h264";
    v.width = w;
    v.height = h;
    v.fps = fps;
    m.streams << v;
    m.durationMs = durationMs;
    return m;
}
} // namespace

class TstInsvPair : public QObject
{
    Q_OBJECT

private slots:
    void partnerPath_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("partner");
        QTest::newRow("lens0") << "/v/VID_20260923_105655_00_082.insv" << "/v/VID_20260923_105655_10_082.insv";
        QTest::newRow("lens1") << "/v/VID_20260923_105655_10_082.insv" << "/v/VID_20260923_105655_00_082.insv";
        QTest::newRow("upper-ext") << "/v/VID_1_00_7.INSV" << "/v/VID_1_10_7.INSV";
        QTest::newRow("space-dir") << "/my videos/日本語_00_12.insv" << "/my videos/日本語_10_12.insv";
        QTest::newRow("trimmed-name") << "/v/VID_1_00_082_trim.insv" << ""; // not the raw naming
        QTest::newRow("lrv-proxy") << "/v/LRV_20260924_121256_11_100.insv" << ""; // _11_ is a single-file proxy
        QTest::newRow("mp4") << "/v/VID_1_00_082.mp4" << "";
        QTest::newRow("plain") << "/v/clip.insv" << "";
    }
    void partnerPath()
    {
        QFETCH(QString, path);
        QFETCH(QString, partner);
        QCOMPARE(InsvPairResolver::partnerPath(path), partner);
    }

    void primary()
    {
        QVERIFY(InsvPairResolver::isPrimary("/v/VID_1_00_082.insv"));
        QVERIFY(!InsvPairResolver::isPrimary("/v/VID_1_10_082.insv"));
        QVERIFY(!InsvPairResolver::isPrimary("/v/clip.insv"));
    }

    void consistency()
    {
        const MediaInfo a = info(2880, 2880, 25.0, 168120);
        QVERIFY(InsvPairResolver::isConsistent(a, info(2880, 2880, 25.0, 168080))); // one frame shorter (real data)
        QVERIFY(!InsvPairResolver::isConsistent(a, info(2880, 2880, 25.0, 170000))); // too different
        QVERIFY(!InsvPairResolver::isConsistent(a, info(2880, 1440, 25.0, 168120)));
        QVERIFY(!InsvPairResolver::isConsistent(a, info(2880, 2880, 30.0, 168120)));
        MediaInfo noVideo;
        noVideo.path = "x";
        QVERIFY(!InsvPairResolver::isConsistent(a, noVideo));
    }

    // ---- camera naming (what the Insta360 apps pair by) ----
    void followsCameraNaming_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<bool>("expected");
        QTest::newRow("video lens0") << "/v/VID_20260923_105655_00_082.insv" << true;
        QTest::newRow("video lens1") << "/v/VID_20260923_105655_10_082.insv" << true;
        QTest::newRow("proxy") << "/v/LRV_20260924_121256_11_100.insv" << true;
        QTest::newRow("upper ext") << "/v/VID_20260923_105655_00_082.INSV" << true;
        QTest::newRow("two-part prefix") << "/v/PRO_VID_20260101_000000_00_001.insv" << true;
        QTest::newRow("long serial") << "/v/VID_20260923_105655_00_12345.insv" << true;
        // What the apps showed as two separate videos: a suffix breaks the pattern.
        QTest::newRow("suffix _trim") << "/v/VID_20260923_105655_00_082_trim.insv" << false;
        QTest::newRow("renamed") << "/v/holiday.insv" << false;
        QTest::newRow("not insv") << "/v/VID_20260923_105655_00_082.mp4" << false;
        QTest::newRow("short date") << "/v/VID_2026092_105655_00_082.insv" << false;
        QTest::newRow("short time") << "/v/VID_20260923_10565_00_082.insv" << false;
        QTest::newRow("no prefix") << "/v/20260923_105655_00_082.insv" << false;
        QTest::newRow("no serial") << "/v/VID_20260923_105655_00_.insv" << false;
    }
    void followsCameraNaming()
    {
        QFETCH(QString, path);
        QFETCH(bool, expected);
        QCOMPARE(InsvPairResolver::followsCameraNaming(path), expected);
    }

    void shiftedCameraName_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<int>("seconds");
        QTest::addColumn<QString>("expected"); // empty = not a camera name
        QTest::newRow("plus one") << "VID_20260923_105655_00_082.insv" << 1 << "VID_20260923_105656_00_082.insv";
        QTest::newRow("plus five") << "VID_20260923_105655_10_082.insv" << 5 << "VID_20260923_105700_10_082.insv";
        QTest::newRow("zero") << "VID_20260923_105655_00_082.insv" << 0 << "VID_20260923_105655_00_082.insv";
        QTest::newRow("minus one") << "VID_20260923_105655_00_082.insv" << -1 << "VID_20260923_105654_00_082.insv";
        QTest::newRow("carry minute") << "VID_20260923_105659_00_082.insv" << 1 << "VID_20260923_105700_00_082.insv";
        QTest::newRow("carry hour") << "VID_20260923_105959_00_082.insv" << 1 << "VID_20260923_110000_00_082.insv";
        QTest::newRow("carry noon") << "VID_20260923_115959_00_082.insv" << 1 << "VID_20260923_120000_00_082.insv";
        QTest::newRow("big shift") << "VID_20260923_105655_00_082.insv" << 3600 << "VID_20260923_115655_00_082.insv";
        QTest::newRow("carry day") << "VID_20260923_235959_00_082.insv" << 1 << "VID_20260924_000000_00_082.insv";
        QTest::newRow("carry month") << "VID_20260930_235959_00_082.insv" << 1 << "VID_20261001_000000_00_082.insv";
        QTest::newRow("carry year") << "VID_20261231_235959_00_082.insv" << 1 << "VID_20270101_000000_00_082.insv";
        QTest::newRow("leap day") << "VID_20280228_235959_00_001.insv" << 1 << "VID_20280229_000000_00_001.insv";
        QTest::newRow("no leap day") << "VID_20270228_235959_00_001.insv" << 1 << "VID_20270301_000000_00_001.insv";
        QTest::newRow("proxy") << "LRV_20260924_121256_11_100.insv" << 1 << "LRV_20260924_121257_11_100.insv";
        QTest::newRow("two-part prefix") << "PRO_VID_20260101_000000_00_001.insv" << 2 << "PRO_VID_20260101_000002_00_001.insv";
        QTest::newRow("keeps extension case") << "VID_20260923_105655_00_082.INSV" << 1 << "VID_20260923_105656_00_082.INSV";
        QTest::newRow("impossible date") << "VID_20261399_105655_00_082.insv" << 1 << "";
        QTest::newRow("impossible time") << "VID_20260923_256099_00_082.insv" << 1 << "";
        QTest::newRow("not a camera name") << "clip.insv" << 1 << "";
        QTest::newRow("already suffixed") << "VID_20260923_105655_00_082_trim.insv" << 1 << "";
    }
    void shiftedCameraName()
    {
        QFETCH(QString, name);
        QFETCH(int, seconds);
        QFETCH(QString, expected);
        const QString shifted = InsvPairResolver::shiftedCameraName("/my videos/日本語/" + name, seconds);
        if (expected.isEmpty())
            QVERIFY(shifted.isEmpty());
        else
            QCOMPARE(shifted, "/my videos/日本語/" + expected); // same folder, new name
    }

    void shiftedNamesStayPairedAndFollowThePattern()
    {
        const QString a = InsvPairResolver::shiftedCameraName("/v/VID_20260923_105655_00_082.insv", 3);
        const QString b = InsvPairResolver::shiftedCameraName("/v/VID_20260923_105655_10_082.insv", 3);
        QVERIFY(InsvPairResolver::followsCameraNaming(a));
        QVERIFY(InsvPairResolver::followsCameraNaming(b));
        QCOMPARE(InsvPairResolver::partnerPath(a), b); // still recognised as the two lenses of one recording
        QCOMPARE(InsvPairResolver::partnerPath(b), a);
    }

#ifdef Q_OS_UNIX
    // The time in the name is the camera's wall clock: a local daylight-saving gap must not matter.
    void shiftIgnoresTheLocalTimeZone()
    {
        const QByteArray old = qgetenv("TZ");
        qputenv("TZ", "America/New_York");
        tzset();
        // 02:30 on 2026-03-08 does not exist in New York (clocks jump 02:00 -> 03:00).
        QCOMPARE(InsvPairResolver::shiftedCameraName("/v/VID_20260308_023000_00_001.insv", 1),
                 QString("/v/VID_20260308_023001_00_001.insv"));
        QCOMPARE(InsvPairResolver::shiftedCameraName("/v/VID_20260308_015959_00_001.insv", 1),
                 QString("/v/VID_20260308_020000_00_001.insv"));
        if (old.isNull())
            qunsetenv("TZ");
        else
            qputenv("TZ", old);
        tzset();
    }
#endif

    void partnerOutput()
    {
        const QString in = "/v/VID_20260923_105655_00_082.insv";
        const QString partner = "/v/VID_20260923_105655_10_082.insv";
        // Default name: the lens token is swapped.
        QCOMPARE(InsvPairResolver::partnerOutputPath("/out/VID_20260923_105655_00_082_trim.insv", in, partner, "_trim"),
                 QString("/out/VID_20260923_105655_10_082_trim.insv"));
        QCOMPARE(InsvPairResolver::partnerOutputPath("/out/VID_20260923_105655_00_082_trim_2.insv", in, partner, "_trim"),
                 QString("/out/VID_20260923_105655_10_082_trim_2.insv"));
        // The other way round.
        QCOMPARE(InsvPairResolver::partnerOutputPath("/out/VID_20260923_105655_10_082_trim.insv", partner, in, "_trim"),
                 QString("/out/VID_20260923_105655_00_082_trim.insv"));
        // Camera naming: the shifted copy of lens 0 maps to the shifted copy of lens 1.
        QCOMPARE(InsvPairResolver::partnerOutputPath("/out/VID_20260923_105656_00_082.insv", in, partner, "_trim"),
                 QString("/out/VID_20260923_105656_10_082.insv"));
        // Renamed by the user (no lens token): fall back to the partner's own name in that folder.
        QCOMPARE(InsvPairResolver::partnerOutputPath("/out/holiday.insv", in, partner, "_trim"),
                 QString("/out/VID_20260923_105655_10_082_trim.insv"));
    }
};

QTEST_APPLESS_MAIN(TstInsvPair)
#include "tst_insvpair.moc"
