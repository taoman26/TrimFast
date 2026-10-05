#include "core/KeyframeLoader.h"

#include "core/ToolLocator.h"

#include <QProcess>

KeyframeLoader::KeyframeLoader(QObject *parent)
    : QObject(parent)
{
}

KeyframeLoader::~KeyframeLoader()
{
    cancel();
}

void KeyframeLoader::cancel()
{
    ++m_generation;
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(1000);
        m_process->deleteLater();
        m_process = nullptr;
    }
}

void KeyframeLoader::load(const QString &path)
{
    cancel();

    const QString tool = ToolLocator::find(QStringLiteral("ffprobe"));
    if (tool.isEmpty()) {
        emit failed(tr("ffprobe was not found"));
        return;
    }

    const quint64 generation = m_generation;
    m_process = new QProcess(this);
    connect(m_process, &QProcess::finished, this, [this, generation](int code, QProcess::ExitStatus status) {
        if (generation != m_generation || !m_process)
            return;
        const QByteArray out = m_process->readAllStandardOutput();
        const QByteArray err = m_process->readAllStandardError();
        m_process->deleteLater();
        m_process = nullptr;

        if (status != QProcess::NormalExit || code != 0) {
            emit failed(tr("Keyframe scan failed: %1").arg(QString::fromLocal8Bit(err).trimmed()));
            return;
        }
        const KeyframeIndex index = KeyframeIndex::parse(out);
        if (index.isEmpty())
            emit failed(tr("No keyframes found"));
        else
            emit loaded(index);
    });
    connect(m_process, &QProcess::errorOccurred, this, [this, generation](QProcess::ProcessError e) {
        if (generation != m_generation || !m_process || e != QProcess::FailedToStart)
            return;
        m_process->deleteLater();
        m_process = nullptr;
        emit failed(tr("Could not start ffprobe"));
    });

    // Packet headers only (no decoding): fast even for multi-GB files.
    m_process->start(tool, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-select_streams"),
                            QStringLiteral("v:0"), QStringLiteral("-show_entries"),
                            QStringLiteral("packet=pts_time,flags"), QStringLiteral("-of"),
                            QStringLiteral("csv=p=0"), path});
}
