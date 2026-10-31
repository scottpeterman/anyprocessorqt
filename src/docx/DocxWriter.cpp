#include "docx/DocxWriter.h"
#include "docx/Convert.h"
#include "docx/Units.h"
#include "docx/Xml.h"
#include "zip/Zip.h"

#include <QBuffer>

#include <algorithm>
#include <QDir>
#include <QHash>
#include <QImage>
#include <QPixmap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextList>
#include <QTextTable>
#include <QUrl>

namespace docx {

namespace {

const QString kRelImage = QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/image");
const QString kRelHyperlink =
    QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink");
const QString kRelNumbering =
    QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/numbering");
const QString kNsW = QStringLiteral("http://schemas.openxmlformats.org/wordprocessingml/2006/main");
const QString kNsR = QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships");
const QString kNsWp = QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing");
const QString kNsA = QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/main");
const QString kNsPic = QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/picture");

QString onOff(const char *tag, bool v)
{
    return v ? QStringLiteral("<w:%1/>").arg(QLatin1String(tag))
             : QStringLiteral("<w:%1 w:val=\"0\"/>").arg(QLatin1String(tag));
}

struct NewList {
    int numId = 0;
    int absId = 0;
    QTextListFormat::Style style = QTextListFormat::ListDisc;
};

struct Writer {
    const QTextDocument *doc;
    Package &pkg;
    QString out;

    QSet<QString> relIds;
    QList<Relationship> newRels;
    QHash<QString, QString> imageRel;      // part -> rId
    QHash<QString, QString> hyperlinkRel;  // href -> rId
    QHash<QString, QString> newImagePart;  // resource name -> part
    QHash<QString, QByteArray> newParts;
    QSet<QString> usedMedia;
    QHash<const QTextList *, NewList> newLists;
    int nextAbs = 0;
    int nextNum = 0;
    int relCounter = 0;
    int docPrId = 0;
    int mediaCounter = 0;

    Writer(const QTextDocument *d, Package &p) : doc(d), pkg(p)
    {
        // Image and hyperlink relationships are regenerated, so their ids are free.
        for (const Relationship &r : pkg.docRels)
            if (r.type != kRelImage && r.type != kRelHyperlink)
                relIds.insert(r.id);
        nextAbs = pkg.numbering.maxAbstractId() + 1;
        nextNum = pkg.numbering.maxNumId() + 1;
    }

    QString newRelId()
    {
        QString id;
        do {
            id = QStringLiteral("rIdAp%1").arg(++relCounter);
        } while (relIds.contains(id));
        relIds.insert(id);
        return id;
    }

    QString relTargetFor(const QString &part) const
    {
        const QString dir = pkg.mainDir();
        if (!dir.isEmpty() && part.startsWith(dir + QLatin1Char('/')))
            return part.mid(dir.size() + 1);
        return QLatin1Char('/') + part;
    }

    // ---------------------------------------------------------------- runs

    QString rPr(const QTextCharFormat &cf, const RunProps &base, bool includeStyle) const
    {
        const RunProps c = fromCharFormat(cf);
        QString s;
        if (includeStyle) {
            const QString rs = cf.stringProperty(RStyle);
            if (!rs.isEmpty())
                s += QStringLiteral("<w:rStyle w:val=\"%1\"/>").arg(xmlAttrEsc(rs));
        }
        if (c.font != base.font.value_or(QString::fromLatin1(kDefaultFont))) {
            const QString f = xmlAttrEsc(*c.font);
            s += QStringLiteral("<w:rFonts w:ascii=\"%1\" w:hAnsi=\"%1\" w:eastAsia=\"%1\" w:cs=\"%1\"/>").arg(f);
        }
        if (*c.b != base.b.value_or(false))
            s += onOff("b", *c.b) + onOff("bCs", *c.b);
        if (*c.i != base.i.value_or(false))
            s += onOff("i", *c.i) + onOff("iCs", *c.i);
        if (*c.strike != base.strike.value_or(false))
            s += onOff("strike", *c.strike);
        if (*c.color != base.color.value_or(QStringLiteral("auto")))
            s += QStringLiteral("<w:color w:val=\"%1\"/>").arg(*c.color);
        if (*c.halfPts != base.halfPts.value_or(kDefaultHalfPts))
            s += QStringLiteral("<w:sz w:val=\"%1\"/><w:szCs w:val=\"%1\"/>").arg(*c.halfPts);
        if (*c.highlight != base.highlight.value_or(QString()))
            s += QStringLiteral("<w:highlight w:val=\"%1\"/>").arg(c.highlight->isEmpty() ? QStringLiteral("none")
                                                                                            : *c.highlight);
        if (*c.u != base.u.value_or(false))
            s += c.u.value() ? QStringLiteral("<w:u w:val=\"single\"/>") : QStringLiteral("<w:u w:val=\"none\"/>");
        if (c.highlight->isEmpty() && *c.shd != base.shd.value_or(QString()))
            s += QStringLiteral("<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"%1\"/>")
                     .arg(c.shd->isEmpty() ? QStringLiteral("auto") : *c.shd);
        if (*c.vertAlign != base.vertAlign.value_or(QStringLiteral("baseline")))
            s += QStringLiteral("<w:vertAlign w:val=\"%1\"/>").arg(*c.vertAlign);
        return s.isEmpty() ? QString() : QStringLiteral("<w:rPr>") + s + QStringLiteral("</w:rPr>");
    }

