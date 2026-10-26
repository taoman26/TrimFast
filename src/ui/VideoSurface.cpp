#include "ui/VideoSurface.h"

#include "ui/Theme.h"

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
    const Theme::Colors &c = Theme::colors();
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(c.surfaceLow);
    p.setPen(QPen(c.border, 1));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);

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
        return;
    }

    p.setBrush(c.surface);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(frame, 6, 6);
    p.setPen(c.textDim);

    QFont big = font();
    big.setPointSizeF(big.pointSizeF() * 1.8);
    p.setFont(big);
    p.drawText(frame.adjusted(0, 0, 0, -font().pointSize() * 2), Qt::AlignCenter, m_title);
    if (!m_detail.isEmpty()) {
        p.setFont(font());
        p.drawText(frame.adjusted(0, font().pointSize() * 6, 0, 0), Qt::AlignHCenter | Qt::AlignVCenter,
                   m_detail);
    }
}
