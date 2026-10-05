#include "core/OutputCheck.h"

#include <QLocale>
#include <QObject>
#include <algorithm>
#include <cmath>

namespace OutputCheck {

namespace {
constexpr qint64 kFourGiB = qint64(4) * 1024 * 1024 * 1024;

bool isFat(const QString &fs)
{
    const QString f = fs.toLower();
    // exFAT has no 4 GiB limit, plain FAT variants do.
    return f == QLatin1String("vfat") || f == QLatin1String("fat") || f == QLatin1String("fat16") ||
           f == QLatin1String("fat32") || f == QLatin1String("msdos") || f == QLatin1String("msdosfs");
}
} // namespace

QString formatBytes(qint64 bytes)
{
    return QLocale().formattedDataSize(bytes);
}

qint64 estimateBytes(qint64 inputBytes, qint64 keptMs, qint64 totalMs, qint64 extraBytes)
{
    if (inputBytes <= 0)
        return extraBytes;
    double fraction = 1.0;
    if (totalMs > 0)
        fraction = std::clamp(static_cast<double>(keptMs) / static_cast<double>(totalMs), 0.0, 1.0);
    // +3 % for container overhead and the keyframe the cut starts on.
    return static_cast<qint64>(std::ceil(static_cast<double>(inputBytes) * fraction * 1.03)) + extraBytes;
}

qint64 usableFreeBytes(qint64 bytesAvailable, qint64 bytesTotal)
{
    if (bytesTotal <= 0 || bytesAvailable < 0)
        return -1;
    return bytesAvailable;
}

Result evaluate(qint64 largestFileBytes, qint64 totalBytes, const QString &fileSystem, qint64 freeBytes)
{
    Result r;
    if (isFat(fileSystem) && largestFileBytes >= kFourGiB) {
        r.problems << QObject::tr("The destination is a %1 volume, which cannot hold a file of 4 GiB or more "
                                  "(this export needs about %2). Choose another folder.")
                          .arg(fileSystem.toUpper(), formatBytes(largestFileBytes));
    }
    if (freeBytes >= 0 && totalBytes > freeBytes) {
        r.cautions << QObject::tr("There may not be enough free space: about %1 needed, %2 reported available.")
                          .arg(formatBytes(totalBytes), formatBytes(freeBytes));
    }
    return r;
}

} // namespace OutputCheck
