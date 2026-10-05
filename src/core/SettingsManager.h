#pragma once

#include <QString>

// Small persistent user settings (local INI file; nothing leaves the machine).
// Empty values mean "use the default".
class SettingsManager
{
public:
    explicit SettingsManager(const QString &iniPath);

    // Explicit paths to ffmpeg / ffprobe; empty = search PATH.
    QString ffmpegPath() const;
    void setFfmpegPath(const QString &path);
    QString ffprobePath() const;
    void setFfprobePath(const QString &path);

    // Appended to the input's base name for the output file ("clip" -> "clip_trim").
    QString outputSuffix() const;
    void setOutputSuffix(const QString &suffix);

    static QString defaultOutputSuffix() { return QStringLiteral("_trim"); }

private:
    QString value(const QString &key) const;
    void setValue(const QString &key, const QString &value);

    QString m_iniPath;
};
