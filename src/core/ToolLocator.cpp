#include "core/ToolLocator.h"

#include <QFileInfo>
#include <QHash>
#include <QStandardPaths>

namespace ToolLocator {

namespace {
QHash<QString, QString> &overrides()
{
    static QHash<QString, QString> map;
    return map;
}
} // namespace

void setOverride(const QString &name, const QString &path)
{
    if (path.isEmpty())
        overrides().remove(name);
    else
        overrides().insert(name, path);
}

QString find(const QString &name)
{
    const QString custom = overrides().value(name);
    if (!custom.isEmpty()) {
        const QFileInfo fi(custom);
        if (fi.isFile() && fi.isExecutable())
            return fi.absoluteFilePath();
        // An unusable override must not silently fall back: the user asked for it.
        return QString();
    }
    return QStandardPaths::findExecutable(name);
}

} // namespace ToolLocator
