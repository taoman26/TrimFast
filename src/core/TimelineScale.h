#pragma once

#include <QString>
#include <QtGlobal>

// Pure geometry/ruler helpers for TimelineWidget (no Qt Widgets; unit-tested).
namespace TimelineScale {

// Linear mapping of [0, durationMs] onto the pixel span [x0, x1].
double msToX(qint64 ms, qint64 durationMs, double x0, double x1);
// Inverse of msToX, rounded and clamped to [0, durationMs].
qint64 xToMs(double x, qint64 durationMs, double x0, double x1);

// Smallest "nice" major tick step (ms) whose on-screen spacing is at least
// `minMajorPx`. Returns 0 for an empty/degenerate scale.
qint64 chooseMajorStep(qint64 durationMs, double widthPx, double minMajorPx);

// Number of minor subdivisions between two major ticks for `majorStepMs`.
int minorDivisions(qint64 majorStepMs);

// Ruler label: "m:ss" / "h:mm:ss", with tenths ("m:ss.t") for sub-second steps.
QString tickLabel(qint64 ms, qint64 majorStepMs);

} // namespace TimelineScale
