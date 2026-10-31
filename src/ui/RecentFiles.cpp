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

// Stored as one newline-joined string: QSettings' native backends differ in how
// they round-trip string lists (the Windows registry in particular).
static void save(const QStringList &list)
{
    QSettings s;
    if (list.isEmpty())
        s.remove(kKey);
    else
        s.setValue(kKey, list.join(QLatin1Char('\n')));
    s.sync();
}

QStringList load()
{
    const QVariant v = QSettings().value(kKey);
    QStringList out;
    const QStringList raw = v.typeId() == QMetaType::QStringList ? v.toStringList()
                                                                 : v.toString().split(QLatin1Char('\n'));
    for (const QString &p : raw)
        if (!p.trimmed().isEmpty())
            out.append(p);
    return out;
}

void add(const QString &path)
{
    save(pushed(load(), path));
}

void remove(const QString &path)
{
    QStringList l = load();
    l.removeAll(normalized(path));
    save(l);
}

void clear()
{
    save({});
}

} // namespace recent
