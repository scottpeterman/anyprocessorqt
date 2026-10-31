#include "ui/RecentFiles.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace recent {

namespace {
const QString kKey = QStringLiteral("recentFiles");

QString normalized(const QString &path)
{
    const QFileInfo fi(path);
    return QDir::cleanPath(fi.isRelative() ? fi.absoluteFilePath() : path);
}
} // namespace

QStringList pushed(QStringList list, const QString &path)
{
    const QString p = normalized(path);
    list.removeAll(p);
    list.prepend(p);
    while (list.size() > kMax)
        list.removeLast();
    return list;
}

QStringList load()
{
    return QSettings().value(kKey).toStringList();
}

void add(const QString &path)
{
    QSettings().setValue(kKey, pushed(load(), path));
}

void remove(const QString &path)
{
    QStringList l = load();
    l.removeAll(normalized(path));
    QSettings().setValue(kKey, l);
}

void clear()
{
    QSettings().remove(kKey);
}

} // namespace recent
