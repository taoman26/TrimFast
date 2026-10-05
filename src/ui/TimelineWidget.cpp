#include "ui/TimelineWidget.h"

#include "core/TimelineScale.h"

#include <QMouseEvent>
#include <QPainter>
#include <algorithm>

namespace {
constexpr int kMargin = 20;
constexpr int kRulerTop = 6;
constexpr int kTrackTop = 52;
constexpr int kTrackHeight = 28;
constexpr double kMinMajorPx = 90.0;
const QColor kLine(150, 150, 150);
const QColor kDark(90, 90, 90);
const QColor kPlayhead(200, 40, 40);
const QColor kBlue(51, 102, 152);
const QColor kBlueFill(150, 190, 235);
constexpr double kGrabPx = 9.0; // marker hit radius
} // namespace

TimelineWidget::TimelineWidget(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true); // hover cursor over the markers
    setAccessibleName(tr("Timeline"));
    setAccessibleDescription(tr("Click or drag to seek"));
}

QSize TimelineWidget::sizeHint() const
{
    return {800, 120};
}

QSize TimelineWidget::minimumSizeHint() const
{
    return {320, 120};
}

double TimelineWidget::trackLeft() const
{
    return kMargin;
}

double TimelineWidget::trackRight() const
{
    return width() - kMargin;
}

void TimelineWidget::setDuration(qint64 ms)
{
    ms = std::max<qint64>(0, ms);
    if (ms == m_duration)
        return;
    m_duration = ms;
    update();
}

void TimelineWidget::setPosition(qint64 ms)
{
    ms = std::clamp<qint64>(ms, 0, m_duration);
    if (ms == m_position)
        return;
    m_position = ms;
    update();
}

void TimelineWidget::setKeyframes(const KeyframeIndex &keyframes)
{
    m_keyframes = keyframes;
    update();
}

void TimelineWidget::setRange(qint64 inMs, qint64 outMs)
{
    if (inMs == m_in && outMs == m_out)
        return;
    m_in = inMs;
    m_out = outMs;
    update();
}

void TimelineWidget::clear()
{
    m_duration = 0;
    m_position = 0;
    m_in = 0;
    m_out = 0;
    m_keyframes = KeyframeIndex();
    m_drag = Drag::None;
    unsetCursor();
    update();
}

void TimelineWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::white);
    p.setPen(kLine);
    p.drawRect(rect().adjusted(0, 0, -1, -1));

    const double x0 = trackLeft();
    const double x1 = trackRight();
    if (x1 <= x0)
        return;

    // Ruler.
    p.setPen(kDark);
    if (m_duration > 0) {
        const qint64 major = TimelineScale::chooseMajorStep(m_duration, x1 - x0, kMinMajorPx);
        const int divisions = TimelineScale::minorDivisions(major);
        const qint64 minor = std::max<qint64>(1, major / divisions);

        QFont small = font();
        small.setPointSizeF(std::max(7.0, small.pointSizeF() * 0.85));
        p.setFont(small);

        for (qint64 t = 0; t <= m_duration; t += minor) {
            const int x = qRound(TimelineScale::msToX(t, m_duration, x0, x1));
            if (t % major == 0) {
                p.drawLine(x, kRulerTop, x, kRulerTop + 16);
                // Label sits under the ticks; skip it when it would run off the edge.
                const QString label = TimelineScale::tickLabel(t, major);
                const int labelWidth = p.fontMetrics().horizontalAdvance(label);
                if (x + 3 + labelWidth <= width() - 2)
                    p.drawText(x + 3, kRulerTop + 16, labelWidth + 2, 16, Qt::AlignLeft | Qt::AlignVCenter, label);
            } else {
                p.drawLine(x, kRulerTop, x, kRulerTop + 8);
            }
        }
        p.setFont(font());
    } else {
        // No media: evenly spaced empty ruler.
        const int ticks = 50;
        for (int i = 0; i <= ticks; ++i) {
            const int x = qRound(x0 + (x1 - x0) * i / ticks);
            p.drawLine(x, kRulerTop, x, kRulerTop + ((i % 10 == 0) ? 14 : 8));
        }
    }

    // Track.
    const QRectF track(x0, kTrackTop, x1 - x0, kTrackHeight);
    p.fillRect(track, QColor(235, 235, 235));
    p.setPen(kLine);
    p.drawRect(track.adjusted(0, 0, -1, -1));

    // Selected range: highlighted part of the track.
    const bool hasRange = m_duration > 0 && m_out > m_in;
    double xIn = 0;
    double xOut = 0;
    if (hasRange) {
        xIn = TimelineScale::msToX(m_in, m_duration, x0, x1);
        xOut = TimelineScale::msToX(m_out, m_duration, x0, x1);
        const QRectF sel(xIn, kTrackTop, xOut - xIn, kTrackHeight);
        p.fillRect(sel, kBlueFill);
        p.setPen(kBlue);
        p.drawRect(sel.adjusted(0, 0, -1, -1));
    }

    // Keyframe ticks (skipped when they would merge into a solid block).
    if (m_duration > 0 && !m_keyframes.isEmpty() && m_keyframes.size() * 3 <= static_cast<qsizetype>(x1 - x0)) {
        p.setPen(QColor(170, 170, 170));
        for (qint64 t : m_keyframes.times()) {
            if (t > m_duration)
                break;
            const int x = qRound(TimelineScale::msToX(t, m_duration, x0, x1));
            p.drawLine(x, kTrackTop + kTrackHeight - 7, x, kTrackTop + kTrackHeight - 2);
        }
    }

    // IN / OUT markers: a line through the track with a flag below it.
    if (hasRange) {
        QFont bold = font();
        bold.setBold(true);
        bold.setPointSizeF(std::max(7.0, bold.pointSizeF() * 0.85));
        p.setFont(bold);
        const int base = kTrackTop + kTrackHeight + 16;
        auto drawMarker = [&](double x, bool isIn) {
            const int ix = qRound(x);
            p.setPen(QPen(kBlue, 3));
            p.drawLine(ix, kTrackTop - 6, ix, base);
            p.setPen(Qt::NoPen);
            p.setBrush(kBlue);
            const int dir = isIn ? 1 : -1; // flag points inward
            const QPoint flag[3] = {{ix, base}, {ix + dir * 12, base}, {ix, base - 12}};
            p.drawPolygon(flag, 3);
            p.setPen(kBlue);
            // The label sits outside the range; at the very edge of the widget it would be cut
            // off, so it moves to the inside (beyond the flag).
            const QString label = isIn ? tr("IN") : tr("OUT");
            constexpr int kLabelWidth = 26;
            bool outside = true;
            QRect box;
            if (isIn) {
                box = QRect(ix - 4 - kLabelWidth, base - 14, kLabelWidth, 14);
                if (box.left() < 2)
                    outside = false;
            } else {
                box = QRect(ix + 4, base - 14, kLabelWidth, 14);
                if (box.right() > width() - 2)
                    outside = false;
            }
            if (!outside)
                box.moveLeft(isIn ? ix + 16 : ix - 16 - kLabelWidth);
            const bool alignLeft = isIn ? !outside : outside;
            p.drawText(box, (alignLeft ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignVCenter, label);
        };
        drawMarker(xIn, true);
        drawMarker(xOut, false);
        p.setFont(font());
        p.setBrush(Qt::NoBrush);
    }

    // Playhead.
    if (m_duration > 0) {
        const int x = qRound(TimelineScale::msToX(m_position, m_duration, x0, x1));
        p.setPen(QPen(kPlayhead, 2));
        p.drawLine(x, kTrackTop - 6, x, kTrackTop + kTrackHeight + 14);
        p.setPen(Qt::NoPen);
        p.setBrush(kPlayhead);
        const QPoint tri[3] = {{x - 7, kTrackTop - 14}, {x + 7, kTrackTop - 14}, {x, kTrackTop - 2}};
        p.drawPolygon(tri, 3);
    }
}

qint64 TimelineWidget::msAt(double x) const
{
    return TimelineScale::xToMs(x, m_duration, trackLeft(), trackRight());
}

void TimelineWidget::seekToX(double x)
{
    const qint64 ms = msAt(x);
    setPosition(ms); // immediate feedback; the player confirms afterwards
    emit seekRequested(ms);
}

TimelineWidget::Drag TimelineWidget::hitTest(double x) const
{
    if (m_duration <= 0 || m_out <= m_in)
        return Drag::Seek;
    const double dIn = qAbs(x - TimelineScale::msToX(m_in, m_duration, trackLeft(), trackRight()));
    const double dOut = qAbs(x - TimelineScale::msToX(m_out, m_duration, trackLeft(), trackRight()));
    if (dIn > kGrabPx && dOut > kGrabPx)
        return Drag::Seek;
    return dIn <= dOut ? Drag::In : Drag::Out;
}

void TimelineWidget::dragTo(double x)
{
    switch (m_drag) {
    case Drag::Seek:
        seekToX(x);
        break;
    case Drag::In:
        emit inRequested(msAt(x));
        break;
    case Drag::Out:
        emit outRequested(msAt(x));
        break;
    case Drag::None:
        break;
    }
}

void TimelineWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || m_duration <= 0) {
        QWidget::mousePressEvent(event);
        return;
    }
    m_drag = hitTest(event->position().x());
    dragTo(event->position().x());
}

void TimelineWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_drag != Drag::None) {
        dragTo(event->position().x());
        return;
    }
    if (m_duration > 0 && hitTest(event->position().x()) != Drag::Seek)
        setCursor(Qt::SizeHorCursor);
    else
        unsetCursor();
    QWidget::mouseMoveEvent(event);
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_drag != Drag::None) {
        m_drag = Drag::None;
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void TimelineWidget::leaveEvent(QEvent *event)
{
    unsetCursor();
    QWidget::leaveEvent(event);
}
