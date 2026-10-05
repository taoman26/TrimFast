#include "core/SettingsManager.h"
#include "core/ToolLocator.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class TstSettingsManager : public QObject
{
    Q_OBJECT

private slots:
    void defaults()
    {
        QTemporaryDir dir;
        SettingsManager s(dir.filePath("settings.ini"));
        QVERIFY(s.ffmpegPath().isEmpty());
        QVERIFY(s.ffprobePath().isEmpty());
        QCOMPARE(s.outputSuffix(), QString("_trim"));
    }

    void persistsAndClears()
    {
        QTemporaryDir dir;
        const QString ini = dir.filePath("settings.ini");
        {
            SettingsManager s(ini);
            s.setFfmpegPath("/opt/ffmpeg/bin/ffmpeg");
            s.setFfprobePath("/opt/ffmpeg/bin/ffprobe");
            s.setOutputSuffix("_cut");
        }
        SettingsManager again(ini);
        QCOMPARE(again.ffmpegPath(), QString("/opt/ffmpeg/bin/ffmpeg"));
        QCOMPARE(again.ffprobePath(), QString("/opt/ffmpeg/bin/ffprobe"));
        QCOMPARE(again.outputSuffix(), QString("_cut"));

        again.setFfmpegPath(QString()); // empty = back to default
        again.setOutputSuffix(QString());
        QVERIFY(again.ffmpegPath().isEmpty());
        QCOMPARE(again.outputSuffix(), QString("_trim"));
    }

    void toolLocatorOverride()
    {
        // No override: PATH search (whatever it finds, or nothing).
        ToolLocator::setOverride("faketool", QString());
        QVERIFY(ToolLocator::find("faketool-that-does-not-exist").isEmpty());

        QTemporaryDir dir;
        const QString good = dir.filePath("mytool");
        QFile f(good);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("#!/bin/sh\n");
        f.close();
        QVERIFY(QFile::setPermissions(good, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));

        ToolLocator::setOverride("faketool", good);
        QCOMPARE(ToolLocator::find("faketool"), QFileInfo(good).absoluteFilePath());

        // An override that is not an executable file does not fall back to PATH.
        ToolLocator::setOverride("faketool", dir.filePath("missing"));
        QVERIFY(ToolLocator::find("faketool").isEmpty());
        ToolLocator::setOverride("faketool", dir.path()); // a directory
        QVERIFY(ToolLocator::find("faketool").isEmpty());

        ToolLocator::setOverride("faketool", QString());
    }
};

QTEST_APPLESS_MAIN(TstSettingsManager)
#include "tst_settingsmanager.moc"
