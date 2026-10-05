#include "ui/VideoSurface.h"

#include <QPainter>
#include <QVideoFrame>

VideoSurface::VideoSurface(QWidget *parent)
    : QWidget(parent)
    , m_title(tr("Video Preview Area"))
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAutoFillBackground(false);
}

void VideoSurface::setCaption(const QString &title, const QString &detail)
{
    m_title = title;
    m_detail = detail;
    update();
}

void VideoSurface::setFrame(const QVideoFrame &frame)
{
    if (!frame.isValid())
        return;
    const QImage image = frame.toImage();
    if (image.isNull())
        return;
    m_image = image;
    update();
}

void VideoSurface::clearFrame()
{
    m_image = QImage();
    update();
}

QSize VideoSurface::minimumSizeHint() const
{
    return {320, 180};
}

void VideoSurface::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(245, 245, 245));
    p.setPen(QColor(150, 150, 150));
    p.drawRect(rect().adjusted(0, 0, -1, -1));

    // Frame centered in the surface, keeping the aspect ratio (16:9 placeholder when empty).
    const QSize source = m_image.isNull() ? QSize(16, 9) : m_image.size();
    QRect frame = rect().adjusted(20, 20, -20, -20);
    int w = frame.width();
    int h = static_cast<int>(static_cast<qint64>(w) * source.height() / source.width());
    if (h > frame.height()) {
        h = frame.height();
        w = static_cast<int>(static_cast<qint64>(h) * source.width() / source.height());
    }
    frame = QRect(0, 0, w, h);
    frame.moveCenter(rect().center());

    if (!m_image.isNull()) {
        p.drawImage(frame, m_image);
        p.setPen(QColor(90, 90, 90));
        p.drawRect(frame.adjusted(-1, -1, 0, 0));
        return;
    }

    p.fillRect(frame, QColor(232, 232, 232));
    p.setPen(QColor(90, 90, 90));
    p.drawRect(frame.adjusted(0, 0, -1, -1));

    QFont big = font();
    big.setBold(true);
    big.setPointSizeF(big.pointSizeF() * 1.8);
    p.setFont(big);
    p.drawText(frame.adjusted(0, 0, 0, -font().pointSize() * 2), Qt::AlignCenter, m_title);
    if (!m_detail.isEmpty()) {
        p.setFont(font());
        p.drawText(frame.adjusted(0, font().pointSize() * 6, 0, 0), Qt::AlignHCenter | Qt::AlignVCenter,
                   m_detail);
    }
}
