#pragma once

#include <QObject>
#include <QString>

class QAudioOutput;
class QMediaPlayer;
class QVideoSink;

// Preview playback built on QMediaPlayer (FFmpeg backend on Linux and Haiku).
// Owns the playback position model used for frame stepping. Preview only:
// export never goes through this class.
class VideoPlayer : public QObject
{
    Q_OBJECT

public:
    explicit VideoPlayer(QObject *parent = nullptr);
    ~VideoPlayer() override;

    // Frames are delivered here; connect to QVideoSink::videoFrameChanged.
    QVideoSink *videoSink() const;

    // `fps` is the video frame rate from ffprobe (0 = unknown, 25 is assumed).
    void open(const QString &path, double fps);
    void close();

    bool hasMedia() const { return m_hasMedia; }
    bool isPlaying() const { return m_playing; }
    qint64 position() const { return m_position; }
    qint64 duration() const { return m_duration; }

public slots:
    void play();
    void pause();
    void togglePlay();
    void seek(qint64 ms);               // clamped to [0, duration]
    void seekRelative(qint64 deltaMs);
    void stepFrames(int frames);        // pauses; +1 = next frame, -1 = previous
    void goToStart();
    void goToEnd();

signals:
    void loaded(qint64 durationMs);
    void positionChanged(qint64 ms);
    void playingChanged(bool playing);
    void errorOccurred(const QString &message);

private:
    double frameMs() const;
    void setPosition(qint64 ms);

    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audio = nullptr;
    QVideoSink *m_sink = nullptr;
    double m_fps = 25.0;
    qint64 m_position = 0;
    qint64 m_duration = 0;
    bool m_playing = false;
    bool m_hasMedia = false;
};
