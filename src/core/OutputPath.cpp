#include "core/OutputPath.h"

#include "core/InsvPairResolver.h"

#include <QDir>
#include <QFileInfo>

namespace OutputPath {

QString suggest(const QString &inputPath, const QString &suffix)
{
    const QFileInfo fi(inputPath);
    QString name = fi.completeBaseName() + suffix;
    const QString ext = fi.suffix();
    if (!ext.isEmpty())
        name += QLatin1Char('.') + ext;
    return fi.absoluteDir().filePath(name);
}

QString suggestUnique(const QString &inputPath, const QString &suffix, const QString &partnerInput, int maxTries)
{
    // Camera naming: keep the pattern, move the time forward until the name (and the partner's) is free.
    if (!InsvPairResolver::shiftedCameraName(inputPath, 1).isEmpty()) {
        for (int n = 1; n <= maxTries; ++n) {
            const QString candidate = InsvPairResolver::shiftedCameraName(inputPath, n);
            if (candidate.isEmpty() || QFileInfo::exists(candidate))
                continue;
            if (!partnerInput.isEmpty()) {
                const QString partner = InsvPairResolver::shiftedCameraName(partnerInput, n);
                if (!partner.isEmpty() && QFileInfo::exists(partner))
                    continue;
            }
            return candidate;
        }
    }

    auto named = [](const QString &input, const QString &suffixWithNumber) {
        return suggest(input, suffixWithNumber);
    };
    for (int n = 1; n <= maxTries; ++n) {
        const QString tag = n == 1 ? suffix : QStringLiteral("%1_%2").arg(suffix).arg(n);
        const QString candidate = named(inputPath, tag);
        if (QFileInfo::exists(candidate))
            continue;
        if (!partnerInput.isEmpty() && QFileInfo::exists(named(partnerInput, tag)))
            continue;
        return candidate;
    }
    return suggest(inputPath, suffix); // give up: the caller's overwrite checks still protect files
}

QString forcedFormat(const QString &outputPath)
{
    const QString ext = QFileInfo(outputPath).suffix().toLower();
    if (ext == QLatin1String("insv") || ext == QLatin1String("lrv"))
        return QStringLiteral("mp4");
    return QString();
}

} // namespace OutputPath
