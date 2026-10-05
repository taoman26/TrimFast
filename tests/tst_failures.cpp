// Failure paths and unusual environments, driven through the real MainWindow.
// Needs TRIMFAST_TEST_VIDEO (see scripts/make_test_media.sh) and ffmpeg/ffprobe; skipped otherwise.
// In every case: no crash, a clear message, nothing half-written, and the window recovers.
#include "app/MainWindow.h"
#include "core/SettingsManager.h"
#include "core/ToolLocator.h"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QtTest>
#include <unistd.h>

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

bool hasLabelContaining(MainWindow &w, const QString &part)
{
    for (const QLabel *l : w.findChildren<QLabel *>()) {
        if (l->text().contains(part))
            return true;
    }
    return false;
}

bool hasLabel(MainWindow &w, const QString &text)
{
    for (const QLabel *l : w.findChildren<QLabel *>()) {
        if (l->text() == text)
            return true;
    }
    return false;
}

bool keyframesReady(MainWindow &w)
{
    return hasLabelContaining(w, "Keyframes: ") && !hasLabelContaining(w, "scanning");
}

void writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}

QStringList files(const QString &dir)
{
    return QDir(dir).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
}

} // namespace

class TstFailures : public QObject
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

    void cleanup() { ToolLocator::setOverride("ffmpeg", QString()); ToolLocator::setOverride("ffprobe", QString()); }

    // Opens `bad`, expects an error and a disabled window, then a valid file must open fine.
    void expectOpenFailsThenRecovers(const QString &bad, const QString &expectedPart)
    {
        QTemporaryDir cfg;
        MainWindow w(nullptr, cfg.path());
        w.setOutputChooser([](const QString &s) { return s; });

        w.openPath(bad);
        QTRY_VERIFY_WITH_TIMEOUT(hasLabelContaining(w, "Error:"), 8000);
        QVERIFY2(hasLabelContaining(w, expectedPart), qPrintable(expectedPart));
        QCOMPARE(w.windowTitle(), QString("TrimFast"));
        QVERIFY(!press(w, QKeySequence(Qt::CTRL | Qt::Key_E))); // Export is disabled
        QVERIFY(!press(w, QKeySequence(Qt::Key_I)));            // so is Set IN

        // The window is not stuck: a good file opens afterwards.
        w.openPath(m_video);
        QTRY_VERIFY_WITH_TIMEOUT(keyframesReady(w), 8000);
        QVERIFY(w.windowTitle().startsWith("TrimFast - "));
        QVERIFY(press(w, QKeySequence(Qt::Key_I)));
    }

    void openingGarbageFails()
    {
        QTemporaryDir dir;
        const QString junk = dir.filePath("junk.mp4");
        writeFile(junk, "this is not a video, just text");
        expectOpenFailsThenRecovers(junk, "Error:");
    }

    void openingEmptyFileFails()
    {
        QTemporaryDir dir;
        const QString empty = dir.filePath("empty.mp4");
        writeFile(empty, QByteArray());
        expectOpenFailsThenRecovers(empty, "Error:");
    }

    void openingTruncatedVideoFails()
    {
        // An MP4 cut off before its index (moov) cannot be read.
        QFile src(m_video);
        QVERIFY(src.open(QIODevice::ReadOnly));
        const QByteArray head = src.read(src.size() / 3);
        QTemporaryDir dir;
        const QString cut = dir.filePath("truncated.mp4");
        writeFile(cut, head);
        expectOpenFailsThenRecovers(cut, "Error:");
    }

    void openingMissingFileFails()
    {
        QTemporaryDir dir;
        expectOpenFailsThenRecovers(dir.filePath("nope.mp4"), "not found");
    }

    void openingADirectoryFails()
    {
        QTemporaryDir dir;
        expectOpenFailsThenRecovers(dir.path(), "Error:");
    }

    void openingAnUnreadableFileFails()
    {
        if (geteuid() == 0)
            QSKIP("root can read anything");
        QTemporaryDir dir;
        const QString f = dir.filePath("locked.mp4");
        QVERIFY(QFile::copy(m_video, f));
        QVERIFY(QFile::setPermissions(f, QFileDevice::Permissions()));
        expectOpenFailsThenRecovers(f, "Error:");
        QFile::setPermissions(f, QFile::ReadOwner | QFile::WriteOwner);
    }

    void ffprobeMissing()
    {
        QTemporaryDir cfg;
        SettingsManager(cfg.filePath("settings.ini")).setFfprobePath(cfg.filePath("no-such-ffprobe"));
        MainWindow w(nullptr, cfg.path());
        w.openPath(m_video);
        QTRY_VERIFY_WITH_TIMEOUT(hasLabelContaining(w, "ffprobe was not found"), 5000);
        QVERIFY(!press(w, QKeySequence(Qt::CTRL | Qt::Key_E)));
    }

    void ffmpegMissingBlocksOnlyTheExport()
    {
        QTemporaryDir dir;
        const QString clip = dir.filePath("clip.mp4");
        QVERIFY(QFile::copy(m_video, clip));
        QTemporaryDir cfg;
        SettingsManager(cfg.filePath("settings.ini")).setFfmpegPath(cfg.filePath("no-such-ffmpeg"));

        MainWindow w(nullptr, cfg.path());
        w.setOutputChooser([](const QString &s) { return s; });
        w.openPath(clip);
        QTRY_VERIFY_WITH_TIMEOUT(keyframesReady(w), 8000); // viewing works: only ffmpeg is gone
        QVERIFY(press(w, QKeySequence(Qt::CTRL | Qt::Key_E)));
        QTest::qWait(800);
        QCOMPARE(files(dir.path()), QStringList{"clip.mp4"}); // nothing written
        QVERIFY(press(w, QKeySequence(Qt::CTRL | Qt::Key_E)));  // Export is usable again (not stuck)
    }

    void unwritableDestination()
    {
        if (geteuid() == 0)
            QSKIP("root ignores directory permissions");
        QTemporaryDir dir;
        const QString clip = dir.filePath("clip.mp4");
        QVERIFY(QFile::copy(m_video, clip));
        const QString ro = dir.filePath("readonly");
        QVERIFY(QDir().mkpath(ro));
        QVERIFY(QFile::setPermissions(ro, QFile::ReadOwner | QFile::ExeOwner));

        QTemporaryDir cfg;
        MainWindow w(nullptr, cfg.path());
        w.setOutputChooser([&](const QString &) { return ro + "/out.mp4"; });
        w.openPath(clip);
        QTRY_VERIFY_WITH_TIMEOUT(keyframesReady(w), 8000);
        QVERIFY(press(w, QKeySequence(Qt::CTRL | Qt::Key_E)));
        QTest::qWait(1500);
        QVERIFY(files(ro).isEmpty());
        QVERIFY(press(w, QKeySequence(Qt::CTRL | Qt::Key_E))); // recovered
        QTest::qWait(1000);
        QVERIFY(QFile::setPermissions(ro, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QCOMPARE(files(dir.path()), QStringList{"clip.mp4"});
    }

    void missingDestinationFolder()
    {
        QTemporaryDir dir;
        const QString clip = dir.filePath("clip.mp4");
        QVERIFY(QFile::copy(m_video, clip));
        QTemporaryDir cfg;
        MainWindow w(nullptr, cfg.path());
        w.setOutputChooser([&](const QString &) { return dir.filePath("no/such/folder/out.mp4"); });
        w.openPath(clip);
        QTRY_VERIFY_WITH_TIMEOUT(keyframesReady(w), 8000);
        QVERIFY(press(w, QKeySequence(Qt::CTRL | Qt::Key_E)));
        QTest::qWait(1500);
        QVERIFY(!QDir(dir.filePath("no")).exists()); // nothing was created
        QVERIFY(press(w, QKeySequence(Qt::CTRL | Qt::Key_E)));
    }

    void inputDisappearsAfterOpening()
    {
        QTemporaryDir dir;
        const QString clip = dir.filePath("clip.mp4");
        QVERIFY(QFile::copy(m_video, clip));
        QTemporaryDir cfg;
        MainWindow w(nullptr, cfg.path());
        w.setOutputChooser([](const QString &s) { return s; });
        w.openPath(clip);
        QTRY_VERIFY_WITH_TIMEOUT(keyframesReady(w), 8000);
        QVERIFY(QFile::remove(clip));
        QVERIFY(press(w, QKeySequence(Qt::CTRL | Qt::Key_E)));
        QTest::qWait(1000);
        QVERIFY(files(dir.path()).isEmpty()); // no output, no partial file
        QVERIFY(press(w, QKeySequence(Qt::CTRL | Qt::Key_E)));
    }

    // Spaces, brackets, quotes, non-ASCII and a leading dash in names and folders.
    void awkwardPathsWorkEndToEnd_data()
    {
        QTest::addColumn<QString>("folder");
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("expectedOutput");
        QTest::newRow("japanese") << "動画 テスト (1)" << "日本語 ファイル.mp4" << "日本語 ファイル_trim.mp4";
        QTest::newRow("spaces") << "my videos" << "a b  c.mp4" << "a b  c_trim.mp4";
        QTest::newRow("quotes") << "it's \"quoted\"" << "x'y\".mp4" << "x'y\"_trim.mp4";
        QTest::newRow("dash") << "-dash dir" << "-leading dash.mp4" << "-leading dash_trim.mp4";
        QTest::newRow("percent-and-dollar") << "100%$HOME" << "a%20b$x.mp4" << "a%20b$x_trim.mp4";
        QTest::newRow("emoji") << "🎬 clips" << "🎥.mp4" << "🎥_trim.mp4";
        QTest::newRow("colon") << "a:b" << "c:d.mp4" << "c:d_trim.mp4";
    }
    void awkwardPathsWorkEndToEnd()
    {
        QFETCH(QString, folder);
        QFETCH(QString, name);
        QFETCH(QString, expectedOutput);
        QTemporaryDir root;
        const QString dir = root.filePath(folder);
        QVERIFY(QDir().mkpath(dir));
        const QString clip = dir + "/" + name;
        QVERIFY(QFile::copy(m_video, clip));

        QTemporaryDir cfg;
        MainWindow w(nullptr, cfg.path());
        w.setOutputChooser([](const QString &s) { return s; });
        w.openPath(clip);
        QTRY_VERIFY_WITH_TIMEOUT(keyframesReady(w), 8000);
        QVERIFY(press(w, QKeySequence(Qt::CTRL | Qt::Key_E)));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(dir + "/" + expectedOutput), 15000);
        QTest::qWait(300);
        QCOMPARE(files(dir).size(), 2); // the clip and its trim, no leftovers
        QVERIFY(QFileInfo(dir + "/" + expectedOutput).size() > 1000);
    }

    // The session survives a path with odd characters, too.
    void sessionWithAwkwardPath()
    {
        QTemporaryDir root;
        const QString dir = root.filePath("動画 (1)");
        QVERIFY(QDir().mkpath(dir));
        const QString clip = dir + "/日本語.mp4";
        QVERIFY(QFile::copy(m_video, clip));
        QTemporaryDir cfg;
        {
            MainWindow w(nullptr, cfg.path());
            w.openPath(clip);
            QTRY_VERIFY_WITH_TIMEOUT(keyframesReady(w), 8000);
            QVERIFY(press(w, QKeySequence(Qt::SHIFT | Qt::Key_Right)));
            QVERIFY(press(w, QKeySequence(Qt::SHIFT | Qt::Key_Right)));
            QTRY_VERIFY_WITH_TIMEOUT(hasLabel(w, "00:00:02.000"), 3000);
            QVERIFY(press(w, QKeySequence(Qt::Key_I)));
        }
        MainWindow w(nullptr, cfg.path());
        w.openPath(clip);
        QTRY_VERIFY_WITH_TIMEOUT(keyframesReady(w), 8000);
        QCOMPARE(w.findChildren<QLineEdit *>().at(0)->text(), QString("00:00:02.000"));
    }

    void switchingFilesResetsTheRange()
    {
        QTemporaryDir cfg;
        MainWindow w(nullptr, cfg.path());
        w.openPath(m_video);
        QTRY_VERIFY_WITH_TIMEOUT(keyframesReady(w), 8000);
        QVERIFY(press(w, QKeySequence(Qt::SHIFT | Qt::Key_Right)));
        QTRY_VERIFY_WITH_TIMEOUT(hasLabel(w, "00:00:01.000"), 3000);
        QVERIFY(press(w, QKeySequence(Qt::Key_I)));
        QCOMPARE(w.findChildren<QLineEdit *>().at(0)->text(), QString("00:00:01.000"));

        // Open the same clip under another name: a fresh file starts with the whole range.
        QTemporaryDir dir;
        const QString other = dir.filePath("other.mp4");
        QVERIFY(QFile::copy(m_video, other));
        w.openPath(other);
        QTRY_VERIFY_WITH_TIMEOUT(keyframesReady(w), 8000);
        QCOMPARE(w.findChildren<QLineEdit *>().at(0)->text(), QString("00:00:00.000"));
    }

private:
    QString m_video;
};

QTEST_MAIN(TstFailures)
#include "tst_failures.moc"
