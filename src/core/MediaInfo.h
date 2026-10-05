#pragma once

#include <QList>
#include <QString>
#include <QtGlobal>

struct StreamInfo
{
    int index = -1;
    QString type;      // "video", "audio", "subtitle", "data", "attachment"
    QString codec;     // ffprobe codec_name, e.g. "h264"
    int width = 0;     // video only
    int height = 0;    // video only
    double fps = 0.0;  // video only (0 if unknown)
    int sampleRate = 0; // audio only
    int channels = 0;   // audio only
};

// Result of probing a media file with ffprobe. Plain value type.
struct MediaInfo
{
    QString path;
    QString formatName;
    qint64 sizeBytes = 0;
    qint64 durationMs = 0;
    int chapterCount = 0;
    QList<StreamInfo> streams;

    bool isValid() const { return !path.isEmpty() && !streams.isEmpty(); }

    // First video stream, or nullptr.
    const StreamInfo *videoStream() const
    {
        for (const StreamInfo &s : streams) {
            if (s.type == QLatin1String("video"))
                return &s;
        }
        return nullptr;
    }
    const StreamInfo *audioStream() const
    {
        for (const StreamInfo &s : streams) {
            if (s.type == QLatin1String("audio"))
                return &s;
        }
        return nullptr;
    }
    double fps() const
    {
        const StreamInfo *v = videoStream();
        return v ? v->fps : 0.0;
    }
};
