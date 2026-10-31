#pragma once

#include "docx/Numbering.h"
#include "docx/Styles.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>

namespace docx {

struct PageSetup {
    int width = 12240;   // twips (US Letter)
    int height = 15840;
    int top = 1440, right = 1440, bottom = 1440, left = 1440;
    double contentWidthPx() const;
};

struct Relationship {
    QString id;
    QString type;
    QString target;
    QString targetMode;
};

// Everything from the original .docx that the editor model doesn't own.
// On save, document.xml is regenerated and the rest passes through.
struct Package {
    QList<QString> partOrder;
    QHash<QString, QByteArray> parts;

    QString mainPart = QStringLiteral("word/document.xml");
    QList<Relationship> docRels;
    QString rootStartTag;  // original <w:document ...> start tag, namespaces intact
    QString sectPrXml;     // original body-level <w:sectPr> verbatim
    PageSetup page;

    Styles styles;
    Numbering numbering;

    QString mainDir() const;                 // "word"
    QString mainRelsPath() const;            // "word/_rels/document.xml.rels"
    QString resolveTarget(const QString &target) const;  // rel target -> part name
    const Relationship *rel(const QString &id) const;
    QString partForRelType(const QString &typeSuffix) const;

    bool loadFrom(const QByteArray &zipData, QString *error);
    static Package blank();
};

} // namespace docx
