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
    int header = 720, footer = 720;  // distance of header top / footer bottom from the page edge
    double contentWidthPx() const;
};

struct Relationship {
    QString id;
    QString type;
    QString target;
    QString targetMode;
};

// A header or footer referenced from the body section.
struct HdrFtrRef {
    bool footer = false;
    QString type;  // "default", "first", "even"
    QString relId;
    QString part;  // e.g. "word/header1.xml"
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
    int defaultTabStop = 720;  // twips, from settings.xml w:defaultTabStop
    QList<HdrFtrRef> hdrFtr;   // body section's header/footer references
    bool titlePg = false;      // different first page
    bool evenAndOdd = false;   // settings.xml w:evenAndOddHeaders

    Styles styles;
    Numbering numbering;

    QString mainDir() const;                 // "word"
    QString mainRelsPath() const;            // "word/_rels/document.xml.rels"
    QString resolveTarget(const QString &target) const;  // rel target -> part name
    const Relationship *rel(const QString &id) const;
    QString partForRelType(const QString &typeSuffix) const;
    QList<Relationship> relsOf(const QString &part) const;  // a part's own relationships

    const HdrFtrRef *hdrFtrRef(bool footer, const QString &type) const;
    // Replace the XML of a header/footer, creating the part, its relationship,
    // content type and section reference if it doesn't exist yet.
    void setHdrFtrXml(bool footer, const QString &type, const QByteArray &xml);
    void setTitlePg(bool on);

    bool loadFrom(const QByteArray &zipData, QString *error);
    static Package blank();
};

} // namespace docx
