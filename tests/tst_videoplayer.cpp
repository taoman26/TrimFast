// Integration test for VideoPlayer. Needs a real clip: set TRIMFAST_TEST_VIDEO
// to a 25 fps file of at least 5 s (see scripts/make_test_media.sh). Skipped otherwise.
#include "playback/VideoPlayer.h"

#include <QSignalSpy>
#include <QVideoSink>
#include <QtTest>

class TstVideoPlayer : public QObject
{
    Q_OBJECT

private slots:
    void playbackAndSeek()
    {
        const QString path = qEnvironmentVariable("TRIMFAST_TEST_VIDEO");
        if (path.isEmpty())
            QSKIP("TRIMFAST_TEST_VIDEO not set");

        VideoPlayer player;
        QSignalSpy loaded(&player, &VideoPlayer::loaded);
        QSignalSpy frames(player.videoSink(), &QVideoSink::videoFrameChanged);

        player.open(path, 25.0);
        QVERIFY(loaded.wait(5000));
        QVERIFY2(player.duration() >= 5000, qPrintable(QString::number(player.duration())));
        QVERIFY(!player.isPlaying());
        QVERIFY(player.hasMedia());

        // Absolute seek and clamping.
        player.seek(2000);
        QCOMPARE(player.position(), qint64(2000));
        player.seek(-5);
        QCOMPARE(player.position(), qint64(0));
        player.seek(1000000);
        QCOMPARE(player.position(), player.duration());

        // Frame stepping (40 ms at 25 fps) shows a new frame each time.
        player.seek(2000);
        frames.clear();
        player.stepFrames(1);
        QCOMPARE(player.position(), qint64(2040));
        QTRY_VERIFY_WITH_TIMEOUT(frames.count() >= 1, 2000);
        frames.clear();
        player.stepFrames(-2);
        QCOMPARE(player.position(), qint64(1960));
        QTRY_VERIFY_WITH_TIMEOUT(frames.count() >= 1, 2000);
        player.stepFrames(-1000);
        QCOMPARE(player.position(), qint64(0));

        // Relative seek.
        player.seek(3000);
        player.seekRelative(1000);
        QCOMPARE(player.position(), qint64(4000));
        player.seekRelative(-9000);
        QCOMPARE(player.position(), qint64(0));

        // Playback advances in (roughly) real time and can be paused.
        player.seek(2000);
        QSignalSpy playing(&player, &VideoPlayer::playingChanged);
        player.play();
        QTRY_VERIFY_WITH_TIMEOUT(player.isPlaying(), 3000);
        QTest::qWait(1000);
        const qint64 afterOneSecond = player.position();
        QVERIFY2(afterOneSecond > 2500 && afterOneSecond < 3600, qPrintable(QString::number(afterOneSecond)));
        player.pause();
        QTRY_VERIFY_WITH_TIMEOUT(!player.isPlaying(), 3000);

        // goToEnd stays within one or two frames of the end; play from there restarts.
        player.goToEnd();
        QVERIFY(player.position() >= player.duration() - 80);
        QVERIFY(player.position() < player.duration());
        player.play();
        QTest::qWait(500);
        QVERIFY2(player.position() < 2000, qPrintable(QString::number(player.position())));
        player.pause();

        player.close();
        QVERIFY(!player.hasMedia());
    }
};

QTEST_MAIN(TstVideoPlayer)
#include "tst_videoplayer.moc"
