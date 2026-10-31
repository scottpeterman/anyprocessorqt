#pragma once

#include <QStringList>

// Most-recently-used document list, kept in QSettings ("recentFiles").
namespace recent {

constexpr int kMax = 10;

// `path` moved (or added) to the front; duplicates removed; at most kMax.
QStringList pushed(QStringList list, const QString &path);

QStringList load();               // as saved, missing files included
void add(const QString &path);    // push and save
void remove(const QString &path);
void clear();

} // namespace recent
