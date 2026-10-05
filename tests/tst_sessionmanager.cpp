#include "core/SessionManager.h"

#include <QTemporaryDir>
#include <QtTest>

class TstSessionManager : public QObject
{
    Q_OBJECT

private slots:
    void roundtrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        SessionManager s(dir.filePath("session.ini"));
        QVERIFY(!s.find("/videos/a.mp4", 60000).has_value());

        s.save({"/videos/a.mp4", 1000, 40000, 60000});
        const auto e = s.find("/videos/a.mp4", 60000);
        QVERIFY(e.has_value());
        QCOMPARE(e->inMs, qint64(1000));
        QCOMPARE(e->outMs, qint64(40000));
        QCOMPARE(e->durationMs, qint64(60000));

        // A new SessionManager on the same file sees it (persisted).
        SessionManager again(dir.filePath("session.ini"));
        QVERIFY(again.find("/videos/a.mp4", 60000).has_value());
        // Other files are independent.
        QVERIFY(!again.find("/videos/b.mp4", 60000).has_value());
    }

    void saveOverwrites()
    {
        QTemporaryDir dir;
        SessionManager s(dir.filePath("session.ini"));
        s.save({"/v/a.mp4", 100, 200, 1000});
        s.save({"/v/a.mp4", 300, 900, 1000});
        const auto e = s.find("/v/a.mp4", 1000);
        QVERIFY(e.has_value());
        QCOMPARE(e->inMs, qint64(300));
        QCOMPARE(e->outMs, qint64(900));
    }

    void changedFileIsIgnored()
    {
        QTemporaryDir dir;
        SessionManager s(dir.filePath("session.ini"));
        s.save({"/v/a.mp4", 1000, 40000, 60000});
        QVERIFY(s.find("/v/a.mp4", 60400).has_value());   // within tolerance
        QVERIFY(!s.find("/v/a.mp4", 61000).has_value());  // file differs now
        QVERIFY(!s.find("/v/a.mp4", 30000).has_value());
    }

    void corruptEntriesAreIgnored()
    {
        QTemporaryDir dir;
        SessionManager s(dir.filePath("session.ini"));
        s.save({"/v/a.mp4", 5000, 5000, 60000}); // IN == OUT
        QVERIFY(!s.find("/v/a.mp4", 60000).has_value());
        s.save({"/v/a.mp4", -5, 100, 60000});
        QVERIFY(!s.find("/v/a.mp4", 60000).has_value());
        s.save({"/v/a.mp4", 0, 90000, 60000}); // OUT beyond the file
        QVERIFY(!s.find("/v/a.mp4", 60000).has_value());
    }

    void lastFile()
    {
        QTemporaryDir dir;
        SessionManager s(dir.filePath("session.ini"));
        QVERIFY(s.lastFile().isEmpty());
        s.setLastFile("/v/a.mp4");
        QCOMPARE(s.lastFile(), QString("/v/a.mp4"));
        SessionManager again(dir.filePath("session.ini"));
        QCOMPARE(again.lastFile(), QString("/v/a.mp4"));
    }

    void oldestEntriesAreDropped()
    {
        QTemporaryDir dir;
        SessionManager s(dir.filePath("session.ini"));
        const int total = SessionManager::kMaxEntries + 5;
        for (int i = 0; i < total; ++i) {
            s.save({QString("/v/%1.mp4").arg(i), 0, 100, 1000});
            QTest::qWait(2); // distinct "updated" stamps
        }
        QVERIFY(!s.find("/v/0.mp4", 1000).has_value());
        QVERIFY(!s.find("/v/4.mp4", 1000).has_value());
        QVERIFY(s.find("/v/5.mp4", 1000).has_value());
        QVERIFY(s.find(QString("/v/%1.mp4").arg(total - 1), 1000).has_value());
    }
};

QTEST_MAIN(TstSessionManager)
#include "tst_sessionmanager.moc"
