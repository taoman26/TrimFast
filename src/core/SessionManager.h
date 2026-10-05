#pragma once

#include <QString>
#include <QtGlobal>
#include <optional>

// Remembers, per video file, the last IN/OUT range, plus the last file opened.
// Local INI file only; nothing leaves the machine.
class SessionManager
{
public:
    struct Entry
    {
        QString path;
        qint64 inMs = 0;
        qint64 outMs = 0;
        qint64 durationMs = 0; // of the file when saved, to detect a changed file
    };

    // `iniPath` is the storage file (tests pass a temporary one).
    explicit SessionManager(const QString &iniPath);

    void save(const Entry &entry);
    // The saved range for `path`, only if the file still has (about) the same
    // duration as when it was saved.
    std::optional<Entry> find(const QString &path, qint64 currentDurationMs) const;

    void setLastFile(const QString &path);
    QString lastFile() const;

    // Oldest entries beyond this many are dropped on save.
    static constexpr int kMaxEntries = 50;
    // A saved range is ignored if the duration differs by more than this.
    static constexpr qint64 kDurationToleranceMs = 500;

private:
    static QString keyFor(const QString &path);

    QString m_iniPath;
};
