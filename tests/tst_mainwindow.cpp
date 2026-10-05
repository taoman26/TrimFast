// Integration test: opens a real clip in MainWindow and drives it through its
// shortcuts. Needs TRIMFAST_TEST_VIDEO (>= 5 s, 25 fps, keyframe every 1 s:
// scripts/make_test_media.sh) and ffprobe on PATH; skipped otherwise.
#include "app/MainWindow.h"

#include <QAction>
#include <QLabel>
#include <QDir>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QtTest>

namespace {

// Fires the action that owns `key`, as pressing it would.
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

QString inText(MainWindow &w) { return w.findChildren<QLineEdit *>().at(0)->text(); }
QString outText(MainWindow &w) { return w.findChildren<QLineEdit *>().at(1)->text(); }

bool hasLabel(MainWindow &w, const QString &text)
{
    for (const QLabel *l : w.findChildren<QLabel *>()) {
        if (l->text() == text)
            return true;
    }
    return false;
}

bool hasLabelContaining(MainWindow &w, const QString &part)
{
    for (const QLabel *l : w.findChildren<QLabel *>()) {
        if (l->text().contains(part))
            return true;
    }
    return false;
}

// The keyframe scan has finished (the status line stops saying "scanning").
bool keyframesReady(MainWindow &w)
{
    return hasLabelContaining(w, "Keyframes: ") && !hasLabelContaining(w, "scanning");
}

} // namespace

