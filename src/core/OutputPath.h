#pragma once

#include <QString>

// Naming of the exported file.
namespace OutputPath {

// "<dir>/<name><suffix>.<ext>" next to the input, e.g. /v/clip.mp4 -> /v/clip_trim.mp4.
// Always absolute.
QString suggest(const QString &inputPath, const QString &suffix);

// Like suggest(), but never an existing file: "clip_trim.mp4", "clip_trim_2.mp4", ...
// Insta360 files (VID_20260923_105655_00_082.insv ...) are the exception: they are NOT given a
// suffix, because the Insta360 apps pair the two lens files by names that follow the camera's
// pattern. The copy gets the same name with the time moved forward by 1, 2, ... seconds until it
// is free (VID_20260923_105656_00_082.insv).
// With `partnerInput` (a paired Insta360 file) the number is chosen so that the
// partner's output name is free as well. `maxTries` bounds the search.
QString suggestUnique(const QString &inputPath, const QString &suffix, const QString &partnerInput = QString(),
                      int maxTries = 999);

// Container to force with `-f` when the file extension does not name one
// ffmpeg knows. Insta360 .insv / .lrv are MP4 files; empty = let ffmpeg decide.
QString forcedFormat(const QString &outputPath);

} // namespace OutputPath
