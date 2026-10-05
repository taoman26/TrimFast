// Plays a clip in the real window and checks that memory does not keep growing
// (Linux only: reads /proc). Any video of >= 6 s works; set TRIMFAST_TEST_VIDEO.
#include "app/MainWindow.h"
#include "app/PerfLog.h"

#include <QAction>
#include <QLabel>
#include <QTemporaryDir>
#include <QtTest>

namespace {
bool press(MainWindow &w, const QKeySequence &key)
{
    for (QAction *a : w.findChildren<QAction *>()) {
        if (a->shortcut() == key && a->isEnabled()) {
            a->trigger();
            return true;
        }
    }
    return false;
}
bool ready(MainWindow &w)
{
    bool kf = false, scanning = false;
    for (const QLabel *l : w.findChildren<QLabel *>()) {
        kf |= l->text().contains("Keyframes: ");
        scanning |= l->text().contains("scanning");
    }
    return kf && !scanning;
}
} // namespace

class TstPlaybackMemory : public QObject
{
    Q_OBJECT

private slots:
    void playingDoesNotLeak()
    {
#ifndef Q_OS_LINUX
        QSKIP("needs /proc");
#endif
        const QString video = qEnvironmentVariable("TRIMFAST_TEST_VIDEO");
        if (video.isEmpty())
            QSKIP("TRIMFAST_TEST_VIDEO not set");
        qputenv("TRIMFAST_AUDIO", "0");

        QTemporaryDir cfg;
        MainWindow w(nullptr, cfg.path());
        w.resize(1000, 700);
        w.show(); // frames are converted and painted, as for a user
        w.openPath(video);
        QTRY_VERIFY_WITH_TIMEOUT(ready(w), 10000);

        QVERIFY(press(w, QKeySequence(Qt::Key_Space)));
        QTest::qWait(1500); // let buffers fill up
        const double before = PerfLog::residentMiB();
        QTest::qWait(3000);
        const double after = PerfLog::residentMiB();
        press(w, QKeySequence(Qt::Key_Space));
        qInfo("playback RSS: %.1f MiB -> %.1f MiB (+%.1f)", before, after, after - before);
        QVERIFY2(after - before < 100.0, qPrintable(QString("grew by %1 MiB").arg(after - before)));
    }
};

QTEST_MAIN(TstPlaybackMemory)
#include "tst_playbackmemory.moc"
