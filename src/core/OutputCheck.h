#pragma once

#include <QString>
#include <QStringList>
#include <QtGlobal>

// Pre-flight check of the destination before exporting: free space and file-system
// limits (FAT cannot hold a file of 4 GiB or more). Pure logic; the caller gathers
// the facts (QStorageInfo).
namespace OutputCheck {

struct Result
{
    QStringList problems; // certain to fail: the export must not start
    QStringList cautions; // probably fails: ask the user before going on
    bool ok() const { return problems.isEmpty(); }
    bool needsConfirmation() const { return !cautions.isEmpty(); }
};

// Rough size of the trimmed copy: the input size scaled by the kept fraction, plus a margin.
qint64 estimateBytes(qint64 inputBytes, qint64 keptMs, qint64 totalMs, qint64 extraBytes = 0);

// Free space as reported by the OS, or -1 when it cannot be trusted. Some file systems
// report nothing sensible (Haiku's read-only packagefs says "0 of 0 bytes").
qint64 usableFreeBytes(qint64 bytesAvailable, qint64 bytesTotal);

// `largestFileBytes`: the biggest single output; `totalBytes`: all outputs together.
// `freeBytes` < 0 means unknown (not checked).
Result evaluate(qint64 largestFileBytes, qint64 totalBytes, const QString &fileSystem, qint64 freeBytes);

QString formatBytes(qint64 bytes);

} // namespace OutputCheck
