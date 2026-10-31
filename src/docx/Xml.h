#pragma once

// Tiny read-only element tree on top of QXmlStreamReader.
// QDom on Qt 6.4 drops whitespace-only text nodes, which loses the single-space
// <w:t xml:space="preserve"> </w:t> runs that Word emits between words.

#include <QByteArray>
#include <QList>
#include <QString>
#include <QXmlStreamAttributes>

#include <memory>
#include <vector>

namespace docx {

struct XElem {
    QString ns;
    QString name;            // local name
    QXmlStreamAttributes attrs;
    std::vector<std::unique_ptr<XElem>> kids;
    QString text;            // character data directly inside this element

    const XElem *child(QStringView local) const;
    QList<const XElem *> children(QStringView local = {}) const;
    QString attr(QStringView local) const;  // match on local name, any namespace
    bool hasAttr(QStringView local) const;
    int intAttr(QStringView local, int def = 0) const;
};

std::unique_ptr<XElem> parseXml(const QByteArray &data, QString *error = nullptr);

QString xmlEsc(const QString &s);      // text content
QString xmlAttrEsc(const QString &s);  // attribute value

} // namespace docx
