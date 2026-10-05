#include "core/MediaProbe.h"

#include "core/ToolLocator.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <cmath>

namespace {

// "30000/1001" -> 29.97..., "0/0" -> 0.
double parseRate(const QString &text)
{
    const QStringList parts = text.split(QLatin1Char('/'));
    if (parts.size() != 2)
        return 0.0;
    bool okN = false;
    bool okD = false;
    const double n = parts.at(0).toDouble(&okN);
    const double d = parts.at(1).toDouble(&okD);
    if (!okN || !okD || d <= 0.0 || n <= 0.0)
        return 0.0;
    return n / d;
}

} // namespace

MediaProbe::MediaProbe(QObject *parent)
    : QObject(parent)
{
}

MediaProbe::~MediaProbe()
{
    cancel();
}

void MediaProbe::cancel()
{
    ++m_generation;
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(1000);
        m_process->deleteLater();
        m_process = nullptr;
    }
}

std::optional<MediaInfo> MediaProbe::parse(const QByteArray &json, const QString &path, QString *error)
{
    auto fail = [error](const QString &msg) -> std::optional<MediaInfo> {
        if (error)
            *error = msg;
        return std::nullopt;
    };

    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject())
        return fail(QObject::tr("Unreadable ffprobe output"));
    const QJsonObject root = doc.object();

    MediaInfo info;
    info.path = path;

    const QJsonObject format = root.value(QLatin1String("format")).toObject();
    info.formatName = format.value(QLatin1String("format_name")).toString();
    info.sizeBytes = format.value(QLatin1String("size")).toString().toLongLong();
    const double durationSec = format.value(QLatin1String("duration")).toString().toDouble();
    info.durationMs = std::llround(durationSec * 1000.0);

    const QJsonArray streams = root.value(QLatin1String("streams")).toArray();
    for (const QJsonValue &v : streams) {
        const QJsonObject o = v.toObject();
        StreamInfo s;
        s.index = o.value(QLatin1String("index")).toInt(-1);
        s.type = o.value(QLatin1String("codec_type")).toString();
        s.codec = o.value(QLatin1String("codec_name")).toString();
        if (s.type == QLatin1String("video")) {
            s.width = o.value(QLatin1String("width")).toInt();
            s.height = o.value(QLatin1String("height")).toInt();
            s.fps = parseRate(o.value(QLatin1String("avg_frame_rate")).toString());
            if (s.fps <= 0.0)
                s.fps = parseRate(o.value(QLatin1String("r_frame_rate")).toString());
        } else if (s.type == QLatin1String("audio")) {
            s.sampleRate = o.value(QLatin1String("sample_rate")).toString().toInt();
            s.channels = o.value(QLatin1String("channels")).toInt();
        }
        info.streams.append(s);
    }
    info.chapterCount = root.value(QLatin1String("chapters")).toArray().size();

    if (info.streams.isEmpty())
        return fail(QObject::tr("No streams found"));
    if (!info.videoStream())
        return fail(QObject::tr("No video stream found"));
    return info;
}

void MediaProbe::probe(const QString &path)
{
    cancel();

    const QString tool = ToolLocator::find(QStringLiteral("ffprobe"));
    if (tool.isEmpty()) {
        emit failed(tr("ffprobe was not found. Please install FFmpeg."));
        return;
    }
    if (!QFileInfo::exists(path)) {
        emit failed(tr("File not found: %1").arg(path));
        return;
    }

    m_path = path;
    const quint64 generation = m_generation;
    m_process = new QProcess(this);
    connect(m_process, &QProcess::finished, this, [this, generation](int code, QProcess::ExitStatus status) {
        if (generation != m_generation || !m_process)
            return;
        const QByteArray out = m_process->readAllStandardOutput();
        const QByteArray err = m_process->readAllStandardError();
        m_process->deleteLater();
        m_process = nullptr;

        if (status != QProcess::NormalExit || code != 0) {
            const QString detail = QString::fromLocal8Bit(err).trimmed();
            emit failed(tr("ffprobe failed: %1").arg(detail.isEmpty() ? tr("unknown error") : detail));
            return;
        }
        QString reason;
        const auto info = parse(out, m_path, &reason);
        if (info)
            emit finished(*info);
        else
            emit failed(reason);
    });
    connect(m_process, &QProcess::errorOccurred, this, [this, generation](QProcess::ProcessError e) {
        if (generation != m_generation || !m_process || e != QProcess::FailedToStart)
            return;
        m_process->deleteLater();
        m_process = nullptr;
        emit failed(tr("Could not start ffprobe"));
    });

    m_process->start(tool, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-show_format"),
                            QStringLiteral("-show_streams"), QStringLiteral("-show_chapters"),
                            QStringLiteral("-of"), QStringLiteral("json"), path});
}
