#pragma once

#include <QString>
#include <QtGlobal>
#include <optional>

namespace TimeFormat {

// Milliseconds -> "HH:MM:SS.mmm". Negative values are clamped to 0.
// Hours are at least two digits and may grow beyond 99.
QString format(qint64 ms);

// "HH:MM:SS.mmm", "MM:SS.mmm", "SS.mmm" or "SS" -> milliseconds.
// The fraction may have 1-3 digits. Returns nullopt on malformed input.
std::optional<qint64> parse(const QString &text);

} // namespace TimeFormat
