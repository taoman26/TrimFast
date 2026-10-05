#include "ui/TimelineWidget.h"

#include <QSignalSpy>
#include <QtTest>

class TstTimelineWidget : public QObject
{
    Q_OBJECT

private slots:
    void noMediaIgnoresMouse()
    {
        TimelineWidget w;
        w.resize(1000, 120);
        QSignalSpy spy(&w, &TimelineWidget::seekRequested);
        QTest::mouseClick(&w, Qt::LeftButton, Qt::NoModifier, QPoint(500, 60));
        QCOMPARE(spy.count(), 0);
    }

    void clickSeeks()
    {
        TimelineWidget w;
        w.resize(1040, 120); // track spans x = 20 .. 1020
        w.setDuration(100000);
        QSignalSpy spy(&w, &TimelineWidget::seekRequested);

        QTest::mouseClick(&w, Qt::LeftButton, Qt::NoModifier, QPoint(520, 60)); // middle
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.last().at(0).toLongLong(), qint64(50000));
        QCOMPARE(w.position(), qint64(50000)); // immediate local feedback

        QTest::mouseClick(&w, Qt::LeftButton, Qt::NoModifier, QPoint(0, 60)); // left of track: clamped
        QCOMPARE(spy.last().at(0).toLongLong(), qint64(0));
        QTest::mouseClick(&w, Qt::LeftButton, Qt::NoModifier, QPoint(1039, 60)); // right of track
        QCOMPARE(spy.last().at(0).toLongLong(), qint64(100000));
    }

    void dragSeeksContinuously()
    {
        TimelineWidget w;
        w.resize(1040, 120);
        w.setDuration(100000);
        QSignalSpy spy(&w, &TimelineWidget::seekRequested);

        QTest::mousePress(&w, Qt::LeftButton, Qt::NoModifier, QPoint(20, 60));
        QCOMPARE(spy.count(), 1);
        // QTest::mouseMove does not carry the pressed button, so deliver the event ourselves.
        for (int x : {270, 520, 1020}) {
            QMouseEvent move(QEvent::MouseMove, QPointF(x, 60), QPointF(x, 60), Qt::NoButton, Qt::LeftButton,
                             Qt::NoModifier);
            QCoreApplication::sendEvent(&w, &move);
        }
        QCOMPARE(spy.count(), 4);
        QCOMPARE(spy.at(1).at(0).toLongLong(), qint64(25000));
        QCOMPARE(spy.at(3).at(0).toLongLong(), qint64(100000));
        QTest::mouseRelease(&w, Qt::LeftButton, Qt::NoModifier, QPoint(1020, 60));

        // After release, moving no longer seeks.
        QMouseEvent move(QEvent::MouseMove, QPointF(100, 60), QPointF(100, 60), Qt::NoButton, Qt::NoButton,
                         Qt::NoModifier);
        QCoreApplication::sendEvent(&w, &move);
        QCOMPARE(spy.count(), 4);
    }

    void markersAreDraggable()
    {
        TimelineWidget w;
        w.resize(1040, 120); // track x = 20..1020; 100 s => 10 px per second
        w.setDuration(100000);
        w.setRange(20000, 80000); // IN at x=220, OUT at x=820
        QSignalSpy seek(&w, &TimelineWidget::seekRequested);
        QSignalSpy in(&w, &TimelineWidget::inRequested);
        QSignalSpy out(&w, &TimelineWidget::outRequested);

        // Pressing near IN grabs IN (no seek).
        QTest::mousePress(&w, Qt::LeftButton, Qt::NoModifier, QPoint(223, 60));
        QCOMPARE(in.count(), 1);
        QCOMPARE(seek.count(), 0);
        QMouseEvent move(QEvent::MouseMove, QPointF(420, 60), QPointF(420, 60), Qt::NoButton, Qt::LeftButton,
                         Qt::NoModifier);
        QCoreApplication::sendEvent(&w, &move);
        QCOMPARE(in.count(), 2);
        QCOMPARE(in.last().at(0).toLongLong(), qint64(40000));
        QTest::mouseRelease(&w, Qt::LeftButton, Qt::NoModifier, QPoint(420, 60));

        // Near OUT grabs OUT.
        QTest::mouseClick(&w, Qt::LeftButton, Qt::NoModifier, QPoint(815, 60));
        QCOMPARE(out.count(), 1);
        QCOMPARE(out.last().at(0).toLongLong(), qint64(79500));
        QCOMPARE(seek.count(), 0);

        // Away from both markers it seeks.
        QTest::mouseClick(&w, Qt::LeftButton, Qt::NoModifier, QPoint(520, 60));
        QCOMPARE(seek.count(), 1);
        QCOMPARE(seek.last().at(0).toLongLong(), qint64(50000));
        QCOMPARE(in.count(), 2);
        QCOMPARE(out.count(), 1);
    }

    void hoverShowsResizeCursorOnMarkers()
    {
        TimelineWidget w;
        w.resize(1040, 120);
        w.setDuration(100000);
        w.setRange(20000, 80000);
        QMouseEvent onMarker(QEvent::MouseMove, QPointF(222, 60), QPointF(222, 60), Qt::NoButton, Qt::NoButton,
                             Qt::NoModifier);
        QCoreApplication::sendEvent(&w, &onMarker);
        QCOMPARE(w.cursor().shape(), Qt::SizeHorCursor);
        QMouseEvent away(QEvent::MouseMove, QPointF(520, 60), QPointF(520, 60), Qt::NoButton, Qt::NoButton,
                         Qt::NoModifier);
        QCoreApplication::sendEvent(&w, &away);
        QVERIFY(w.cursor().shape() != Qt::SizeHorCursor);
    }

    void rightButtonDoesNothing()
    {
        TimelineWidget w;
        w.resize(1040, 120);
        w.setDuration(100000);
        QSignalSpy spy(&w, &TimelineWidget::seekRequested);
        QTest::mouseClick(&w, Qt::RightButton, Qt::NoModifier, QPoint(520, 60));
        QCOMPARE(spy.count(), 0);
    }

    void setPositionClampsAndClearResets()
    {
        TimelineWidget w;
        w.setDuration(5000);
        w.setPosition(9000);
        QCOMPARE(w.position(), qint64(5000));
        w.setPosition(-3);
        QCOMPARE(w.position(), qint64(0));
        w.setKeyframes(KeyframeIndex(QList<qint64>{0, 1000}));
        w.clear();
        QCOMPARE(w.duration(), qint64(0));
        QCOMPARE(w.position(), qint64(0));
    }

    void renderSnapshot()
    {
        // Optional visual check: TRIMFAST_SAVE_PNG=<dir> writes timeline_*.png
        const QString dir = qEnvironmentVariable("TRIMFAST_SAVE_PNG");
        if (dir.isEmpty())
            QSKIP("TRIMFAST_SAVE_PNG not set");
        QList<qint64> kf;
        for (qint64 t = 0; t <= 168000; t += 640)
            kf.append(t);
        struct Case { const char *name; qint64 duration; qint64 pos; KeyframeIndex kf; };
        const QList<Case> cases = {{"empty", 0, 0, {}},
                                   {"168s", 168120, 72480, KeyframeIndex(kf)},
                                   {"5s", 5000, 1500, {}},
                                   {"2h", 7200000, 3600000, {}},
                                   {"full", 6000, 0, {}}};
        for (const Case &c : cases) {
            TimelineWidget w;
            w.resize(1860, 120);
            w.setDuration(c.duration);
            w.setPosition(c.pos);
            if (c.duration > 0)
                w.setRange(c.duration / 4, c.duration * 3 / 4);
            if (QByteArray(c.name) == "full")
                w.setRange(0, c.duration); // markers at both edges: the labels must stay visible
            w.setKeyframes(c.kf);
            QVERIFY(w.grab().save(QStringLiteral("%1/timeline_%2.png").arg(dir, QLatin1String(c.name))));
        }
    }
};

QTEST_MAIN(TstTimelineWidget)
#include "tst_timelinewidget.moc"