    void textRuns(const QString &text, const QString &props)
    {
        const QString open = QStringLiteral("<w:r>") + props;
        QString chunk;
        QString runBody;
        auto flush = [&] {
            if (!chunk.isEmpty()) {
                runBody += QStringLiteral("<w:t xml:space=\"preserve\">") + xmlEsc(chunk) + QStringLiteral("</w:t>");
                chunk.clear();
            }
        };
        for (const QChar ch : text) {
            switch (ch.unicode()) {
            case '\t': flush(); runBody += QStringLiteral("<w:tab/>"); break;
            case 0x2028: flush(); runBody += QStringLiteral("<w:br/>"); break;
            case 0x2011: flush(); runBody += QStringLiteral("<w:noBreakHyphen/>"); break;
            case 0x00AD: flush(); runBody += QStringLiteral("<w:softHyphen/>"); break;
            case 0xFFFC: break;
            case 0x2029: flush(); runBody += QStringLiteral("<w:br/>"); break;
            default: chunk += ch; break;
            }
        }
        flush();
        if (!runBody.isEmpty())
            out += open + runBody + QStringLiteral("</w:r>");
    }

    QString imagePartFor(const QTextImageFormat &f, QSize *naturalSize)
    {
        const QString name = f.name();
        if (name.startsWith(QLatin1String("docx:"))) {
            const QString part = name.mid(5);
            if (pkg.parts.contains(part)) {
                const QVariant v = doc->resource(QTextDocument::ImageResource, QUrl(name));
                if (v.canConvert<QImage>())
                    *naturalSize = v.value<QImage>().size();
                return part;
            }
        }
        if (newImagePart.contains(name))
            return newImagePart.value(name);
        const QVariant v = doc->resource(QTextDocument::ImageResource, QUrl(name));
        QImage img;
        if (v.typeId() == QMetaType::QImage)
            img = v.value<QImage>();
        else if (v.typeId() == QMetaType::QPixmap)
            img = v.value<QPixmap>().toImage();
        else if (v.typeId() == QMetaType::QByteArray)
            img.loadFromData(v.toByteArray());
        if (img.isNull())
            return QString();
        *naturalSize = img.size();
        QByteArray png;
        QBuffer buf(&png);
        buf.open(QIODevice::WriteOnly);
        img.save(&buf, "PNG");
        QString part;
        do {
            part = QStringLiteral("%1/media/anyproc_image%2.png").arg(pkg.mainDir()).arg(++mediaCounter);
        } while (pkg.parts.contains(part) || newParts.contains(part));
        newParts.insert(part, png);
        newImagePart.insert(name, part);
        return part;
    }

    void imageRun(const QTextImageFormat &f)
    {
        QSize natural;
        const QString part = imagePartFor(f, &natural);
        if (part.isEmpty())
            return;
        usedMedia.insert(part);
        QString rid = imageRel.value(part);
        if (rid.isEmpty()) {
            rid = newRelId();
            imageRel.insert(part, rid);
            newRels.append({rid, kRelImage, relTargetFor(part), QString()});
        }
        double w = f.width() > 0 ? f.width() : natural.width();
        double h = f.height() > 0 ? f.height() : natural.height();
        if (f.width() > 0 && f.height() <= 0 && natural.width() > 0)
            h = natural.height() * w / natural.width();
        const qint64 cx = pxToEmu(w), cy = pxToEmu(h);
        const int id = ++docPrId;
        out += QStringLiteral(
                   "<w:r><w:drawing><wp:inline distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\">"
                   "<wp:extent cx=\"%1\" cy=\"%2\"/><wp:effectExtent l=\"0\" t=\"0\" r=\"0\" b=\"0\"/>"
                   "<wp:docPr id=\"%3\" name=\"Picture %3\"/>"
                   "<wp:cNvGraphicFramePr><a:graphicFrameLocks xmlns:a=\"%5\" noChangeAspect=\"1\"/></wp:cNvGraphicFramePr>"
                   "<a:graphic xmlns:a=\"%5\"><a:graphicData uri=\"%6\"><pic:pic xmlns:pic=\"%6\">"
                   "<pic:nvPicPr><pic:cNvPr id=\"%3\" name=\"Picture %3\"/><pic:cNvPicPr/></pic:nvPicPr>"
                   "<pic:blipFill><a:blip r:embed=\"%4\"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill>"
                   "<pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"%1\" cy=\"%2\"/></a:xfrm>"
                   "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr>"
                   "</pic:pic></a:graphicData></a:graphic></wp:inline></w:drawing></w:r>")
                   .arg(cx)
                   .arg(cy)
                   .arg(id)
                   .arg(rid, kNsA, kNsPic);
    }

