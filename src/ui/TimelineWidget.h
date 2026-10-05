#pragma once

#include "core/KeyframeIndex.h"

#include <QWidget>

// Custom-painted timeline: ruler, track, keyframe ticks, IN/OUT range, playhead.
// Click or drag to seek; drag the IN/OUT markers to request a new range.
// It holds display copies only: the player owns the position and the
// MarkerManager owns (and validates) the range.
class TimelineWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TimelineWidget(QWidget *parent = nullptr);

    void setDuration(qint64 ms);
    void setPosition(qint64 ms);
    void setKeyframes(const KeyframeIndex &keyframes);
    // Selected range to draw; call with (0, 0) to hide it.
    void setRange(qint64 inMs, qint64 outMs);
    void clear(); // no media

    qint64 duration() const { return m_duration; }
    qint64 position() const { return m_position; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void seekRequested(qint64 ms);
    void inRequested(qint64 ms);  // IN marker dragged here
    void outRequested(qint64 ms); // OUT marker dragged here

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    double trackLeft() const;
    double trackRight() const;
    void seekToX(double x);
    enum class Drag { None, Seek, In, Out };
    Drag hitTest(double x) const;
    void dragTo(double x);
    qint64 msAt(double x) const;

    qint64 m_duration = 0;
    qint64 m_position = 0;
    KeyframeIndex m_keyframes;
    qint64 m_in = 0;
    qint64 m_out = 0;
    Drag m_drag = Drag::None;
};
