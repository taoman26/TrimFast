#include "playback/VideoPlayer.h"

#include <QAudioOutput>
#include <QMediaPlayer>
#include <QUrl>
#include <QVideoSink>
#include <algorithm>
#include <cmath>

namespace {

// Audio is optional: it is not needed to choose cut points, and a failing audio
// backend must never take the application down. TRIMFAST_AUDIO=1/0 overrides.
bool audioEnabledByDefault()
{
    const QByteArray env = qgetenv("TRIMFAST_AUDIO");
    if (!env.isEmpty())
        return env != "0";
#ifdef __HAIKU__
    return false;
#else
    return true;
#endif
}

} // namespace

VideoPlayer::VideoPlayer(QObject *parent)
    : QObject(parent)
    , m_player(new QMediaPlayer(this))
    , m_sink(new QVideoSink(this))
{
    if (audioEnabledByDefault()) {
        m_audio = new QAudioOutput(this);
        m_player->setAudioOutput(m_audio);
    }
    m_player->setVideoSink(m_sink);

    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 ms) {
        // While paused after a manual seek we own the position (frame stepping).
        if (m_playing)
            setPosition(ms);
    });
    connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 ms) {
        if (ms > 0 && ms != m_duration) {
            m_duration = ms;
            emit loaded(ms);
        }
    });
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState st) {
        const bool playing = (st == QMediaPlayer::PlayingState);
        if (playing != m_playing) {
            m_playing = playing;
            emit playingChanged(playing);
        }
    });
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus st) {
        if (st == QMediaPlayer::EndOfMedia) {
            m_playing = false;
            emit playingChanged(false);
            setPosition(m_duration);
        }
    });
    connect(m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &msg) {
        emit errorOccurred(msg);
    });
}

VideoPlayer::~VideoPlayer() = default;

QVideoSink *VideoPlayer::videoSink() const
{
    return m_sink;
}

double VideoPlayer::frameMs() const
{
    return 1000.0 / m_fps;
}

void VideoPlayer::open(const QString &path, double fps)
{
    m_fps = fps > 0.0 ? fps : 25.0;
    m_position = 0;
    m_duration = 0;
    m_hasMedia = true;
    m_playing = false;
    m_player->setSource(QUrl::fromLocalFile(path));
    m_player->pause(); // show the first frame without starting playback
    emit positionChanged(0);
}

void VideoPlayer::close()
{
    m_player->stop();
    m_player->setSource(QUrl());
    m_hasMedia = false;
    m_position = 0;
    m_duration = 0;
}

void VideoPlayer::setPosition(qint64 ms)
{
    if (ms == m_position)
        return;
    m_position = ms;
    emit positionChanged(ms);
}

void VideoPlayer::play()
{
    if (!m_hasMedia)
        return;
    // Restart from the beginning when the end was reached.
    if (m_duration > 0 && m_position >= m_duration - static_cast<qint64>(frameMs()))
        seek(0);
    m_player->play();
}

void VideoPlayer::pause()
{
    if (!m_hasMedia)
        return;
    m_player->pause();
}

void VideoPlayer::togglePlay()
{
    if (m_playing)
        pause();
    else
        play();
}

void VideoPlayer::seek(qint64 ms)
{
    if (!m_hasMedia)
        return;
    const qint64 upper = m_duration > 0 ? m_duration : ms;
    ms = std::clamp<qint64>(ms, 0, std::max<qint64>(0, upper));
    m_player->setPosition(ms);
    setPosition(ms);
}

void VideoPlayer::seekRelative(qint64 deltaMs)
{
    seek(m_position + deltaMs);
}

void VideoPlayer::stepFrames(int frames)
{
    if (!m_hasMedia)
        return;
    if (m_playing)
        m_player->pause();
    const qint64 index = std::llround(static_cast<double>(m_position) / frameMs());
    const qint64 target = std::llround(static_cast<double>(std::max<qint64>(0, index + frames)) * frameMs());
    seek(target);
}

void VideoPlayer::goToStart()
{
    seek(0);
}

void VideoPlayer::goToEnd()
{
    // One frame before the end so that a frame is still shown.
    seek(std::max<qint64>(0, m_duration - static_cast<qint64>(std::ceil(frameMs()))));
}