    QString hyperlinkOpen(const QString &href)
    {
        if (href.startsWith(QLatin1Char('#')))
            return QStringLiteral("<w:hyperlink w:anchor=\"%1\">").arg(xmlAttrEsc(href.mid(1)));
        QString rid = hyperlinkRel.value(href);
        if (rid.isEmpty()) {
            rid = newRelId();
            hyperlinkRel.insert(href, rid);
            newRels.append({rid, kRelHyperlink, href, QStringLiteral("External")});
        }
        return QStringLiteral("<w:hyperlink r:id=\"%1\">").arg(rid);
    }

    // ---------------------------------------------------------- paragraphs

    struct ListInfo {
        int numId = 0;
        int ilvl = 0;
        bool fromStyle = false;
    };

    ListInfo listInfo(const QTextList *list, const QTextBlockFormat &bf)
    {
        ListInfo li;
        const QTextListFormat lf = list->format();
        if (lf.hasProperty(NumId)) {
            li.numId = lf.intProperty(NumId);
            li.ilvl = lf.intProperty(Ilvl) + bf.indent();
            li.fromStyle = lf.boolProperty(NumFromStyle) && bf.indent() == 0;
        } else {
            auto it = newLists.find(list);
            if (it == newLists.end()) {
                NewList nl;
                nl.numId = nextNum++;
                nl.absId = nextAbs++;
                nl.style = lf.style();
                it = newLists.insert(list, nl);
            }
            li.numId = it->numId;
            li.ilvl = qMax(0, lf.indent() - 1) + bf.indent();
        }
        li.ilvl = qBound(0, li.ilvl, 8);
        return li;
    }

