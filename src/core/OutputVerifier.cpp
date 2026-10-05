#include "core/OutputVerifier.h"

#include "core/ToolLocator.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QProcess>
#include <QSet>

namespace OutputVerifier {

namespace {

QString streamKey(const QJsonObject &s)
{
    const QString type = s.value(QLatin1String("codec_type")).toString();
    QString key = type + QLatin1Char(':') + s.value(QLatin1String("codec_name")).toString();
    if (type == QLatin1String("video")) {
        key += QStringLiteral(" %1x%2").arg(s.value(QLatin1String("width")).toInt())
                   .arg(s.value(QLatin1String("height")).toInt());
    }
    return key;
}

QMap<QString, int> countStreams(const QJsonArray &streams)
{
    QMap<QString, int> counts;
    for (const QJsonValue &v : streams)
        ++counts[streamKey(v.toObject())];
    return counts;
}

QSet<QString> sideDataTypes(const QJsonArray &streams)
{
    QSet<QString> types;
    for (const QJsonValue &v : streams) {
        const QJsonObject s = v.toObject();
        for (const QJsonValue &sd : s.value(QLatin1String("side_data_list")).toArray()) {
            types.insert(streamKey(s) + QStringLiteral(" / ") +
                         sd.toObject().value(QLatin1String("side_data_type")).toString());
        }
    }
    return types;
}

// Tags ffmpeg legitimately rewrites; they carry no user data.
bool isVolatileTag(const QString &key)
{
    static const QSet<QString> volatileTags = {
        QStringLiteral("major_brand"), QStringLiteral("minor_version"), QStringLiteral("compatible_brands"),
        QStringLiteral("encoder")};
    return volatileTags.contains(key.toLower());
}

QByteArray runProbe(const QString &path, QString *error)
{
    const QString tool = ToolLocator::find(QStringLiteral("ffprobe"));
    if (tool.isEmpty()) {
        if (error)
            *error = QObject::tr("ffprobe was not found");
        return {};
    }
    QProcess p;
    p.start(tool, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-show_format"),
                   QStringLiteral("-show_streams"), QStringLiteral("-show_chapters"), QStringLiteral("-of"),
                   QStringLiteral("json"), path});
    if (!p.waitForFinished(60000) || p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        if (error)
            *error = QString::fromLocal8Bit(p.readAllStandardError()).trimmed();
        return {};
    }
    return p.readAllStandardOutput();
}

} // namespace

QStringList compare(const QByteArray &inputJson, const QByteArray &outputJson)
{
    const QJsonObject in = QJsonDocument::fromJson(inputJson).object();
    const QJsonObject out = QJsonDocument::fromJson(outputJson).object();
    QStringList warnings;

    // Streams: every stream kind of the input must exist, in the same number.
    const QJsonArray inStreams = in.value(QLatin1String("streams")).toArray();
    const QJsonArray outStreams = out.value(QLatin1String("streams")).toArray();
    const auto inCounts = countStreams(inStreams);
    const auto outCounts = countStreams(outStreams);
    for (auto it = inCounts.begin(); it != inCounts.end(); ++it) {
        const int have = outCounts.value(it.key());
        if (have < it.value())
            warnings << QObject::tr("Stream missing in the output: %1 (input %2, output %3)")
                            .arg(it.key()).arg(it.value()).arg(have);
    }
    for (auto it = outCounts.begin(); it != outCounts.end(); ++it) {
        if (!inCounts.contains(it.key()))
            warnings << QObject::tr("Unexpected stream in the output: %1").arg(it.key());
    }

    // Chapters. (A trimmed file may legitimately have fewer: only complain when the
    // input had chapters and the output has none.)
    const int inChapters = in.value(QLatin1String("chapters")).toArray().size();
    const int outChapters = out.value(QLatin1String("chapters")).toArray().size();
    if (inChapters > 0 && outChapters == 0)
        warnings << QObject::tr("Chapters were not carried over (%1 in the input)").arg(inChapters);

    // Container tags.
    const QJsonObject inTags = in.value(QLatin1String("format")).toObject().value(QLatin1String("tags")).toObject();
    const QJsonObject outTags = out.value(QLatin1String("format")).toObject().value(QLatin1String("tags")).toObject();
    for (auto it = inTags.begin(); it != inTags.end(); ++it) {
        if (isVolatileTag(it.key()))
            continue;
        if (!outTags.contains(it.key()))
            warnings << QObject::tr("Metadata tag missing in the output: %1").arg(it.key());
        else if (outTags.value(it.key()) != it.value())
            warnings << QObject::tr("Metadata tag changed: %1").arg(it.key());
    }

    // Side data (spherical / projection / rotation / ...).
    const QSet<QString> inSide = sideDataTypes(inStreams);
    const QSet<QString> outSide = sideDataTypes(outStreams);
    for (const QString &t : inSide) {
        if (!outSide.contains(t))
            warnings << QObject::tr("Stream metadata missing in the output: %1").arg(t);
    }

    warnings.sort();
    return warnings;
}

Report verify(const QString &inputPath, const QString &outputPath)
{
    Report report;
    QString err;
    const QByteArray outJson = runProbe(outputPath, &err);
    if (outJson.isEmpty()) {
        report.readable = false;
        report.error = err.isEmpty() ? QObject::tr("The exported file cannot be read") : err;
        return report;
    }
    const QByteArray inJson = runProbe(inputPath, &err);
    if (inJson.isEmpty())
        return report; // cannot compare; the output itself is fine
    report.warnings = compare(inJson, outJson);
    return report;
}

} // namespace OutputVerifier
