#include "core/KeyframeIndex.h"

#include <QtTest>

class TstKeyframeIndex : public QObject
{
    Q_OBJECT

private slots:
    void parseKeepsOnlyKeyframes()
    {
        const QByteArray csv =
            "0.000000,K__\n0.040000,___\n0.080000,___\n0.640000,K__\n0.680000,_D_\n1.280000,K_C\n";
        const KeyframeIndex idx = KeyframeIndex::parse(csv);
        QCOMPARE(idx.times(), (QList<qint64>{0, 640, 1280}));
    }

    void parseToleratesJunk()
    {
        const QByteArray csv = "N/A,K__\n,K__\nabc,K__\n-1.0,K__\n2.5,K__\nnoseparator\n\r\n3.0,K__\r\n";
        const KeyframeIndex idx = KeyframeIndex::parse(csv);
        QCOMPARE(idx.times(), (QList<qint64>{2500, 3000}));
        QVERIFY(KeyframeIndex::parse("").isEmpty());
    }

    void constructorSortsAndDeduplicates()
    {
        const KeyframeIndex idx(QList<qint64>{3000, 0, 1000, 1000, 2000});
        QCOMPARE(idx.times(), (QList<qint64>{0, 1000, 2000, 3000}));
        QCOMPARE(idx.size(), qsizetype(4));
    }

    void neighbours_data()
    {
        QTest::addColumn<qint64>("ms");
        QTest::addColumn<qint64>("prev"); // -1 = none
        QTest::addColumn<qint64>("next");
        QTest::addColumn<qint64>("floor");
        QTest::addColumn<qint64>("ceil");
        // keyframes: 0, 1000, 2000
        QTest::newRow("before-first") << qint64(-5) << qint64(-1) << qint64(0) << qint64(-1) << qint64(0);
        QTest::newRow("on-first") << qint64(0) << qint64(-1) << qint64(1000) << qint64(0) << qint64(0);
        QTest::newRow("between") << qint64(500) << qint64(0) << qint64(1000) << qint64(0) << qint64(1000);
        QTest::newRow("on-middle") << qint64(1000) << qint64(0) << qint64(2000) << qint64(1000) << qint64(1000);
        QTest::newRow("just-after") << qint64(1001) << qint64(1000) << qint64(2000) << qint64(1000) << qint64(2000);
        QTest::newRow("on-last") << qint64(2000) << qint64(1000) << qint64(-1) << qint64(2000) << qint64(2000);
        QTest::newRow("after-last") << qint64(9000) << qint64(2000) << qint64(-1) << qint64(2000) << qint64(-1);
    }
    void neighbours()
    {
        QFETCH(qint64, ms);
        QFETCH(qint64, prev);
        QFETCH(qint64, next);
        QFETCH(qint64, floor);
        QFETCH(qint64, ceil);
        const KeyframeIndex idx(QList<qint64>{0, 1000, 2000});
        auto val = [](const std::optional<qint64> &v) { return v ? *v : qint64(-1); };
        QCOMPARE(val(idx.previous(ms)), prev);
        QCOMPARE(val(idx.next(ms)), next);
        QCOMPARE(val(idx.floor(ms)), floor);
        QCOMPARE(val(idx.ceil(ms)), ceil);
    }

    void emptyIndex()
    {
        const KeyframeIndex idx;
        QVERIFY(idx.isEmpty());
        QVERIFY(!idx.previous(5).has_value());
        QVERIFY(!idx.next(5).has_value());
        QVERIFY(!idx.floor(5).has_value());
        QVERIFY(!idx.ceil(5).has_value());
    }
};

QTEST_APPLESS_MAIN(TstKeyframeIndex)
#include "tst_keyframeindex.moc"
