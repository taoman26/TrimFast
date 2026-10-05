#include "core/TimelineScale.h"

#include <QtTest>

class TstTimelineScale : public QObject
{
    Q_OBJECT

private slots:
    void mapping()
    {
        QCOMPARE(TimelineScale::msToX(0, 10000, 20, 120), 20.0);
        QCOMPARE(TimelineScale::msToX(10000, 10000, 20, 120), 120.0);
        QCOMPARE(TimelineScale::msToX(5000, 10000, 20, 120), 70.0);
        QCOMPARE(TimelineScale::msToX(5000, 0, 20, 120), 20.0); // no duration

        QCOMPARE(TimelineScale::xToMs(70, 10000, 20, 120), qint64(5000));
        QCOMPARE(TimelineScale::xToMs(-50, 10000, 20, 120), qint64(0));    // clamped
        QCOMPARE(TimelineScale::xToMs(999, 10000, 20, 120), qint64(10000)); // clamped
        QCOMPARE(TimelineScale::xToMs(70, 0, 20, 120), qint64(0));
        QCOMPARE(TimelineScale::xToMs(70, 10000, 50, 50), qint64(0)); // degenerate span
    }

    void roundtrip()
    {
        for (qint64 ms : {0LL, 1LL, 12345LL, 168120LL}) {
            const double x = TimelineScale::msToX(ms, 168120, 20, 1900);
            // One pixel is ~95 ms here, so allow that much error.
            QVERIFY(qAbs(TimelineScale::xToMs(x, 168120, 20, 1900) - ms) <= 1);
        }
    }

    void chooseStep_data()
    {
        QTest::addColumn<qint64>("duration");
        QTest::addColumn<double>("width");
        QTest::addColumn<qint64>("expected");
        // minMajorPx = 90 in all rows
        QTest::newRow("5min-1800px") << qint64(300000) << 1800.0 << qint64(15000);   // 15s = 90px
        QTest::newRow("168s-1880px") << qint64(168120) << 1880.0 << qint64(10000);   // 10s=111px
        QTest::newRow("1h-1800px") << qint64(3600000) << 1800.0 << qint64(300000);   // 5min = 150px; 2min=60 <90
        QTest::newRow("5s-1800px") << qint64(5000) << 1800.0 << qint64(500);         // 500ms = 180px; 200ms = 72
        QTest::newRow("tiny-width") << qint64(60000) << 50.0 << qint64(120000);      // needs >= 108 s for 90 px
        QTest::newRow("empty") << qint64(0) << 1800.0 << qint64(0);
        QTest::newRow("no-width") << qint64(1000) << 0.0 << qint64(0);
    }
    void chooseStep()
    {
        QFETCH(qint64, duration);
        QFETCH(double, width);
        QFETCH(qint64, expected);
        QCOMPARE(TimelineScale::chooseMajorStep(duration, width, 90.0), expected);
    }

    void minorDivisions()
    {
        QCOMPARE(TimelineScale::minorDivisions(10000), 5);
        QCOMPARE(TimelineScale::minorDivisions(15000), 3);
        QCOMPARE(TimelineScale::minorDivisions(900000), 3);
    }

    void labels()
    {
        QCOMPARE(TimelineScale::tickLabel(0, 10000), QString("0:00"));
        QCOMPARE(TimelineScale::tickLabel(75000, 15000), QString("1:15"));
        QCOMPARE(TimelineScale::tickLabel(3600000, 300000), QString("1:00:00"));
        QCOMPARE(TimelineScale::tickLabel(3725000, 5000), QString("1:02:05"));
        QCOMPARE(TimelineScale::tickLabel(1500, 500), QString("0:01.5"));
        QCOMPARE(TimelineScale::tickLabel(-10, 1000), QString("0:00"));
    }
};

QTEST_APPLESS_MAIN(TstTimelineScale)
#include "tst_timelinescale.moc"