    void paragraph(const QTextBlock &block)
    {
        const QTextBlockFormat bf = block.blockFormat();
        QString styleId = bf.stringProperty(PStyle);
        if (styleId.isEmpty() || !pkg.styles.style(styleId))
            styleId = pkg.styles.defaultParagraphStyle();
        const ParaProps sp = pkg.styles.resolvedPara(styleId);
        const RunProps stRun = pkg.styles.resolvedParaRun(styleId);
        const QTextList *list = block.textList();
        const ParaProps c = fromBlockFormat(bf, doc->indentWidth());

        QString p;
        if (styleId != pkg.styles.defaultParagraphStyle() || pkg.styles.style(styleId) == nullptr)
            p += QStringLiteral("<w:pStyle w:val=\"%1\"/>").arg(xmlAttrEsc(styleId));
        if (*c.keepNext != sp.keepNext.value_or(false))
            p += onOff("keepNext", *c.keepNext);
        if (*c.keepLines != sp.keepLines.value_or(false))
            p += onOff("keepLines", *c.keepLines);
        if (*c.pageBreakBefore != sp.pageBreakBefore.value_or(false))
            p += onOff("pageBreakBefore", *c.pageBreakBefore);
        const bool ctxDiff = *c.contextualSpacing != sp.contextualSpacing.value_or(false);

        if (list) {
            const ListInfo li = listInfo(list, bf);
            const bool styleMatches = li.fromStyle && sp.numId.value_or(0) == li.numId
                                      && sp.ilvl.value_or(0) == li.ilvl;
            if (!styleMatches)
                p += QStringLiteral("<w:numPr><w:ilvl w:val=\"%1\"/><w:numId w:val=\"%2\"/></w:numPr>")
                         .arg(li.ilvl)
                         .arg(li.numId);
        } else if (pkg.numbering.isRealList(sp.numId.value_or(0), sp.ilvl.value_or(0))) {
            p += QStringLiteral("<w:numPr><w:numId w:val=\"0\"/></w:numPr>");
        }

        // Borders
        static const char *sideNames[4] = {"top", "left", "bottom", "right"};
        QString bdr;
        for (int s = 0; s < 4; ++s) {
            const QString cur = c.bdr[s].value_or(QString());
            const QString st = sp.bdr[s].value_or(QString());
            if (cur == st)
                continue;
            if (cur.isEmpty()) {
                bdr += QStringLiteral("<w:%1 w:val=\"nil\"/>").arg(QLatin1String(sideNames[s]));
            } else {
                const QStringList f = cur.split(QLatin1Char('|'));
                if (f.size() == 4)
                    bdr += QStringLiteral("<w:%1 w:val=\"%2\" w:sz=\"%3\" w:space=\"%4\" w:color=\"%5\"/>")
                               .arg(QLatin1String(sideNames[s]), f[0], f[1], f[2], f[3]);
            }
        }
        if (!bdr.isEmpty())
            p += QStringLiteral("<w:pBdr>") + bdr + QStringLiteral("</w:pBdr>");

        if (*c.shd != sp.shd.value_or(QString()))
            p += QStringLiteral("<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"%1\"/>")
                     .arg(c.shd->isEmpty() ? QStringLiteral("auto") : *c.shd);

        // Tabs (style tabs that are gone get cleared, new ones added)
        QList<TabStop> styleTabs;
        for (const TabStop &t : sp.tabs)
            if (t.val == QLatin1String("left") || t.val == QLatin1String("right") || t.val == QLatin1String("center")
                || t.val == QLatin1String("decimal"))
                styleTabs.append(TabStop{t.val, t.pos, QString()});
        QList<TabStop> curTabs = c.tabs;
        if (curTabs != styleTabs) {
            QString tabs;
            for (const TabStop &t : styleTabs)
                if (!curTabs.contains(t))
                    tabs += QStringLiteral("<w:tab w:val=\"clear\" w:pos=\"%1\"/>").arg(t.pos);
            for (const TabStop &t : curTabs)
                if (!styleTabs.contains(t))
                    tabs += QStringLiteral("<w:tab w:val=\"%1\" w:pos=\"%2\"/>").arg(t.val).arg(t.pos);
            if (!tabs.isEmpty())
                p += QStringLiteral("<w:tabs>") + tabs + QStringLiteral("</w:tabs>");
        }

        // Spacing
        QString spacing;
        if (*c.before != sp.before.value_or(0))
            spacing += QStringLiteral(" w:before=\"%1\"").arg(*c.before);
        if (*c.after != sp.after.value_or(0))
            spacing += QStringLiteral(" w:after=\"%1\"").arg(*c.after);
        const QString styleRule = sp.lineRule.value_or(QStringLiteral("auto"));
        if (*c.line != sp.line.value_or(240) || *c.lineRule != styleRule)
            spacing += QStringLiteral(" w:line=\"%1\" w:lineRule=\"%2\"").arg(*c.line).arg(*c.lineRule);
        if (!spacing.isEmpty())
            p += QStringLiteral("<w:spacing") + spacing + QStringLiteral("/>");

        // Indentation (list items get theirs from the numbering level)
        if (!list) {
            QString ind;
            if (*c.left != sp.left.value_or(0))
                ind += QStringLiteral(" w:left=\"%1\"").arg(*c.left);
            if (*c.right != sp.right.value_or(0))
                ind += QStringLiteral(" w:right=\"%1\"").arg(*c.right);
            if (*c.firstLine != sp.firstLine.value_or(0)) {
                if (*c.firstLine < 0)
                    ind += QStringLiteral(" w:hanging=\"%1\"").arg(-*c.firstLine);
                else
                    ind += QStringLiteral(" w:firstLine=\"%1\"").arg(*c.firstLine);
            }
            if (!ind.isEmpty())
                p += QStringLiteral("<w:ind") + ind + QStringLiteral("/>");
        }
        if (ctxDiff)
            p += onOff("contextualSpacing", *c.contextualSpacing);

        if (normJc(c.jc) != normJc(sp.jc))
            p += QStringLiteral("<w:jc w:val=\"%1\"/>").arg(normJc(c.jc));

        const QString mark = rPr(block.charFormat(), stRun, false);
        p += mark;

        out += QStringLiteral("<w:p>");
        if (!p.isEmpty())
            out += QStringLiteral("<w:pPr>") + p + QStringLiteral("</w:pPr>");

        QString openHref;
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            if (!frag.isValid())
                continue;
            const QTextCharFormat cf = frag.charFormat();
            const QString href = cf.isAnchor() ? cf.anchorHref() : QString();
            if (href != openHref) {
                if (!openHref.isEmpty())
                    out += QStringLiteral("</w:hyperlink>");
                if (!href.isEmpty())
                    out += hyperlinkOpen(href);
                openHref = href;
            }
            if (cf.isImageFormat()) {
                const QTextImageFormat imf = cf.toImageFormat();
                for (const QChar ch : frag.text())
                    if (ch == QChar::ObjectReplacementCharacter)
                        imageRun(imf);
                continue;
            }
            RunProps base = stRun;
            const QString rs = cf.stringProperty(RStyle);
            if (!rs.isEmpty())
                base.merge(pkg.styles.charStyleRun(rs));
            const QString field = cf.stringProperty(FieldInstr);
            if (!field.isEmpty())
                out += QStringLiteral("<w:fldSimple w:instr=\" %1 \\* MERGEFORMAT \">").arg(field);
            textRuns(frag.text(), rPr(cf, base, true));
            if (!field.isEmpty())
                out += QStringLiteral("</w:fldSimple>");
        }
        if (!openHref.isEmpty())
            out += QStringLiteral("</w:hyperlink>");
        out += QStringLiteral("</w:p>");
    }

