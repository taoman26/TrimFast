#pragma once

#include <QApplication>
#include <QString>

// QApplication that turns "open this file" requests from the system into a signal:
// Haiku (Tracker's "Open With", double-click, `open`) and macOS deliver them as
// QFileOpenEvent instead of command-line arguments. The request may arrive before the
// window exists (the application was just launched for that file), so it is kept until
// somebody asks for it.
class TrimFastApplication : public QApplication
{
    Q_OBJECT

public:
    using QApplication::QApplication;

    // The file the system asked us to open before anyone was listening (empty if none).
    QString takePendingFile();

signals:
    void fileOpenRequested(const QString &path);

protected:
    bool event(QEvent *event) override;

private:
    QString m_pendingFile;
};
