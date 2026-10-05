#pragma once

#include "core/MediaInfo.h"

#include <QString>

// An Insta360 recording is two files, one per lens:
//   VID_20260923_105655_00_082.insv  (lens 0)  <->  VID_20260923_105655_10_082.insv  (lens 1)
// Opening either one pairs it with the other so that both are trimmed together.
namespace InsvPairResolver {

// Path of the other lens' file (only the name is derived; the file may not exist).
// Empty when `path` does not follow the "<prefix>_00_<n>.insv" / "_10_" naming.
QString partnerPath(const QString &path);

// True for the lens-0 file ("_00_"), false for lens 1.
bool isPrimary(const QString &path);

// True when the file name keeps the camera's own pattern,
// <prefix>_<yyyyMMdd>_<HHmmss>_<lens>_<serial>.insv (VID_20260923_105655_00_082.insv, LRV_..._11_100.insv).
// The Insta360 apps pair the two lens files of a recording by these names (checked with the real
// app: the same pair renamed "..._00_082_trim.insv" / "..._10_082_trim.insv" showed up as two videos).
bool followsCameraNaming(const QString &path);

// The same file name with the time of day moved forward by `seconds` (carrying into minutes, hours,
// the date, month and year), in the same folder. Empty when the name does not follow the camera
// pattern. This is how an exported copy gets a free name that still follows it.
QString shiftedCameraName(const QString &path, int seconds);

// Whether two probed files really are two halves of one recording: same size and
// frame rate, and durations within `toleranceMs` (the lenses can differ by a frame).
bool isConsistent(const MediaInfo &a, const MediaInfo &b, qint64 toleranceMs = 100);

// Maps the output name chosen for `input` to the matching name for `partnerInput`
// (swapping the lens token, e.g. "x_00_1_trim.insv" -> "x_10_1_trim.insv"); falls
// back to "<partner stem><suffix>.<ext>" next to the chosen output.
QString partnerOutputPath(const QString &chosenOutput, const QString &input, const QString &partnerInput,
                          const QString &suffix);

} // namespace InsvPairResolver
