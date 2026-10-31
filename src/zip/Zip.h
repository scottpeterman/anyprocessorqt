#pragma once

// Minimal ZIP reader/writer for OOXML packages.
// No dependency beyond QtCore: inflate is implemented here, deflate uses qCompress.

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace zip {

struct Entry {
    QString name;
    QByteArray data;
};

class Reader {
public:
    bool open(const QByteArray &archive);
    QStringList names() const { return m_order; }
    bool contains(const QString &name) const { return m_entries.contains(name); }
    QByteArray read(const QString &name, bool *ok = nullptr) const;
    QString error() const { return m_error; }

private:
    struct Info {
        quint16 method = 0;
        quint32 csize = 0;
        quint32 usize = 0;
        quint32 offset = 0;
        quint32 crc = 0;
    };
    QByteArray m_buf;
    QHash<QString, Info> m_entries;
    QStringList m_order;
    QString m_error;
};

QByteArray write(const QList<Entry> &entries);

QByteArray inflateRaw(const QByteArray &in, qsizetype expectedSize, bool *ok);
QByteArray deflateRaw(const QByteArray &in);
quint32 crc32(const QByteArray &data);

} // namespace zip
