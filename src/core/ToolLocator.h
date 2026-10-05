#pragma once

#include <QString>

namespace ToolLocator {

// Finds an external tool ("ffmpeg", "ffprobe"): the user's override first (if it
// points to an executable file), then PATH. Returns an empty string if the tool
// is not available.
QString find(const QString &name);

// Sets (or, with an empty path, clears) the override for `name`. Process-wide;
// the application feeds it from SettingsManager at startup.
void setOverride(const QString &name, const QString &path);

} // namespace ToolLocator
