#include "core/SettingsManager.h"

#include <QSettings>

SettingsManager::SettingsManager(const QString &iniPath)
    : m_iniPath(iniPath)
{
}

QString SettingsManager::value(const QString &key) const
{
    QSettings ini(m_iniPath, QSettings::IniFormat);
    return ini.value(key).toString();
}

void SettingsManager::setValue(const QString &key, const QString &value)
{
    QSettings ini(m_iniPath, QSettings::IniFormat);
    if (value.isEmpty())
        ini.remove(key);
    else
        ini.setValue(key, value);
    ini.sync();
}

QString SettingsManager::ffmpegPath() const
{
    return value(QStringLiteral("tools/ffmpeg"));
}

void SettingsManager::setFfmpegPath(const QString &path)
{
    setValue(QStringLiteral("tools/ffmpeg"), path);
}

QString SettingsManager::ffprobePath() const
{
    return value(QStringLiteral("tools/ffprobe"));
}

void SettingsManager::setFfprobePath(const QString &path)
{
    setValue(QStringLiteral("tools/ffprobe"), path);
}

QString SettingsManager::outputSuffix() const
{
    const QString s = value(QStringLiteral("output/suffix"));
    return s.isEmpty() ? defaultOutputSuffix() : s;
}

void SettingsManager::setOutputSuffix(const QString &suffix)
{
    setValue(QStringLiteral("output/suffix"), suffix);
}