    // -------------------------------------------------------------- tables

    void table(QTextTable *t)
    {
        const QTextTableFormat tf = t->format();
        const int rows = t->rows();
        const int cols = t->columns();
        const double contentPx = pkg.page.contentWidthPx();

        QList<int> widths;
        const QList<QTextLength> cons = tf.columnWidthConstraints();
        if (cons.size() == cols) {
            for (const QTextLength &l : cons) {
                double px = 0;
                if (l.type() == QTextLength::FixedLength)
                    px = l.rawValue();
                else if (l.type() == QTextLength::PercentageLength)
                    px = contentPx * l.rawValue() / 100.0;
                widths.append(px > 0 ? pxToTwips(px) : 0);
            }
        }
        if (widths.size() != cols || widths.contains(0)) {
            widths.clear();
            for (int c = 0; c < cols; ++c)
                widths.append(pxToTwips(contentPx / cols));
        }
        int total = 0;
        for (int w : widths)
            total += w;

        out += QStringLiteral("<w:tbl><w:tblPr>");
        const QString ts = tf.stringProperty(TableStyle);
        if (!ts.isEmpty())
            out += QStringLiteral("<w:tblStyle w:val=\"%1\"/>").arg(xmlAttrEsc(ts));
        out += QStringLiteral("<w:tblW w:w=\"%1\" w:type=\"dxa\"/>").arg(total);
        const Qt::Alignment a = tf.alignment() & Qt::AlignHorizontal_Mask;
        if (a & Qt::AlignHCenter)
            out += QStringLiteral("<w:jc w:val=\"center\"/>");
        else if (a & Qt::AlignRight)
            out += QStringLiteral("<w:jc w:val=\"right\"/>");
        if (tf.border() > 0) {
            out += QStringLiteral("<w:tblBorders>");
            for (const char *side : {"top", "left", "bottom", "right", "insideH", "insideV"})
                out += QStringLiteral("<w:%1 w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"auto\"/>")
                           .arg(QLatin1String(side));
            out += QStringLiteral("</w:tblBorders>");
        } else if (ts.isEmpty()) {
            out += QStringLiteral("<w:tblBorders>");
            for (const char *side : {"top", "left", "bottom", "right", "insideH", "insideV"})
                out += QStringLiteral("<w:%1 w:val=\"nil\"/>").arg(QLatin1String(side));
            out += QStringLiteral("</w:tblBorders>");
        }
        out += QStringLiteral("<w:tblLayout w:type=\"fixed\"/>");
        out += QStringLiteral("</w:tblPr><w:tblGrid>");
        for (int w : widths)
            out += QStringLiteral("<w:gridCol w:w=\"%1\"/>").arg(w);
        out += QStringLiteral("</w:tblGrid>");

        for (int r = 0; r < rows; ++r) {
            out += QStringLiteral("<w:tr>");
            for (int c = 0; c < cols; ++c) {
                const QTextTableCell cell = t->cellAt(r, c);
                if (!cell.isValid() || cell.column() != c)
                    continue;
                const int span = qMin(cell.columnSpan(), cols - c);
                int w = 0;
                for (int k = c; k < c + span; ++k)
                    w += widths[k];
                out += QStringLiteral("<w:tc><w:tcPr><w:tcW w:w=\"%1\" w:type=\"dxa\"/>").arg(w);
                if (span > 1)
                    out += QStringLiteral("<w:gridSpan w:val=\"%1\"/>").arg(span);
                const bool continuation = cell.row() != r;
                if (cell.rowSpan() > 1)
                    out += continuation ? QStringLiteral("<w:vMerge/>")
                                        : QStringLiteral("<w:vMerge w:val=\"restart\"/>");
                const QBrush bg = cell.format().background();
                if (bg.style() != Qt::NoBrush)
                    out += QStringLiteral("<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"%1\"/>")
                               .arg(bg.color().name(QColor::HexRgb).mid(1).toUpper());
                out += QStringLiteral("</w:tcPr>");
                if (continuation) {
                    out += QStringLiteral("<w:p/>");
                } else {
                    bool lastWasPara = false;
                    for (auto it = cell.begin(); !it.atEnd(); ++it) {
                        if (QTextFrame *f = it.currentFrame()) {
                            frameChild(f);
                            lastWasPara = false;
                        } else {
                            paragraph(it.currentBlock());
                            lastWasPara = true;
                        }
                    }
                    if (!lastWasPara)
                        out += QStringLiteral("<w:p/>");
                }
                out += QStringLiteral("</w:tc>");
            }
            out += QStringLiteral("</w:tr>");
        }
        out += QStringLiteral("</w:tbl>");
    }

