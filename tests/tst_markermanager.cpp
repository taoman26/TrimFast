#include "core/MarkerManager.h"

#include <QSignalSpy>
#include <QtTest>

namespace {
KeyframeIndex kf() { return KeyframeIndex(QList<qint64>{0, 1000, 2000, 3000, 4000}); }
}

class TstMarkerManager : public QObject
{
    Q_OBJECT

private slots:
    void noMediaRejectsEverything()
    {
        MarkerManager m;
        QVERIFY(!m.hasMedia());
        QCOMPARE(m.setIn(5).status, MarkerManager::Result::NoMedia);
        QCOMPARE(m.setOut(5).status, MarkerManager::Result::NoMedia);
        QCOMPARE(m.setRange(0, 5).status, MarkerManager::Result::NoMedia);
    }

    void resetCoversWholeFile()
    {
        MarkerManager m;
        QSignalSpy spy(&m, &MarkerManager::markersChanged);
        m.reset(5000);
        QCOMPARE(m.in(), qint64(0));
        QCOMPARE(m.out(), qint64(5000));
        QCOMPARE(m.length(), qint64(5000));
        QVERIFY(m.isFullRange());
        QCOMPARE(spy.count(), 1);
        m.reset(0);
        QVERIFY(!m.hasMedia());
    }

    void inSnapsDownToKeyframe()
    {
        MarkerManager m;
        m.reset(5000);
        m.setKeyframes(kf());
        const auto r = m.setIn(2500);
        QVERIFY(r.ok());
        QVERIFY(r.snapped());
        QCOMPARE(r.requested, qint64(2500));
        QCOMPARE(r.applied, qint64(2000));
        QCOMPARE(m.in(), qint64(2000));

        const auto exact = m.setIn(3000);
        QVERIFY(exact.ok());
        QVERIFY(!exact.snapped());
        QCOMPARE(m.in(), qint64(3000));
    }

    void inWithoutKeyframesIsTakenAsGiven()
    {
        MarkerManager m;
        m.reset(5000);
        const auto r = m.setIn(2500);
        QVERIFY(r.ok());
        QVERIFY(!r.snapped());
        QCOMPARE(m.in(), qint64(2500));
    }

    void keyframesArrivingLaterResnapIn()
    {
        MarkerManager m;
        m.reset(5000);
        m.setIn(2500);
        QSignalSpy spy(&m, &MarkerManager::markersChanged);
        m.setKeyframes(kf());
        QCOMPARE(m.in(), qint64(2000));
        QCOMPARE(spy.count(), 1);
        // Already on a keyframe: no change, no signal.
        m.setKeyframes(kf());
        QCOMPARE(spy.count(), 1);
    }

    void outIsNotSnapped()
    {
        MarkerManager m;
        m.reset(5000);
        m.setKeyframes(kf());
        const auto r = m.setOut(3500);
        QVERIFY(r.ok());
        QVERIFY(!r.snapped());
        QCOMPARE(m.out(), qint64(3500));
    }

    void inMustStayBeforeOut()
    {
        MarkerManager m;
        m.reset(5000);
        m.setKeyframes(kf());
        m.setOut(2500);
        QSignalSpy spy(&m, &MarkerManager::markersChanged);

        // Requested IN is after OUT even though its snapped keyframe (2000) is before it.
        auto r = m.setIn(2600);
        QCOMPARE(r.status, MarkerManager::Result::InvalidRange);
        QCOMPARE(m.in(), qint64(0));
        QCOMPARE(r.applied, qint64(0));
        r = m.setIn(2500); // equal is also invalid
        QCOMPARE(r.status, MarkerManager::Result::InvalidRange);
        QCOMPARE(spy.count(), 0);

        QVERIFY(m.setIn(2400).ok()); // snaps to 2000, still before OUT
        QCOMPARE(m.in(), qint64(2000));
    }

    void outMustStayAfterIn()
    {
        MarkerManager m;
        m.reset(5000);
        m.setIn(3000);
        QCOMPARE(m.setOut(3000).status, MarkerManager::Result::InvalidRange);
        QCOMPARE(m.setOut(1000).status, MarkerManager::Result::InvalidRange);
        QCOMPARE(m.out(), qint64(5000));
        QVERIFY(m.setOut(3001).ok());
    }

    void valuesAreClampedToTheFile()
    {
        MarkerManager m;
        m.reset(5000);
        QVERIFY(m.setOut(99999).ok());
        QCOMPARE(m.out(), qint64(5000));
        QVERIFY(m.setIn(-50).ok());
        QCOMPARE(m.in(), qint64(0));
    }

    void clearRestoresWholeFile()
    {
        MarkerManager m;
        m.reset(5000);
        m.setIn(1000);
        m.setOut(3000);
        QVERIFY(!m.isFullRange());
        m.clearRange();
        QVERIFY(m.isFullRange());
        QCOMPARE(m.out(), qint64(5000));
    }

    void setRangeRestoresWithoutSnapping()
    {
        MarkerManager m;
        m.reset(5000);
        m.setKeyframes(kf());
        QVERIFY(m.setRange(1500, 4200).ok());
        QCOMPARE(m.in(), qint64(1500)); // as saved, not re-snapped
        QCOMPARE(m.out(), qint64(4200));
        QCOMPARE(m.setRange(3000, 3000).status, MarkerManager::Result::InvalidRange);
        QCOMPARE(m.in(), qint64(1500));
        QVERIFY(m.setRange(-5, 99999).ok()); // clamped
        QVERIFY(m.isFullRange());
    }

    void durationChange()
    {
        MarkerManager m;
        m.reset(5000);
        m.setDuration(5040); // full range follows the player's duration
        QVERIFY(m.isFullRange());
        QCOMPARE(m.out(), qint64(5040));

        m.setIn(1000);
        m.setOut(4000);
        m.setDuration(3000); // shrinks below OUT: clamped
        QCOMPARE(m.out(), qint64(3000));
        QCOMPARE(m.in(), qint64(1000));

        m.setDuration(500); // below IN: IN pulled back to keep IN < OUT
        QCOMPARE(m.out(), qint64(500));
        QVERIFY(m.in() < m.out());
    }

    void signalOnlyOnChange()
    {
        MarkerManager m;
        m.reset(5000);
        QSignalSpy spy(&m, &MarkerManager::markersChanged);
        m.setOut(5000); // unchanged
        QCOMPARE(spy.count(), 0);
        m.setOut(4000);
        QCOMPARE(spy.count(), 1);
        m.clearRange();
        QCOMPARE(spy.count(), 2);
        m.clearRange(); // already full
        QCOMPARE(spy.count(), 2);
    }
};

QTEST_APPLESS_MAIN(TstMarkerManager)
#include "tst_markermanager.moc"
