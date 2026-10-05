#include "core/SessionManager.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <algorithm>

SessionManager::SessionManager(const QString &iniPath)
    : m_iniPath(iniPath)
{
}

QString SessionManager::keyFor(const QString &path)
{
    const QString canonical = QFileInfo(path).absoluteFilePath();
    return QString::fromLatin1(QCryptographicHash::hash(canonical.toUtf8(), QCryptographicHash::Sha1).toHex());
}

void SessionManager::save(const Entry &entry)
{
    QSettings ini(m_iniPath, QSettings::IniFormat);
    ini.beginGroup(QStringLiteral("files"));
    ini.beginGroup(keyFor(entry.path));
    ini.setValue(QStringLiteral("path"), QFileInfo(entry.path).absoluteFilePath());
    ini.setValue(QStringLiteral("in"), entry.inMs);
    ini.setValue(QStringLiteral("out"), entry.outMs);
    ini.setValue(QStringLiteral("duration"), entry.durationMs);
    ini.setValue(QStringLiteral("updated"), QDateTime::currentMSecsSinceEpoch());
    ini.endGroup();

    // Drop the oldest entries beyond the limit.
    const QStringList keys = ini.childGroups();
    if (keys.size() > kMaxEntries) {
        QList<std::pair<qint64, QString>> byAge;
        for (const QString &k : keys)
            byAge.append({ini.value(k + QStringLiteral("/updated")).toLongLong(), k});
        std::sort(byAge.begin(), byAge.end());
        for (qsizetype i = 0; i < byAge.size() - kMaxEntries; ++i)
            ini.remove(byAge.at(i).second);
    }
    ini.endGroup();
    ini.sync();
}

std::optional<SessionManager::Entry> SessionManager::find(const QString &path, qint64 currentDurationMs) const
{
    QSettings ini(m_iniPath, QSettings::IniFormat);
    ini.beginGroup(QStringLiteral("files/") + keyFor(path));
    if (!ini.contains(QStringLiteral("in")))
        return std::nullopt;

    Entry e;
    e.path = QFileInfo(path).absoluteFilePath();
    e.inMs = ini.value(QStringLiteral("in")).toLongLong();
    e.outMs = ini.value(QStringLiteral("out")).toLongLong();
    e.durationMs = ini.value(QStringLiteral("duration")).toLongLong();

    if (qAbs(e.durationMs - currentDurationMs) > kDurationToleranceMs)
        return std::nullopt; // the file changed since
    if (e.inMs < 0 || e.inMs >= e.outMs || e.outMs > currentDurationMs + kDurationToleranceMs)
        return std::nullopt;
    return e;
}

void SessionManager::setLastFile(const QString &path)
{
    QSettings ini(m_iniPath, QSettings::IniFormat);
    ini.setValue(QStringLiteral("session/last_file"), QFileInfo(path).absoluteFilePath());
    ini.sync();
}

QString SessionManager::lastFile() const
{
    QSettings ini(m_iniPath, QSettings::IniFormat);
    return ini.value(QStringLiteral("session/last_file")).toString();
}