    void frameChild(QTextFrame *f)
    {
        if (auto *t = qobject_cast<QTextTable *>(f))
            table(t);
        else
            frame(f);
    }

    void frame(QTextFrame *f)
    {
        for (auto it = f->begin(); !it.atEnd(); ++it) {
            if (QTextFrame *child = it.currentFrame())
                frameChild(child);
            else
                paragraph(it.currentBlock());
        }
    }

    // ----------------------------------------------------------- numbering

    static QString abstractNumXml(const NewList &nl)
    {
        const bool bullet = nl.style == QTextListFormat::ListDisc || nl.style == QTextListFormat::ListCircle
                            || nl.style == QTextListFormat::ListSquare;
        static const QString bullets[3] = {QStringLiteral("●"), QStringLiteral("○"), QStringLiteral("■")};
        int first = 0;
        if (nl.style == QTextListFormat::ListCircle)
            first = 1;
        else if (nl.style == QTextListFormat::ListSquare)
            first = 2;
        QString firstFmt = QStringLiteral("decimal");
        switch (nl.style) {
        case QTextListFormat::ListLowerAlpha: firstFmt = QStringLiteral("lowerLetter"); break;
        case QTextListFormat::ListUpperAlpha: firstFmt = QStringLiteral("upperLetter"); break;
        case QTextListFormat::ListLowerRoman: firstFmt = QStringLiteral("lowerRoman"); break;
        case QTextListFormat::ListUpperRoman: firstFmt = QStringLiteral("upperRoman"); break;
        default: break;
        }
        static const QString cycle[3] = {QStringLiteral("decimal"), QStringLiteral("lowerLetter"),
                                         QStringLiteral("lowerRoman")};
        QString s = QStringLiteral("<w:abstractNum w:abstractNumId=\"%1\"><w:multiLevelType w:val=\"hybridMultilevel\"/>")
                        .arg(nl.absId);
        for (int lvl = 0; lvl < 9; ++lvl) {
            const int left = 720 * (lvl + 1);
            if (bullet) {
                s += QStringLiteral("<w:lvl w:ilvl=\"%1\"><w:start w:val=\"1\"/><w:numFmt w:val=\"bullet\"/>"
                                    "<w:lvlText w:val=\"%2\"/><w:lvlJc w:val=\"left\"/>"
                                    "<w:pPr><w:ind w:left=\"%3\" w:hanging=\"360\"/></w:pPr></w:lvl>")
                         .arg(lvl)
                         .arg(bullets[(first + lvl) % 3])
                         .arg(left);
            } else {
                const QString fmt = lvl == 0 ? firstFmt : cycle[lvl % 3];
                // lvlText is "%N." with a literal percent. QString::arg has no %% escape and
                // chained arg() calls would re-substitute it, so use one multi-arg call.
                const QString lvlText = QLatin1Char('%') + QString::number(lvl + 1) + QLatin1Char('.');
                s += QStringLiteral("<w:lvl w:ilvl=\"%1\"><w:start w:val=\"1\"/><w:numFmt w:val=\"%2\"/>"
                                    "<w:lvlText w:val=\"%3\"/><w:lvlJc w:val=\"left\"/>"
                                    "<w:pPr><w:ind w:left=\"%4\" w:hanging=\"360\"/></w:pPr></w:lvl>")
                         .arg(QString::number(lvl), fmt, lvlText, QString::number(left));
            }
        }
        s += QStringLiteral("</w:abstractNum>");
        return s;
    }
};

QString ensureNs(QString rootTag, const QString &prefix, const QString &uri)
{
    if (rootTag.contains(QStringLiteral("xmlns:%1=").arg(prefix)))
        return rootTag;
    const int gt = rootTag.lastIndexOf(QLatin1Char('>'));
    rootTag.insert(gt, QStringLiteral(" xmlns:%1=\"%2\"").arg(prefix, uri));
    return rootTag;
}

QString relsXml(const QList<Relationship> &rels)
{
    QString s = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                               "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
    for (const Relationship &r : rels) {
        s += QStringLiteral("<Relationship Id=\"%1\" Type=\"%2\" Target=\"%3\"")
                 .arg(xmlAttrEsc(r.id), xmlAttrEsc(r.type), xmlAttrEsc(r.target));
        if (!r.targetMode.isEmpty())
            s += QStringLiteral(" TargetMode=\"%1\"").arg(xmlAttrEsc(r.targetMode));
        s += QStringLiteral("/>");
    }
    s += QStringLiteral("</Relationships>");
    return s;
}

QString ensureDefaultType(QString ct, const QString &ext, const QString &mime)
{
    const QRegularExpression re(QStringLiteral("Extension=\"%1\"").arg(ext), QRegularExpression::CaseInsensitiveOption);
    if (re.match(ct).hasMatch())
        return ct;
    const int close = ct.lastIndexOf(QLatin1String("</Types>"));
    if (close < 0)
        return ct;
    ct.insert(close, QStringLiteral("<Default Extension=\"%1\" ContentType=\"%2\"/>").arg(ext, mime));
    return ct;
}

QString ensureOverride(QString ct, const QString &part, const QString &mime)
{
    if (ct.contains(QStringLiteral("PartName=\"/%1\"").arg(part)))
        return ct;
    const int close = ct.lastIndexOf(QLatin1String("</Types>"));
    if (close < 0)
        return ct;
    ct.insert(close, QStringLiteral("<Override PartName=\"/%1\" ContentType=\"%2\"/>").arg(part, mime));
    return ct;
}

} // namespace

