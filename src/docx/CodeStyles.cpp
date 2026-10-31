#include "docx/CodeStyles.h"
#include "docx/Package.h"

namespace docx {
namespace code {

static const char *kBlockXml =
    "<w:style w:type=\"paragraph\" w:customStyle=\"1\" w:styleId=\"Code\"><w:name w:val=\"Code\"/>"
    "<w:basedOn w:val=\"Normal\"/><w:next w:val=\"Code\"/><w:qFormat/>"
    "<w:pPr><w:keepLines/><w:pBdr><w:left w:val=\"single\" w:sz=\"18\" w:space=\"6\" w:color=\"C9CED6\"/></w:pBdr>"
    "<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"F3F4F6\"/>"
    "<w:spacing w:before=\"60\" w:after=\"60\" w:line=\"240\" w:lineRule=\"auto\"/><w:ind w:left=\"144\"/>"
    "<w:contextualSpacing/></w:pPr>"
    "<w:rPr><w:rFonts w:ascii=\"Consolas\" w:hAnsi=\"Consolas\" w:eastAsia=\"Consolas\" w:cs=\"Consolas\"/>"
    "<w:noProof/><w:sz w:val=\"19\"/><w:szCs w:val=\"19\"/></w:rPr></w:style>";

static const char *kInlineXml =
    "<w:style w:type=\"character\" w:customStyle=\"1\" w:styleId=\"CodeChar\"><w:name w:val=\"Inline Code\"/>"
    "<w:basedOn w:val=\"DefaultParagraphFont\"/><w:qFormat/>"
    "<w:rPr><w:rFonts w:ascii=\"Consolas\" w:hAnsi=\"Consolas\" w:eastAsia=\"Consolas\" w:cs=\"Consolas\"/>"
    "<w:noProof/><w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"F3F4F6\"/></w:rPr></w:style>";

static const char *kStylesRel = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles";

bool ensureStyles(Package &pkg)
{
    const bool haveBlock = pkg.styles.style(QString::fromLatin1(kBlockStyle)) != nullptr;
    const bool haveInline = pkg.styles.style(QString::fromLatin1(kInlineStyle)) != nullptr;
    if (haveBlock && haveInline)
        return true;

    QString part = pkg.partForRelType(QStringLiteral("/styles"));
    if (part.isEmpty()) {
        // No styles part (rare): add one with just what we need.
        part = pkg.mainDir() + QStringLiteral("/styles.xml");
        pkg.parts.insert(part, QByteArray("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                                          "<w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
                                          "</w:styles>"));
        pkg.partOrder.append(part);
        QString id = QStringLiteral("rIdApStyles");
        pkg.docRels.append({id, QString::fromLatin1(kStylesRel), QStringLiteral("styles.xml"), QString()});
        QByteArray &ct = pkg.parts[QStringLiteral("[Content_Types].xml")];
        if (!ct.contains("/styles.xml")) {
            const int close = ct.lastIndexOf("</Types>");
            if (close >= 0)
                ct.insert(close, QByteArray("<Override PartName=\"/") + part.toUtf8()
                                     + "\" ContentType=\"application/vnd.openxmlformats-officedocument."
                                       "wordprocessingml.styles+xml\"/>");
        }
    }
    QByteArray xml = pkg.parts.value(part);
    const int close = xml.lastIndexOf("</w:styles>");
    if (close < 0)
        return false;
    QByteArray add;
    if (!haveBlock)
        add += kBlockXml;
    if (!haveInline)
        add += kInlineXml;
    xml.insert(close, add);
    pkg.parts.insert(part, xml);
    pkg.styles.parse(xml);
    return true;
}

} // namespace code
} // namespace docx
