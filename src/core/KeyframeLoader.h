#pragma once

#include "core/KeyframeIndex.h"

#include <QObject>
#include <QString>

class QProcess;

// Loads the keyframe list of a file with ffprobe, asynchronously.
class KeyframeLoader : public QObject
{
    Q_OBJECT

public:
    explicit KeyframeLoader(QObject *parent = nullptr);
    ~KeyframeLoader() override;

    // Cancels a load in flight. Emits loaded() or failed() once per call
    // (unless cancelled or superseded).
    void load(const QString &path);
    void cancel();

signals:
    void loaded(const KeyframeIndex &index);
    void failed(const QString &reason);

private:
    QProcess *m_process = nullptr;
    quint64 m_generation = 0;
};
