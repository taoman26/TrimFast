#include "core/InsvPairResolver.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTimeZone>
#include <cmath>

namespace InsvPairResolver {

namespace {

// "<anything>_<00|10>_<digits>" (the base name without extension)
const QRegularExpression &stemPattern()
{
    static const QRegularExpression re(QStringLiteral("^(.*_)(00|10)(_\\d+)$"));
    return re;
}

QString swapLens(const QString &lens)
{
    return lens == QLatin1String("00") ? QStringLiteral("10") : QStringLiteral("00");
}

} // namespace

bool followsCameraNaming(const QString &path)
{
    const QFileInfo fi(path);
    if (fi.suffix().compare(QLatin1String("insv"), Qt::CaseInsensitive) != 0)
        return false;
    static const QRegularExpression re(QStringLiteral("^[A-Za-z]+(?:_[A-Za-z]+)*_\\d{8}_\\d{6}_\\d{2}_\\d+$"));
    return re.match(fi.completeBaseName()).hasMatch();
}

QString shiftedCameraName(const QString &path, int seconds)
{
    if (!followsCameraNaming(path))
        return QString();
    const QFileInfo fi(path);
    static const QRegularExpression re(QStringLiteral("^(.*_)(\\d{8})_(\\d{6})(_\\d{2}_\\d+)$"));
    const auto m = re.match(fi.completeBaseName());
    if (!m.hasMatch())
        return QString();
    // Date and time are read separately and shifted as UTC: the time in the name is the camera's wall
    // clock, so the local time zone (and its daylight-saving gaps) must play no part.
    const QDate date = QDate::fromString(m.captured(2), QStringLiteral("yyyyMMdd"));
    const QTime time = QTime::fromString(m.captured(3), QStringLiteral("HHmmss"));
    if (!date.isValid() || !time.isValid())
        return QString();
    const QDateTime moved = QDateTime(date, time, QTimeZone::UTC).addSecs(seconds);
    const QString name = m.captured(1) + moved.toString(QStringLiteral("yyyyMMdd_HHmmss")) + m.captured(4) +
                         QLatin1Char('.') + fi.suffix();
    return fi.absoluteDir().filePath(name);
}

QString partnerPath(const QString &path)
{
    const QFileInfo fi(path);
    if (fi.suffix().compare(QLatin1String("insv"), Qt::CaseInsensitive) != 0)
        return QString();
    const auto m = stemPattern().match(fi.completeBaseName());
    if (!m.hasMatch())
        return QString();
    const QString name = m.captured(1) + swapLens(m.captured(2)) + m.captured(3) + QLatin1Char('.') + fi.suffix();
    return fi.absoluteDir().filePath(name);
}

bool isPrimary(const QString &path)
{
    const auto m = stemPattern().match(QFileInfo(path).completeBaseName());
    return m.hasMatch() && m.captured(2) == QLatin1String("00");
}

bool isConsistent(const MediaInfo &a, const MediaInfo &b, qint64 toleranceMs)
{
    const StreamInfo *va = a.videoStream();
    const StreamInfo *vb = b.videoStream();
    if (!va || !vb)
        return false;
    if (va->width != vb->width || va->height != vb->height)
        return false;
    if (std::abs(va->fps - vb->fps) > 0.01)
        return false;
    return qAbs(a.durationMs - b.durationMs) <= toleranceMs;
}

QString partnerOutputPath(const QString &chosenOutput, const QString &input, const QString &partnerInput,
                          const QString &suffix)
{
    const QFileInfo out(chosenOutput);
    const QFileInfo in(input);
    const QFileInfo partner(partnerInput);

    // Same lens token in the chosen name as in the input name -> swap it.
    const auto inMatch = stemPattern().match(in.completeBaseName());
    if (inMatch.hasMatch()) {
        const QString token = QLatin1Char('_') + inMatch.captured(2) + QLatin1Char('_');
        const QString swapped = QLatin1Char('_') + swapLens(inMatch.captured(2)) + QLatin1Char('_');
        const QString base = out.completeBaseName();
        const qsizetype at = base.indexOf(token);
        if (at >= 0) {
            QString name = base;
            name.replace(at, token.size(), swapped);
            const QString ext = out.suffix();
            return out.absoluteDir().filePath(ext.isEmpty() ? name : name + QLatin1Char('.') + ext);
        }
    }
    QString name = partner.completeBaseName() + suffix;
    if (!out.suffix().isEmpty())
        name += QLatin1Char('.') + out.suffix();
    return out.absoluteDir().filePath(name);
}

} // namespace InsvPairResolver
