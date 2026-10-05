#include "core/TimelineScale.h"

#include <algorithm>
#include <cmath>

namespace TimelineScale {

double msToX(qint64 ms, qint64 durationMs, double x0, double x1)
{
    if (durationMs <= 0)
        return x0;
    return x0 + (x1 - x0) * static_cast<double>(ms) / static_cast<double>(durationMs);
}

qint64 xToMs(double x, qint64 durationMs, double x0, double x1)
{
    if (durationMs <= 0 || x1 <= x0)
        return 0;
    const double ratio = (x - x0) / (x1 - x0);
    const qint64 ms = std::llround(ratio * static_cast<double>(durationMs));
    return std::clamp<qint64>(ms, 0, durationMs);
}

qint64 chooseMajorStep(qint64 durationMs, double widthPx, double minMajorPx)
{
    if (durationMs <= 0 || widthPx <= 0.0)
        return 0;
    static const qint64 kSteps[] = {100,    200,    500,     1000,    2000,    5000,    10000,
                                    15000,  30000,  60000,   120000,  300000,  600000,  900000,
                                    1800000, 3600000, 7200000, 14400000, 21600000, 43200000, 86400000};
    const double pxPerMs = widthPx / static_cast<double>(durationMs);
    for (qint64 step : kSteps) {
        if (static_cast<double>(step) * pxPerMs >= minMajorPx)
            return step;
    }
    return kSteps[std::size(kSteps) - 1];
}

int minorDivisions(qint64 majorStepMs)
{
    // 1:00 -> 12 x 5 s feels natural; everything else splits into 5 or 3.
    switch (majorStepMs) {
    case 15000:
    case 900000:
        return 3;
    default:
        return 5;
    }
}

QString tickLabel(qint64 ms, qint64 majorStepMs)
{
    ms = std::max<qint64>(0, ms);
    const qint64 totalSec = ms / 1000;
    const qint64 h = totalSec / 3600;
    const qint64 m = (totalSec / 60) % 60;
    const qint64 s = totalSec % 60;
    QString text;
    if (h > 0) {
        text = QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
    } else {
        text = QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
    }
    if (majorStepMs % 1000 != 0)
        text += QStringLiteral(".%1").arg((ms % 1000) / 100);
    return text;
}

} // namespace TimelineScale
