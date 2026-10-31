#include "docx/Package.h"
#include "docx/Units.h"
#include "docx/Xml.h"
#include "zip/Zip.h"

#include <QDir>
#include <QRegularExpression>

namespace docx {

double PageSetup::contentWidthPx() const
{
    return twipsToPx(width - left - right);
}

QString Package::mainDir() const
{
    const int slash = mainPart.lastIndexOf(QLatin1Char('/'));
    return slash < 0 ? QString() : mainPart.left(slash);
}

QString Package::mainRelsPath() const
{
    const int slash = mainPart.lastIndexOf(QLatin1Char('/'));
    const QString dir = slash < 0 ? QString() : mainPart.left(slash + 1);
    const QString file = mainPart.mid(slash + 1);
    return dir + QStringLiteral("_rels/") + file + QStringLiteral(".rels");
}

QString Package::resolveTarget(const QString &target) const
{
    if (target.startsWith(QLatin1Char('/')))
        return target.mid(1);
    const QString dir = mainDir();
    return QDir::cleanPath(dir.isEmpty() ? target : dir + QLatin1Char('/') + target);
}

const Relationship *Package::rel(const QString &id) const
{
    for (const Relationship &r : docRels)
        if (r.id == id)
            return &r;
    return nullptr;
}

QString Package::partForRelType(const QString &typeSuffix) const
{
    for (const Relationship &r : docRels)
        if (r.type.endsWith(typeSuffix))
            return resolveTarget(r.target);
    return QString();
}

static QList<Relationship> parseRels(const QByteArray &xml)
{
    QList<Relationship> out;
    auto root = parseXml(xml);
    if (!root)
        return out;
    for (const XElem *e : root->children(u"Relationship")) {
        Relationship r;
        r.id = e->attr(u"Id");
        r.type = e->attr(u"Type");
        r.target = e->attr(u"Target");
        r.targetMode = e->attr(u"TargetMode");
        out.append(r);
    }
    return out;
}

static void parsePage(const XElem *sectPr, PageSetup &page)
{
    if (!sectPr)
        return;
    if (const XElem *sz = sectPr->child(u"pgSz")) {
        page.width = sz->intAttr(u"w", page.width);
        page.height = sz->intAttr(u"h", page.height);
    }
    if (const XElem *m = sectPr->child(u"pgMar")) {
        page.top = m->intAttr(u"top", page.top);
        page.bottom = m->intAttr(u"bottom", page.bottom);
        page.left = m->hasAttr(u"left") ? m->intAttr(u"left") : m->intAttr(u"start", page.left);
        page.right = m->hasAttr(u"right") ? m->intAttr(u"right") : m->intAttr(u"end", page.right);
    }
}

bool Package::loadFrom(const QByteArray &zipData, QString *error)
{
    zip::Reader zr;
    if (!zr.open(zipData)) {
        if (error)
            *error = zr.error();
        return false;
    }
    partOrder.clear();
    parts.clear();
    for (const QString &name : zr.names()) {
        bool ok = false;
        QByteArray data = zr.read(name, &ok);
        if (!ok) {
            if (error)
                *error = QStringLiteral("could not read part %1").arg(name);
            return false;
        }
        partOrder.append(name);
        parts.insert(name, data);
    }

    // Main document part from the package relationships.
    mainPart = QStringLiteral("word/document.xml");
    for (const Relationship &r : parseRels(parts.value(QStringLiteral("_rels/.rels")))) {
        if (r.type.endsWith(QLatin1String("/officeDocument"))) {
            mainPart = r.target.startsWith(QLatin1Char('/')) ? r.target.mid(1) : r.target;
            break;
        }
    }
    if (!parts.contains(mainPart)) {
        if (error)
            *error = QStringLiteral("no main document part (%1)").arg(mainPart);
        return false;
    }
    docRels = parseRels(parts.value(mainRelsPath()));

    const QString stylesPart = partForRelType(QStringLiteral("/styles"));
    if (!stylesPart.isEmpty())
        styles.parse(parts.value(stylesPart));
    const QString numberingPart = partForRelType(QStringLiteral("/numbering"));
    if (!numberingPart.isEmpty())
        numbering.parse(parts.value(numberingPart));

    const QString docText = QString::fromUtf8(parts.value(mainPart));

    // Root start tag (keeps every namespace declaration and mc:Ignorable).
    static const QRegularExpression rootRe(QStringLiteral("<([A-Za-z_][\\w.-]*:)?document[\\s>][^>]*>"));
    const auto rm = rootRe.match(docText);
    rootStartTag = rm.hasMatch() ? rm.captured(0) : QString();

    // Body-level sectPr is the last one in the document.
    sectPrXml.clear();
    static const QRegularExpression sectRe(QStringLiteral("<([A-Za-z_][\\w.-]*:)?sectPr[\\s>/]"));
    qsizetype pos = -1;
    QString prefix;
    auto it = sectRe.globalMatch(docText);
    while (it.hasNext()) {
        const auto m = it.next();
        pos = m.capturedStart(0);
        prefix = m.captured(1);
    }
    if (pos >= 0) {
        const QString closeTag = QStringLiteral("</%1sectPr>").arg(prefix);
        const qsizetype gt = docText.indexOf(QLatin1Char('>'), pos);
        if (gt > 0 && docText.at(gt - 1) == QLatin1Char('/')) {
            sectPrXml = docText.mid(pos, gt - pos + 1);
        } else {
            const qsizetype end = docText.indexOf(closeTag, pos);
            if (end > 0)
                sectPrXml = docText.mid(pos, end + closeTag.size() - pos);
        }
        // Only keep it if it is the body-level one (directly before </body>).
        const QString after = docText.mid(pos + sectPrXml.size()).trimmed();
        if (!after.startsWith(QStringLiteral("</%1body>").arg(prefix)))
            sectPrXml.clear();
    }

    page = PageSetup();
    if (!sectPrXml.isEmpty()) {
        QString wrapped = QStringLiteral("<x xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
                                         "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">")
                          + sectPrXml + QStringLiteral("</x>");
        // Declare any other prefixes used in the root tag so the fragment parses.
        static const QRegularExpression nsRe(QStringLiteral("xmlns:([\\w.-]+)=\"([^\"]*)\""));
        auto nit = nsRe.globalMatch(rootStartTag);
        QString decls;
        while (nit.hasNext()) {
            const auto m = nit.next();
            if (m.captured(1) != QLatin1String("w") && m.captured(1) != QLatin1String("r"))
                decls += QStringLiteral(" xmlns:%1=\"%2\"").arg(m.captured(1), m.captured(2));
        }
        wrapped.insert(2, decls);
        auto frag = parseXml(wrapped.toUtf8());
        if (frag)
            parsePage(frag->child(u"sectPr"), page);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Blank document
// ---------------------------------------------------------------------------

static const char *kContentTypes =
    R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Default Extension="png" ContentType="image/png"/><Default Extension="jpeg" ContentType="image/jpeg"/><Default Extension="jpg" ContentType="image/jpeg"/><Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/><Override PartName="/word/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml"/><Override PartName="/word/settings.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.settings+xml"/><Override PartName="/docProps/core.xml" ContentType="application/vnd.openxmlformats-package.core-properties+xml"/><Override PartName="/docProps/app.xml" ContentType="application/vnd.openxmlformats-officedocument.extended-properties+xml"/></Types>)";

static const char *kRootRels =
    R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/><Relationship Id="rId2" Type="http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties" Target="docProps/core.xml"/><Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties" Target="docProps/app.xml"/></Relationships>)";

static const char *kDocRels =
    R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/><Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/settings" Target="settings.xml"/></Relationships>)";

static const char *kSettings =
    R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:settings xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"><w:defaultTabStop w:val="720"/><w:characterSpacingControl w:val="doNotCompress"/><w:compat><w:compatSetting w:name="compatibilityMode" w:uri="http://schemas.microsoft.com/office/word" w:val="15"/></w:compat></w:settings>)";

static const char *kCore =
    R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<cp:coreProperties xmlns:cp="http://schemas.openxmlformats.org/package/2006/metadata/core-properties" xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:dcterms="http://purl.org/dc/terms/" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"><dc:title></dc:title><dc:creator></dc:creator></cp:coreProperties>)";

static const char *kApp =
    R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Properties xmlns="http://schemas.openxmlformats.org/officeDocument/2006/extended-properties"><Application>anyprocessorqt</Application></Properties>)";

static const char *kStyles =
    R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:styles xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"><w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii="Calibri" w:hAnsi="Calibri" w:eastAsia="Calibri" w:cs="Calibri"/><w:sz w:val="22"/><w:szCs w:val="22"/><w:lang w:val="en-US" w:eastAsia="en-US" w:bidi="ar-SA"/></w:rPr></w:rPrDefault><w:pPrDefault><w:pPr><w:spacing w:after="160" w:line="259" w:lineRule="auto"/></w:pPr></w:pPrDefault></w:docDefaults><w:style w:type="paragraph" w:default="1" w:styleId="Normal"><w:name w:val="Normal"/><w:qFormat/></w:style><w:style w:type="paragraph" w:styleId="Title"><w:name w:val="Title"/><w:basedOn w:val="Normal"/><w:next w:val="Normal"/><w:qFormat/><w:pPr><w:spacing w:after="80" w:line="240" w:lineRule="auto"/></w:pPr><w:rPr><w:rFonts w:ascii="Calibri Light" w:hAnsi="Calibri Light"/><w:sz w:val="56"/><w:szCs w:val="56"/></w:rPr></w:style><w:style w:type="paragraph" w:styleId="Heading1"><w:name w:val="heading 1"/><w:basedOn w:val="Normal"/><w:next w:val="Normal"/><w:qFormat/><w:pPr><w:keepNext/><w:keepLines/><w:spacing w:before="360" w:after="80"/><w:outlineLvl w:val="0"/></w:pPr><w:rPr><w:rFonts w:ascii="Calibri Light" w:hAnsi="Calibri Light"/><w:color w:val="2F5496"/><w:sz w:val="40"/><w:szCs w:val="40"/></w:rPr></w:style><w:style w:type="paragraph" w:styleId="Heading2"><w:name w:val="heading 2"/><w:basedOn w:val="Normal"/><w:next w:val="Normal"/><w:qFormat/><w:pPr><w:keepNext/><w:keepLines/><w:spacing w:before="160" w:after="80"/><w:outlineLvl w:val="1"/></w:pPr><w:rPr><w:rFonts w:ascii="Calibri Light" w:hAnsi="Calibri Light"/><w:color w:val="2F5496"/><w:sz w:val="32"/><w:szCs w:val="32"/></w:rPr></w:style><w:style w:type="paragraph" w:styleId="Heading3"><w:name w:val="heading 3"/><w:basedOn w:val="Normal"/><w:next w:val="Normal"/><w:qFormat/><w:pPr><w:keepNext/><w:keepLines/><w:spacing w:before="160" w:after="80"/><w:outlineLvl w:val="2"/></w:pPr><w:rPr><w:color w:val="1F3763"/><w:sz w:val="28"/><w:szCs w:val="28"/></w:rPr></w:style><w:style w:type="paragraph" w:styleId="Quote"><w:name w:val="Quote"/><w:basedOn w:val="Normal"/><w:next w:val="Normal"/><w:qFormat/><w:pPr><w:pBdr><w:left w:val="single" w:sz="12" w:space="8" w:color="2F5496"/></w:pBdr><w:ind w:left="360"/></w:pPr><w:rPr><w:i/><w:color w:val="404040"/></w:rPr></w:style><w:style w:type="paragraph" w:styleId="ListParagraph"><w:name w:val="List Paragraph"/><w:basedOn w:val="Normal"/><w:qFormat/><w:pPr><w:ind w:left="720"/></w:pPr></w:style><w:style w:type="character" w:default="1" w:styleId="DefaultParagraphFont"><w:name w:val="Default Paragraph Font"/><w:uiPriority w:val="1"/><w:semiHidden/></w:style><w:style w:type="character" w:styleId="Hyperlink"><w:name w:val="Hyperlink"/><w:rPr><w:color w:val="0563C1"/><w:u w:val="single"/></w:rPr></w:style><w:style w:type="table" w:default="1" w:styleId="TableNormal"><w:name w:val="Normal Table"/><w:tblPr><w:tblInd w:w="0" w:type="dxa"/><w:tblCellMar><w:top w:w="0" w:type="dxa"/><w:left w:w="108" w:type="dxa"/><w:bottom w:w="0" w:type="dxa"/><w:right w:w="108" w:type="dxa"/></w:tblCellMar></w:tblPr></w:style></w:styles>)";

static const char *kDocument =
    R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><w:body><w:p/><w:sectPr><w:pgSz w:w="12240" w:h="15840"/><w:pgMar w:top="1440" w:right="1440" w:bottom="1440" w:left="1440" w:header="720" w:footer="720" w:gutter="0"/></w:sectPr></w:body></w:document>)";

Package Package::blank()
{
    QList<zip::Entry> entries = {
        {QStringLiteral("[Content_Types].xml"), QByteArray(kContentTypes)},
        {QStringLiteral("_rels/.rels"), QByteArray(kRootRels)},
        {QStringLiteral("word/document.xml"), QByteArray(kDocument)},
        {QStringLiteral("word/_rels/document.xml.rels"), QByteArray(kDocRels)},
        {QStringLiteral("word/styles.xml"), QByteArray(kStyles)},
        {QStringLiteral("word/settings.xml"), QByteArray(kSettings)},
        {QStringLiteral("docProps/core.xml"), QByteArray(kCore)},
        {QStringLiteral("docProps/app.xml"), QByteArray(kApp)},
    };
    Package p;
    QString err;
    p.loadFrom(zip::write(entries), &err);
    return p;
}

} // namespace docx
