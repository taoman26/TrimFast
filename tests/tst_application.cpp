// QFileOpenEvent (what Haiku's Tracker sends for "Open With" / double-click) opens the file.
// The window part needs TRIMFAST_TEST_VIDEO and ffprobe; the event part runs anywhere.
#include "app/MainWindow.h"
#include "app/TrimFastApplication.h"

#include <QFileOpenEvent>
#include <QLabel>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class TstApplication : public QObject
{
    Q_OBJECT

private slots:
    void requestBeforeAnyoneListensIsKept()
    {
        auto *app = static_cast<TrimFastApplication *>(QCoreApplication::instance());
        QVERIFY(app->takePendingFile().isEmpty());

        QFileOpenEvent ev(QStringLiteral("/videos/first.mp4"));
        QCoreApplication::sendEvent(app, &ev);
        QFileOpenEvent second(QStringLiteral("/videos/second.mp4"));
        QCoreApplication::sendEvent(app, &second);
        QCOMPARE(app->takePendingFile(), QString("/videos/second.mp4")); // the latest request wins
        QVERIFY(app->takePendingFile().isEmpty());                       // and it is handed out once
    }

    void requestWhileListeningIsEmitted()
    {
        auto *app = static_cast<TrimFastApplication *>(QCoreApplication::instance());
        QSignalSpy spy(app, &TrimFastApplication::fileOpenRequested);
        QFileOpenEvent ev(QStringLiteral("/videos/日本語 の.mp4"));
        QCoreApplication::sendEvent(app, &ev);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toString(), QString("/videos/日本語 の.mp4"));
        QVERIFY(app->takePendingFile().isEmpty()); // not kept when somebody was listening
    }

    void emptyRequestIsIgnored()
    {
        auto *app = static_cast<TrimFastApplication *>(QCoreApplication::instance());
        QSignalSpy spy(app, &TrimFastApplication::fileOpenRequested);
        QFileOpenEvent ev(QUrl("http://example.org/remote.mp4")); // not a local file: file() is empty
        QCoreApplication::sendEvent(app, &ev);
        QCOMPARE(spy.count(), 0);
    }

    void windowOpensTheRequestedFile()
    {
        const QString video = qEnvironmentVariable("TRIMFAST_TEST_VIDEO");
        if (video.isEmpty())
            QSKIP("TRIMFAST_TEST_VIDEO not set");
        auto *app = static_cast<TrimFastApplication *>(QCoreApplication::instance());
        qputenv("TRIMFAST_AUDIO", "0");
        QTemporaryDir cfg;
        MainWindow w(nullptr, cfg.path());
        QObject::connect(app, &TrimFastApplication::fileOpenRequested, &w, &MainWindow::openPath);

        // The system hands over a file (as Tracker does) and the window loads it.
        QFileOpenEvent ev(video);
        QCoreApplication::sendEvent(app, &ev);
        QTRY_VERIFY_WITH_TIMEOUT(w.windowTitle().startsWith("TrimFast - "), 8000);

        // A second request replaces the first (single-launch application).
        QTemporaryDir dir;
        const QString other = dir.filePath("other clip.mp4");
        QVERIFY(QFile::copy(video, other));
        QFileOpenEvent ev2(other);
        QCoreApplication::sendEvent(app, &ev2);
        QTRY_VERIFY_WITH_TIMEOUT(w.windowTitle() == "TrimFast - other clip.mp4", 8000);
        QObject::disconnect(app, nullptr, &w, nullptr);
    }
};

int main(int argc, char **argv)
{
    TrimFastApplication app(argc, argv);
    TstApplication test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_application.moc"
