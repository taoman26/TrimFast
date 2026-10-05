#include "app/TrimFastApplication.h"

#include <QFileOpenEvent>
#include <QMetaMethod>

QString TrimFastApplication::takePendingFile()
{
    return std::exchange(m_pendingFile, QString());
}

bool TrimFastApplication::event(QEvent *event)
{
    if (event->type() == QEvent::FileOpen) {
        const QString path = static_cast<QFileOpenEvent *>(event)->file();
        if (path.isEmpty())
            return true; // a URL we cannot use
        if (isSignalConnected(QMetaMethod::fromSignal(&TrimFastApplication::fileOpenRequested)))
            emit fileOpenRequested(path);
        else
            m_pendingFile = path;
        return true;
    }
    return QApplication::event(event);
}