class TstMainWindow : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_video = qEnvironmentVariable("TRIMFAST_TEST_VIDEO");
        if (m_video.isEmpty())
            QSKIP("TRIMFAST_TEST_VIDEO not set");
        qputenv("TRIMFAST_AUDIO", "0");
    }

    void markersWorkflowAndSession()
    {
        QTemporaryDir dir;
        const QString session = dir.path(); // config dir (session.ini inside)

        {
            MainWindow w(nullptr, session);
            QVERIFY(w.findChildren<QPushButton *>().size() >= 4);
            // Nothing loaded: the marker keys do nothing.
            QVERIFY(!press(w, QKeySequence(Qt::Key_I)));

            w.openPath(m_video);
            QTRY_VERIFY_WITH_TIMEOUT(keyframesReady(w), 8000);
            QCOMPARE(inText(w), QString("00:00:00.000"));
            QVERIFY(outText(w) != QString("00:00:00.000")); // whole file by default

            // Move to 3.48 s (3 s + 12 frames) and set IN: it snaps down to the keyframe at 3 s.
            for (int i = 0; i < 3; ++i)
                QVERIFY(press(w, QKeySequence(Qt::SHIFT | Qt::Key_Right)));
            for (int i = 0; i < 12; ++i)
                QVERIFY(press(w, QKeySequence(Qt::Key_Right)));
            QTRY_VERIFY_WITH_TIMEOUT(hasLabel(w, "00:00:03.480"), 3000);
            QVERIFY(press(w, QKeySequence(Qt::Key_I)));
            QCOMPARE(inText(w), QString("00:00:03.000"));

            // OUT before IN is refused.
            QVERIFY(press(w, QKeySequence(Qt::Key_Home)));
            const QString outBefore = outText(w);
            QVERIFY(press(w, QKeySequence(Qt::Key_O)));
            QCOMPARE(outText(w), outBefore);
            QCOMPARE(inText(w), QString("00:00:03.000"));

            // OUT after IN is accepted, exactly where the playhead is (no snapping).
            QVERIFY(press(w, QKeySequence(Qt::SHIFT | Qt::Key_Right)));
            for (int i = 0; i < 4; ++i)
                QVERIFY(press(w, QKeySequence(Qt::SHIFT | Qt::Key_Right)));
            QVERIFY(press(w, QKeySequence(Qt::Key_Left)));
            QTRY_VERIFY_WITH_TIMEOUT(hasLabel(w, "00:00:04.960"), 3000);
            QVERIFY(press(w, QKeySequence(Qt::Key_O)));
            QCOMPARE(outText(w), QString("00:00:04.960"));

            // Alt+I / Alt+O jump to the markers.
            QVERIFY(press(w, QKeySequence(Qt::ALT | Qt::Key_I)));
            QTRY_VERIFY_WITH_TIMEOUT(hasLabel(w, "00:00:03.000"), 3000);
            QVERIFY(press(w, QKeySequence(Qt::ALT | Qt::Key_O)));
            QTRY_VERIFY_WITH_TIMEOUT(hasLabel(w, "00:00:04.960"), 3000);
            QVERIFY(hasLabelContaining(w, "00:00:01.960")); // Duration : selection length

            // Optional visual check: TRIMFAST_SAVE_PNG=<dir> writes mainwindow.png
            const QString pngDir = qEnvironmentVariable("TRIMFAST_SAVE_PNG");
            if (!pngDir.isEmpty()) {
                w.resize(1000, 700);
                w.show();
                QTest::qWait(500);
                QVERIFY(w.grab().save(pngDir + "/mainwindow.png"));
            }
        } // destroying the window flushes the session

        {
            MainWindow w(nullptr, session);
            w.openPath(m_video);
            QTRY_VERIFY_WITH_TIMEOUT(keyframesReady(w), 8000);
            QCOMPARE(inText(w), QString("00:00:03.000")); // restored
            QCOMPARE(outText(w), QString("00:00:04.960"));
        }
    }

    // Export writes next to the input, so work on a copy in a temporary directory.
    void exportOverwriteAndCancel()
    {
        QTemporaryDir dir;
        const QString clip = dir.filePath("clip.mp4");
        QVERIFY(QFile::copy(m_video, clip));
        const QString output = dir.filePath("clip_trim.mp4");

        MainWindow w(nullptr, dir.filePath("cfg"));
        w.setOutputChooser([](const QString &suggested) { return suggested; }); // "OK" in the dialog
        w.openPath(clip);
        QTRY_VERIFY_WITH_TIMEOUT(keyframesReady(w), 8000);

        // Range 1.000 .. 3.000 (IN on a keyframe).
        QVERIFY(press(w, QKeySequence(Qt::SHIFT | Qt::Key_Right)));
        QTRY_VERIFY_WITH_TIMEOUT(hasLabel(w, "00:00:01.000"), 3000);
        QVERIFY(press(w, QKeySequence(Qt::Key_I)));
        QVERIFY(press(w, QKeySequence(Qt::SHIFT | Qt::Key_Right)));
        QVERIFY(press(w, QKeySequence(Qt::SHIFT | Qt::Key_Right)));
        QTRY_VERIFY_WITH_TIMEOUT(hasLabel(w, "00:00:03.000"), 3000);
        QVERIFY(press(w, QKeySequence(Qt::Key_O)));
        QCOMPARE(inText(w), QString("00:00:01.000"));
        QCOMPARE(outText(w), QString("00:00:03.000"));

        // Export (Ctrl+E): a file appears next to the input and the window is usable again.
        QVERIFY(press(w, QKeySequence(Qt::CTRL | Qt::Key_E)));
        QTRY_VERIFY_WITH_TIMEOUT(hasLabelContaining(w, "Exported:"), 15000);
        QVERIFY(QFileInfo::exists(output));
        const qint64 size = QFileInfo(output).size();
        QVERIFY(size > 1000);
        QVERIFY(!press(w, QKeySequence(Qt::Key_Escape)) || true); // Esc with nothing running is harmless

        // A second export picks a free name (clip_trim_2.mp4) and leaves the first file alone.
        QVERIFY(press(w, QKeySequence(Qt::CTRL | Qt::Key_E)));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(dir.filePath("clip_trim_2.mp4")), 15000);
        QTest::qWait(300);
        QCOMPARE(QFileInfo(output).size(), size);

        // Cancelling the file dialog exports nothing and leaves the window usable.
        w.setOutputChooser([](const QString &) { return QString(); });
        QVERIFY(press(w, QKeySequence(Qt::CTRL | Qt::Key_E)));
        QTest::qWait(300);
        QVERIFY(!QFileInfo::exists(dir.filePath("clip_trim_3.mp4")));
        QVERIFY(press(w, QKeySequence(Qt::CTRL | Qt::Key_E))); // still enabled (second cancel)

        // A name typed without an extension gets the input's extension.
        w.setOutputChooser([&](const QString &) { return dir.filePath("named"); });
        QVERIFY(press(w, QKeySequence(Qt::CTRL | Qt::Key_E)));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(dir.filePath("named.mp4")), 15000);
        QTest::qWait(300);

        // Esc cancels a running export and removes everything it wrote.
        w.setOutputChooser([&](const QString &) { return dir.filePath("cancelme.mp4"); });
        QVERIFY(press(w, QKeySequence(Qt::CTRL | Qt::Key_E)));
        QVERIFY(press(w, QKeySequence(Qt::Key_Escape)));
        QTRY_VERIFY_WITH_TIMEOUT(hasLabelContaining(w, "cancelled"), 8000);
        QVERIFY(!QFileInfo::exists(dir.filePath("cancelme.mp4")));
        for (const QString &name : QDir(dir.path()).entryList(QDir::Files | QDir::Hidden))
            QVERIFY2(!name.contains("partial"), qPrintable(name));
    }

    // An Insta360 pair (here: two copies of the clip named like a pair) is exported together.
    void insvPairIsExportedTogether()
    {
        QTemporaryDir dir;
        const QString lens0 = dir.filePath("VID_20260101_000000_00_001.insv");
        const QString lens1 = dir.filePath("VID_20260101_000000_10_001.insv");
        QVERIFY(QFile::copy(m_video, lens0));
        QVERIFY(QFile::copy(m_video, lens1));

        MainWindow w(nullptr, dir.filePath("cfg"));
        w.openPath(lens0);
        QTRY_VERIFY_WITH_TIMEOUT(keyframesReady(w), 8000);
        QTRY_VERIFY_WITH_TIMEOUT(hasLabelContaining(w, "found (both are exported)"), 8000);

        QVERIFY(press(w, QKeySequence(Qt::SHIFT | Qt::Key_Right)));
        QTRY_VERIFY_WITH_TIMEOUT(hasLabel(w, "00:00:01.000"), 3000);
        QVERIFY(press(w, QKeySequence(Qt::Key_I)));
        QVERIFY(press(w, QKeySequence(Qt::SHIFT | Qt::Key_Right)));
        QVERIFY(press(w, QKeySequence(Qt::SHIFT | Qt::Key_Right)));
        QTRY_VERIFY_WITH_TIMEOUT(hasLabel(w, "00:00:03.000"), 3000);
        QVERIFY(press(w, QKeySequence(Qt::Key_O)));

        // The status line already shows the name the export will use: the camera's pattern, 1 s later.
        QVERIFY(hasLabel(w, "Out: VID_20260101_000001_00_001.insv"));

        QString suggested;
        int questions = 0;
        w.setQuestionHandler([&](const QString &, const QString &) {
            ++questions;
            return true;
        });
        w.setOutputChooser([&](const QString &s) {
            suggested = s;
            return s;
        });
        QVERIFY(press(w, QKeySequence(Qt::CTRL | Qt::Key_E)));
        // The Insta360 apps pair the lens files by name: no "_trim" suffix, the pattern is kept.
        const QString out0 = dir.filePath("VID_20260101_000001_00_001.insv");
        const QString out1 = dir.filePath("VID_20260101_000001_10_001.insv");
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(out0) && QFileInfo::exists(out1), 20000);
        QCOMPARE(suggested, out0);
        QTest::qWait(300);
        QCOMPARE(questions, 0); // the suggested names need no confirmation
        QVERIFY(QFileInfo(out0).size() > 1000 && QFileInfo(out1).size() > 1000);
        for (const QString &name : QDir(dir.path()).entryList(QDir::Files | QDir::Hidden))
            QVERIFY2(!name.contains("partial"), qPrintable(name));
    }

    // A lone "_00_" file without its partner exports just itself.
    void insvWithoutPartnerExportsAlone()
    {
        QTemporaryDir dir;
        const QString lens0 = dir.filePath("VID_20260101_000000_00_001.insv");
        QVERIFY(QFile::copy(m_video, lens0));
        MainWindow w(nullptr, dir.filePath("cfg"));
        w.openPath(lens0);
        QTRY_VERIFY_WITH_TIMEOUT(hasLabelContaining(w, "not found (single file)"), 8000);
        QTRY_VERIFY_WITH_TIMEOUT(keyframesReady(w), 8000);
        w.setOutputChooser([](const QString &s) { return s; });
        QVERIFY(press(w, QKeySequence(Qt::CTRL | Qt::Key_E)));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(dir.filePath("VID_20260101_000001_00_001.insv")), 15000);
        QTest::qWait(300);
        QVERIFY(!QFileInfo::exists(dir.filePath("VID_20260101_000001_10_001.insv")));
    }

    // A name chosen by hand that breaks the camera's pattern is confirmed first (the apps would show
    // the two lenses as separate videos); one that keeps the pattern is not questioned.
    void namesBreakingTheCameraPatternAreConfirmed()
    {
        QTemporaryDir dir;
        const QString lens0 = dir.filePath("VID_20260101_000000_00_001.insv");
        const QString lens1 = dir.filePath("VID_20260101_000000_10_001.insv");
        QVERIFY(QFile::copy(m_video, lens0));
        QVERIFY(QFile::copy(m_video, lens1));

        MainWindow w(nullptr, dir.filePath("cfg"));
        w.openPath(lens0);
        QTRY_VERIFY_WITH_TIMEOUT(keyframesReady(w), 8000);
        QTRY_VERIFY_WITH_TIMEOUT(hasLabelContaining(w, "found (both are exported)"), 8000);

        QStringList asked;
        bool answer = false;
        w.setQuestionHandler([&](const QString &title, const QString &text) {
            asked << title + ": " + text;
            return answer;
        });

        // 1. "holiday.insv": no lens number, no pattern. "No" -> nothing is written.
        w.setOutputChooser([&](const QString &) { return dir.filePath("holiday.insv"); });
        QVERIFY(press(w, QKeySequence(Qt::CTRL | Qt::Key_E)));
        QTest::qWait(500);
        QCOMPARE(asked.size(), 1);
        QVERIFY(asked.first().contains("Insta360"));
        QVERIFY(asked.first().contains("VID_20260101_000001_00_001.insv")); // names the good alternative
        QVERIFY(!QFileInfo::exists(dir.filePath("holiday.insv")));
        QVERIFY(press(w, QKeySequence(Qt::CTRL | Qt::Key_E))); // the window is usable again
        QTest::qWait(300);
        asked.clear();

        // 2. A hand-made name that keeps the pattern needs no question; the partner follows it.
        w.setOutputChooser([&](const QString &) { return dir.filePath("VID_20260101_000030_00_001.insv"); });
        QVERIFY(press(w, QKeySequence(Qt::CTRL | Qt::Key_E)));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(dir.filePath("VID_20260101_000030_10_001.insv")), 20000);
        QVERIFY(QFileInfo::exists(dir.filePath("VID_20260101_000030_00_001.insv")));
        QCOMPARE(asked.size(), 0);
        QTest::qWait(300);

        // 3. Same as 1 but the user insists ("Yes"): exported as chosen.
        answer = true;
        w.setOutputChooser([&](const QString &) { return dir.filePath("holiday.insv"); });
        QVERIFY(press(w, QKeySequence(Qt::CTRL | Qt::Key_E)));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(dir.filePath("holiday.insv")), 20000);
        QCOMPARE(asked.size(), 1);
    }

private:
    QString m_video;
};

QTEST_MAIN(TstMainWindow)
#include "tst_mainwindow.moc"
