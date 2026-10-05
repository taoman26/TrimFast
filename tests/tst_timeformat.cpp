#include "core/TimeFormat.h"

#include <QtTest>

class TstTimeFormat : public QObject
{
    Q_OBJECT

private slots:
    void format_data()
    {
        QTest::addColumn<qint64>("ms");
        QTest::addColumn<QString>("expected");
        QTest::newRow("zero") << qint64(0) << "00:00:00.000";
        QTest::newRow("millis") << qint64(7) << "00:00:00.007";
        QTest::newRow("seconds") << qint64(42000) << "00:00:42.000";
        QTest::newRow("mixed") << qint64(72480) << "00:01:12.480";
        QTest::newRow("hour") << qint64(3600000) << "01:00:00.000";
        QTest::newRow("over99h") << qint64(100LL * 3600000 + 1) << "100:00:00.001";
        QTest::newRow("negative") << qint64(-5) << "00:00:00.000";
    }
    void format()
    {
        QFETCH(qint64, ms);
        QFETCH(QString, expected);
        QCOMPARE(TimeFormat::format(ms), expected);
    }

    void parse_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<bool>("valid");
        QTest::addColumn<qint64>("ms");
        QTest::newRow("full") << "00:03:35.500" << true << qint64(215500);
        QTest::newRow("no-hours") << "03:35.500" << true << qint64(215500);
        QTest::newRow("secs") << "42" << true << qint64(42000);
        QTest::newRow("secs-frac") << "1.5" << true << qint64(1500);
        QTest::newRow("frac-2") << "0:00:01.25" << true << qint64(1250);
        QTest::newRow("spaces") << "  00:00:01.000 " << true << qint64(1000);
        QTest::newRow("long-hours") << "100:00:00.001" << true << qint64(360000001);
        QTest::newRow("empty") << "" << false << qint64(0);
        QTest::newRow("letters") << "ab:cd" << false << qint64(0);
        QTest::newRow("sec-60") << "00:00:60.000" << false << qint64(0);
        QTest::newRow("min-60") << "00:60:00.000" << false << qint64(0);
        QTest::newRow("frac-4") << "00:00:01.1234" << false << qint64(0);
        QTest::newRow("empty-frac") << "00:00:01." << false << qint64(0);
        QTest::newRow("too-many") << "1:2:3:4" << false << qint64(0);
        QTest::newRow("negative") << "-1" << false << qint64(0);
    }
    void parse()
    {
        QFETCH(QString, text);
        QFETCH(bool, valid);
        QFETCH(qint64, ms);
        const auto r = TimeFormat::parse(text);
        QCOMPARE(r.has_value(), valid);
        if (valid)
            QCOMPARE(*r, ms);
    }

    void roundtrip()
    {
        for (qint64 ms : {0LL, 1LL, 999LL, 61001LL, 3599999LL, 86400123LL}) {
            const auto r = TimeFormat::parse(TimeFormat::format(ms));
            QVERIFY(r.has_value());
            QCOMPARE(*r, ms);
        }
    }
};

QTEST_APPLESS_MAIN(TstTimeFormat)
#include "tst_timeformat.moc"