QByteArray DocxWriter::build(QTextDocument *doc, Package &pkg, QString *error)
{
    Writer w(doc, pkg);
    w.frame(doc->rootFrame());

    // ---- document.xml
    QString root = pkg.rootStartTag;
    if (root.isEmpty() || !root.startsWith(QLatin1String("<w:document")))
        root = QStringLiteral("<w:document xmlns:w=\"%1\">").arg(kNsW);
    root = ensureNs(root, QStringLiteral("w"), kNsW);
    root = ensureNs(root, QStringLiteral("r"), kNsR);
    root = ensureNs(root, QStringLiteral("wp"), kNsWp);

    QString sect = pkg.sectPrXml;
    if (sect.isEmpty()) {
        const PageSetup &p = pkg.page;
        sect = QStringLiteral("<w:sectPr><w:pgSz w:w=\"%1\" w:h=\"%2\"/>"
                              "<w:pgMar w:top=\"%3\" w:right=\"%4\" w:bottom=\"%5\" w:left=\"%6\" "
                              "w:header=\"720\" w:footer=\"720\" w:gutter=\"0\"/></w:sectPr>")
                   .arg(p.width)
                   .arg(p.height)
                   .arg(p.top)
                   .arg(p.right)
                   .arg(p.bottom)
                   .arg(p.left);
    }
    const QString docXml = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n") + root
                           + QStringLiteral("<w:body>") + w.out + sect + QStringLiteral("</w:body></w:document>");

    // ---- numbering for lists created in the editor
    QString numberingPart = pkg.partForRelType(QStringLiteral("/numbering"));
    QByteArray numberingXml = numberingPart.isEmpty() ? QByteArray() : pkg.parts.value(numberingPart);
    bool numberingCreated = false;
    if (!w.newLists.isEmpty()) {
        QString abs, nums;
        QList<NewList> ordered = w.newLists.values();
        std::sort(ordered.begin(), ordered.end(), [](const NewList &a, const NewList &b) { return a.numId < b.numId; });
        for (const NewList &nl : ordered) {
            abs += Writer::abstractNumXml(nl);
            nums += QStringLiteral("<w:num w:numId=\"%1\"><w:abstractNumId w:val=\"%2\"/></w:num>")
                        .arg(nl.numId)
                        .arg(nl.absId);
        }
        QString nx = QString::fromUtf8(numberingXml);
        if (nx.isEmpty()) {
            numberingCreated = true;
            numberingPart = pkg.mainDir() + QStringLiteral("/numbering.xml");
            nx = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                                "<w:numbering xmlns:w=\"%1\"></w:numbering>")
                     .arg(kNsW);
        }
        static const QRegularExpression firstNum(QStringLiteral("<w:num[\\s>]"));
        const auto m = firstNum.match(nx);
        const int close = nx.lastIndexOf(QLatin1String("</w:numbering>"));
        if (close < 0) {
            if (error)
                *error = QStringLiteral("numbering.xml is malformed");
            return QByteArray();
        }
        nx.insert(close, nums);
        nx.insert(m.hasMatch() ? int(m.capturedStart()) : close, abs);
        numberingXml = nx.toUtf8();
    }

    // ---- relationships
    QList<Relationship> rels;
    for (const Relationship &r : pkg.docRels)
        if (r.type != kRelImage && r.type != kRelHyperlink)
            rels.append(r);
    rels.append(w.newRels);
    if (numberingCreated) {
        Relationship nr{w.newRelId(), kRelNumbering, w.relTargetFor(numberingPart), QString()};
        rels.append(nr);
    }

    // ---- content types
    QString ct = QString::fromUtf8(pkg.parts.value(QStringLiteral("[Content_Types].xml")));
    for (const QString &part : w.usedMedia) {
        const QString ext = part.section(QLatin1Char('.'), -1).toLower();
        if (ext == QLatin1String("png"))
            ct = ensureDefaultType(ct, ext, QStringLiteral("image/png"));
        else if (ext == QLatin1String("jpg") || ext == QLatin1String("jpeg"))
            ct = ensureDefaultType(ct, ext, QStringLiteral("image/jpeg"));
        else if (ext == QLatin1String("gif"))
            ct = ensureDefaultType(ct, ext, QStringLiteral("image/gif"));
        else if (ext == QLatin1String("bmp"))
            ct = ensureDefaultType(ct, ext, QStringLiteral("image/bmp"));
        else if (ext == QLatin1String("emf"))
            ct = ensureDefaultType(ct, ext, QStringLiteral("image/x-emf"));
        else if (ext == QLatin1String("wmf"))
            ct = ensureDefaultType(ct, ext, QStringLiteral("image/x-wmf"));
        else if (ext == QLatin1String("svg"))
            ct = ensureDefaultType(ct, ext, QStringLiteral("image/svg+xml"));
    }
    if (numberingCreated)
        ct = ensureOverride(ct, numberingPart,
                            QStringLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.numbering+xml"));

    // ---- media that the document used to reference and no longer does
    QSet<QString> dropMedia;
    for (const Relationship &r : pkg.docRels) {
        if (r.type != kRelImage || r.targetMode == QLatin1String("External"))
            continue;
        const QString part = pkg.resolveTarget(r.target);
        if (w.usedMedia.contains(part))
            continue;
        const QString file = part.section(QLatin1Char('/'), -1);
        bool referencedElsewhere = false;
        for (const QString &name : pkg.partOrder) {
            if (name.endsWith(QLatin1String(".rels")) && name != pkg.mainRelsPath()
                && pkg.parts.value(name).contains(file.toUtf8())) {
                referencedElsewhere = true;
                break;
            }
        }
        if (!referencedElsewhere)
            dropMedia.insert(part);
    }

    // ---- assemble
    const QString relsPath = pkg.mainRelsPath();
    QHash<QString, QByteArray> replaced;
    replaced.insert(QStringLiteral("[Content_Types].xml"), ct.toUtf8());
    replaced.insert(pkg.mainPart, docXml.toUtf8());
    replaced.insert(relsPath, relsXml(rels).toUtf8());
    if (!numberingXml.isEmpty())
        replaced.insert(numberingPart, numberingXml);

    QList<zip::Entry> entries;
    QSet<QString> written;
    auto add = [&](const QString &name, const QByteArray &data) {
        if (written.contains(name))
            return;
        written.insert(name);
        entries.append({name, data});
    };
    add(QStringLiteral("[Content_Types].xml"), replaced.value(QStringLiteral("[Content_Types].xml")));
    for (const QString &name : pkg.partOrder) {
        if (dropMedia.contains(name))
            continue;
        add(name, replaced.contains(name) ? replaced.value(name) : pkg.parts.value(name));
    }
    for (auto it = replaced.cbegin(); it != replaced.cend(); ++it)
        add(it.key(), it.value());
    for (auto it = w.newParts.cbegin(); it != w.newParts.cend(); ++it)
        add(it.key(), it.value());

    const QByteArray zipped = zip::write(entries);

    // Stamp lists created in the editor with the numbering they were saved as,
    // so the next save reuses it instead of allocating another definition.
    for (auto it = w.newLists.cbegin(); it != w.newLists.cend(); ++it) {
        auto *list = const_cast<QTextList *>(it.key());
        QTextListFormat lf = list->format();
        lf.setProperty(NumId, it->numId);
        lf.setProperty(Ilvl, qMax(0, lf.indent() - 1));
        lf.setProperty(NumFromStyle, false);
        list->setFormat(lf);
    }

    // Keep the in-memory package in step with what was written.
    Package updated;
    QString err;
    if (updated.loadFrom(zipped, &err)) {
        pkg = updated;
    }
    return zipped;
}

QByteArray DocxWriter::buildPart(QTextDocument *doc, Package &pkg, bool footer)
{
    Writer w(doc, pkg);
    w.frame(doc->rootFrame());
    const QString tag = footer ? QStringLiteral("w:ftr") : QStringLiteral("w:hdr");
    QString body = w.out;
    if (body.isEmpty())
        body = QStringLiteral("<w:p/>");
    return (QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<%1 xmlns:w=\"%2\" "
                           "xmlns:r=\"%3\">")
                .arg(tag, kNsW, kNsR)
            + body + QStringLiteral("</%1>").arg(tag))
        .toUtf8();
}

bool DocxWriter::saveFile(const QString &path, QTextDocument *doc, Package &pkg, QString *error)
{
    const QByteArray data = build(doc, pkg, error);
    if (data.isEmpty())
        return false;
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    f.write(data);
    if (!f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}

} // namespace docx
