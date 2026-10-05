#pragma once

#include <QImage>
#include <QWidget>

class QVideoFrame;

// Drawing surface for the video preview.
class VideoSurface : public QWidget
{
    Q_OBJECT

public:
    explicit VideoSurface(QWidget *parent = nullptr);

    // Placeholder text shown while no frame is available.
    void setCaption(const QString &title, const QString &detail);

    // Shows `frame` (converted to QImage). Invalid frames are ignored.
    void setFrame(const QVideoFrame &frame);
    void clearFrame();

    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_title;
    QString m_detail;
    QImage m_image;
};
