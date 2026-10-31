#include "docx/DocxReader.h"
#include "docx/Convert.h"
#include "docx/Units.h"
#include "docx/Xml.h"

#include <QFile>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QRegularExpression>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextList>
#include <QTextOption>
#include <QTextTable>
#include <QUrl>

namespace docx {

namespace {

const XElem *findDesc(const XElem *e, QStringView name)
{
    if (!e)
        return nullptr;
    for (const auto &k : e->kids) {
        if (k->name == name)
            return k.get();
        if (const XElem *f = findDesc(k.get(), name))
            return f;
    }
    return nullptr;
}

QTextListFormat::Style listStyleFor(const NumLevel &lv)
{
    const QString &f = lv.fmt;
    if (f == QLatin1String("decimal") || f == QLatin1String("decimalZero"))
        return QTextListFormat::ListDecimal;
    if (f == QLatin1String("lowerLetter"))
        return QTextListFormat::ListLowerAlpha;
    if (f == QLatin1String("upperLetter"))
        return QTextListFormat::ListUpperAlpha;
    if (f == QLatin1String("lowerRoman"))
        return QTextListFormat::ListLowerRoman;
    if (f == QLatin1String("upperRoman"))
        return QTextListFormat::ListUpperRoman;
    if (f == QLatin1String("bullet")) {
        const QString t = lv.text;
        if (t == QLatin1String("o") || t == QStringLiteral("○") || t == QStringLiteral("◦"))
            return QTextListFormat::ListCircle;
        if (t == QStringLiteral("▪") || t == QStringLiteral("■") || t == QStringLiteral("")
            || t == QStringLiteral("§"))
            return QTextListFormat::ListSquare;
        return QTextListFormat::ListDisc;
    }
    return QTextListFormat::ListDecimal;  // other numbered formats render as decimal
}

struct Ctx {
    const Package &pkg;
    QTextDocument *doc;
    bool needFirst = true;        // reuse the empty block at cursor instead of inserting
    bool pendingPageBreak = false;
    bool splitPending = false;    // page break seen mid-paragraph; split before next text
    QString href;
    QList<bool> fieldInResult;    // field stack: false = instructions, true = result
    QStringList fieldInstr;       // instruction text, parallel to fieldInResult
    const QList<Relationship> *partRels = nullptr;  // header/footer: the part's own relationships

    const Relationship *relFor(const QString &id) const
    {
        if (!partRels)
            return pkg.rel(id);
        for (const Relationship &r : *partRels)
            if (r.id == id)
                return &r;
        return nullptr;
    }

    // PAGE / NUMPAGES when inside such a field's result, else empty.
    QString currentField() const
    {
        if (fieldInResult.isEmpty() || !fieldInResult.last())
            return QString();
        const QString word = fieldInstr.last().trimmed().section(QLatin1Char(' '), 0, 0).toUpper();
        if (word == QLatin1String("PAGE") || word == QLatin1String("NUMPAGES") || word == QLatin1String("SECTIONPAGES"))
            return word == QLatin1String("SECTIONPAGES") ? QStringLiteral("NUMPAGES") : word;
        return QString();
    }
    QHash<QString, QTextList *> lists;  // "numId:ilvl"

    // Previous paragraph in the same container, for w:contextualSpacing.
    QTextBlock prevBlock;
    QString prevStyle;
    int prevBefore = 0, prevAfter = 0;

    Ctx(const Package &p, QTextDocument *d) : pkg(p), doc(d) {}

    bool inFieldInstr() const { return !fieldInResult.isEmpty() && !fieldInResult.last(); }

    void newBlock(QTextCursor &cur, const QTextBlockFormat &bf, const QTextCharFormat &cf)
    {
        if (needFirst) {
            cur.setBlockFormat(bf);
            cur.setBlockCharFormat(cf);
            needFirst = false;
        } else {
            cur.insertBlock(bf, cf);
        }
    }

    // Word lets a right/centre tab sit exactly on the margin. Qt rounds the frame
    // margins to whole pixels, so the real line is up to ~1px narrower and text
    // ending on such a stop wraps. Pull those stops in for display only; the
    // original twips are kept on the block and written back unless edited.
    void clampTabs(QTextBlockFormat &bf, const ParaProps &eff) const
    {
        QList<QTextOption::Tab> tabs = bf.tabPositions();
        if (tabs.isEmpty())
            return;
        const double avail = pkg.page.contentWidthPx() - bf.leftMargin() - bf.rightMargin();
        bool changed = false;
        for (QTextOption::Tab &t : tabs) {
            if (t.position > avail - kTabSliver) {
                t.position = avail - kTabSliver;
                changed = true;
            }
        }
        if (!changed)
            return;
        QList<TabStop> orig;
        for (const TabStop &t : eff.tabs)
            if (t.val == QLatin1String("left") || t.val == QLatin1String("right") || t.val == QLatin1String("center")
                || t.val == QLatin1String("decimal"))
                orig.append(TabStop{t.val, t.pos, QString()});
        bf.setTabPositions(tabs);
        bf.setProperty(TabsOrig, encodeTabStops(orig));
        bf.setProperty(TabsShown, tabsKey(tabs));
    }
    static constexpr double kTabSliver = 1.5;  // px

    // A page break followed by more content in the same paragraph starts a new block.
    void ensureSplit(QTextCursor &cur)
    {
        if (!splitPending)
            return;
        QTextBlockFormat bf = cur.blockFormat();
        bf.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
        cur.insertBlock(bf, cur.blockCharFormat());
        splitPending = false;
        pendingPageBreak = false;
    }

    void insertText(QTextCursor &cur, const QString &text, const QTextCharFormat &cf)
    {
        if (text.isEmpty())
            return;
        ensureSplit(cur);
        cur.insertText(text, cf);
    }

    void body(const XElem *container, QTextCursor &cur)
    {
        for (const auto &k : container->kids) {
            const QString &n = k->name;
            if (n == QLatin1String("p"))
                paragraph(k.get(), cur);
            else if (n == QLatin1String("tbl"))
                table(k.get(), cur);
            else if (n == QLatin1String("sdt")) {
                if (const XElem *c = k->child(u"sdtContent"))
                    body(c, cur);
            } else if (n == QLatin1String("customXml"))
                body(k.get(), cur);
        }
    }

    void paragraph(const XElem *p, QTextCursor &cur)
    {
        const XElem *pPr = p->child(u"pPr");
        const ParaProps direct = ParaProps::parse(pPr);
        QString styleId;
        if (pPr)
            if (const XElem *ps = pPr->child(u"pStyle"))
                styleId = ps->attr(u"val");
        if (styleId.isEmpty() || !pkg.styles.style(styleId))
            styleId = pkg.styles.defaultParagraphStyle();

        const ParaProps styleProps = pkg.styles.resolvedPara(styleId);
        const int numId = direct.numId ? *direct.numId : styleProps.numId.value_or(0);
        const int ilvl = direct.ilvl ? *direct.ilvl : styleProps.ilvl.value_or(0);
        const bool numFromStyle = !direct.numId.has_value();
        const bool isList = pkg.numbering.isRealList(numId, ilvl);

        ParaProps eff = styleProps;
        if (isList)
            eff.merge(pkg.numbering.level(numId, ilvl)->p);
        eff.merge(direct);

        QTextBlockFormat bf = toBlockFormat(eff, isList);
        bf.setProperty(PStyle, styleId);
        clampTabs(bf, eff);
        if (pendingPageBreak) {
            bf.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
            pendingPageBreak = false;
        }
        splitPending = false;

        const RunProps paraRun = pkg.styles.resolvedParaRun(styleId);
        RunProps markRun = paraRun;
        if (pPr)
            markRun.merge(RunProps::parse(pPr->child(u"rPr")));
        newBlock(cur, bf, toCharFormat(markRun));
        contextual(cur, styleId, eff);

        if (isList) {
            const QString key = QStringLiteral("%1:%2").arg(numId).arg(ilvl);
            QTextList *list = lists.value(key);
            if (list) {
                list->add(cur.block());
            } else {
                const NumLevel *lv = pkg.numbering.level(numId, ilvl);
                QTextListFormat lf;
                lf.setStyle(listStyleFor(*lv));
                lf.setIndent(0);
                QString suffix = lv->text;
                suffix.remove(QRegularExpression(QStringLiteral("%\\d")));
                if (lv->fmt != QLatin1String("bullet"))
                    lf.setNumberSuffix(suffix.isEmpty() ? QStringLiteral(".") : suffix);
                lf.setProperty(NumId, numId);
                lf.setProperty(Ilvl, ilvl);
                lf.setProperty(NumFromStyle, numFromStyle);
                list = cur.createList(lf);
                // createList may touch the block indent; restore the docx geometry.
                QTextBlockFormat fix = cur.blockFormat();
                fix.setIndent(0);
                fix.setLeftMargin(bf.leftMargin());
                fix.setTextIndent(0);
                cur.setBlockFormat(fix);
                lists.insert(key, list);
            }
        }

        runs(p, cur, paraRun);

        QTextBlockFormat lf = cur.blockFormat();
        compensateLineHeight(lf, dominantCharFormat(cur.block()));
        cur.setBlockFormat(lf);
    }

    // Word drops the space between adjacent paragraphs of the same style when the
    // style says so (List Bullet, List Paragraph...). Qt has no such rule, so zero
    // the margins for display and keep the docx values on the block for save.
    static void suppress(QTextBlock b, bool top, int before, int after)
    {
        QTextCursor c(b);
        QTextBlockFormat bf = b.blockFormat();
        if (top)
            bf.setTopMargin(0);
        else
            bf.setBottomMargin(0);
        bf.setProperty(SpacingOrig, QStringLiteral("%1,%2").arg(before).arg(after));
        bf.setProperty(SpacingShown,
                       QStringLiteral("%1,%2").arg(bf.topMargin(), 0, 'f', 3).arg(bf.bottomMargin(), 0, 'f', 3));
        c.setBlockFormat(bf);
    }

    void contextual(QTextCursor &cur, const QString &styleId, const ParaProps &eff)
    {
        const QTextBlock b = cur.block();
        const int before = eff.before.value_or(0), after = eff.after.value_or(0);
        if (prevBlock.isValid() && prevStyle == styleId) {
            if (prevBlock.blockFormat().boolProperty(ContextualSpacing) && prevAfter != 0)
                suppress(prevBlock, false, prevBefore, prevAfter);
            if (eff.contextualSpacing.value_or(false) && before != 0)
                suppress(b, true, before, after);
        }
        prevBlock = b;
        prevStyle = styleId;
        prevBefore = before;
        prevAfter = after;
    }

    void runs(const XElem *container, QTextCursor &cur, const RunProps &paraRun)
    {
        for (const auto &k : container->kids) {
            const QString &n = k->name;
            if (n == QLatin1String("r")) {
                run(k.get(), cur, paraRun);
            } else if (n == QLatin1String("hyperlink")) {
                const QString saved = href;
                const QString rid = k->attr(u"id");
                const QString anchor = k->attr(u"anchor");
                if (const Relationship *r = relFor(rid))
                    href = r->target;
                else if (!anchor.isEmpty())
                    href = QLatin1Char('#') + anchor;
                runs(k.get(), cur, paraRun);
                href = saved;
            } else if (n == QLatin1String("fldSimple")) {
                fieldInResult.append(true);
                fieldInstr.append(k->attr(u"instr"));
                runs(k.get(), cur, paraRun);
                fieldInResult.removeLast();
                fieldInstr.removeLast();
            } else if (n == QLatin1String("ins") || n == QLatin1String("smartTag") || n == QLatin1String("customXml")
                       || n == QLatin1String("dir") || n == QLatin1String("bdo")
                       || n == QLatin1String("moveTo")) {
                runs(k.get(), cur, paraRun);
            } else if (n == QLatin1String("sdt")) {
                if (const XElem *c = k->child(u"sdtContent"))
                    runs(c, cur, paraRun);
            } else if (n == QLatin1String("AlternateContent")) {
                if (const XElem *c = k->child(u"Choice"))
                    runs(c, cur, paraRun);
            }
        }
    }

    void run(const XElem *r, QTextCursor &cur, const RunProps &paraRun)
    {
        const XElem *rPr = r->child(u"rPr");
        RunProps rp = paraRun;
        QString rStyle;
        if (rPr)
            if (const XElem *rs = rPr->child(u"rStyle"))
                rStyle = rs->attr(u"val");
        if (!rStyle.isEmpty())
            rp.merge(pkg.styles.charStyleRun(rStyle));
        rp.merge(RunProps::parse(rPr));
        QTextCharFormat cf = toCharFormat(rp);
        if (!rStyle.isEmpty())
            cf.setProperty(RStyle, rStyle);
        if (!href.isEmpty()) {
            cf.setAnchor(true);
            cf.setAnchorHref(href);
        }

        for (const auto &k : r->kids) {
            const QString &n = k->name;
            if (n == QLatin1String("fldChar")) {
                const QString t = k->attr(u"fldCharType");
                if (t == QLatin1String("begin")) {
                    fieldInResult.append(false);
                    fieldInstr.append(QString());
                } else if (t == QLatin1String("separate") && !fieldInResult.isEmpty()) {
                    fieldInResult.last() = true;
                } else if (t == QLatin1String("end") && !fieldInResult.isEmpty()) {
                    fieldInResult.removeLast();
                    fieldInstr.removeLast();
                }
                continue;
            }
            if (inFieldInstr()) {
                if (n == QLatin1String("instrText"))
                    fieldInstr.last() += k->text;
                continue;
            }
            const QString field = currentField();
            if (field.isEmpty())
                cf.clearProperty(FieldInstr);
            else
                cf.setProperty(FieldInstr, field);
            if (n == QLatin1String("t")) {
                insertText(cur, k->text, cf);
            } else if (n == QLatin1String("tab") || n == QLatin1String("ptab")) {
                insertText(cur, QStringLiteral("\t"), cf);
            } else if (n == QLatin1String("br")) {
                const QString type = k->attr(u"type");
                if (type == QLatin1String("page")) {
                    pendingPageBreak = true;
                    splitPending = true;
                } else if (type != QLatin1String("column")) {
                    insertText(cur, QString(QChar::LineSeparator), cf);
                }
            } else if (n == QLatin1String("cr")) {
                insertText(cur, QString(QChar::LineSeparator), cf);
            } else if (n == QLatin1String("noBreakHyphen")) {
                insertText(cur, QStringLiteral("‑"), cf);
            } else if (n == QLatin1String("softHyphen")) {
                insertText(cur, QStringLiteral("­"), cf);
            } else if (n == QLatin1String("sym")) {
                bool ok = false;
                uint code = k->attr(u"char").toUInt(&ok, 16);
                if (ok) {
                    if (code >= 0xF000 && code <= 0xF0FF)
                        code -= 0xF000;  // symbol-font private-use range
                    insertText(cur, QString(QChar(char16_t(code))), cf);
                }
            } else if (n == QLatin1String("drawing")) {
                image(k.get(), cur, cf);
            } else if (n == QLatin1String("AlternateContent")) {
                if (const XElem *c = k->child(u"Choice"))
                    if (const XElem *d = findDesc(c, u"drawing"))
                        image(d, cur, cf);
            }
        }
    }

    void image(const XElem *drawing, QTextCursor &cur, const QTextCharFormat &cf)
    {
        const XElem *holder = drawing->child(u"inline");
        if (!holder)
            holder = drawing->child(u"anchor");
        if (!holder)
            return;
        const XElem *blip = findDesc(holder, u"blip");
        if (!blip)
            return;  // shapes / charts: not supported yet
        const Relationship *r = relFor(blip->attr(u"embed"));
        if (!r)
            return;
        const QString part = pkg.resolveTarget(r->target);
        const QByteArray bytes = pkg.parts.value(part);
        double w = 0, h = 0;
        if (const XElem *ext = holder->child(u"extent")) {
            w = emuToPx(ext->attr(u"cx").toDouble());
            h = emuToPx(ext->attr(u"cy").toDouble());
        }
        QImage img;
        img.loadFromData(bytes);
        if (img.isNull()) {
            // EMF/WMF/SVG etc: keep the original bytes on save, show a placeholder.
            const QSize sz(qMax(16, int(w)), qMax(16, int(h)));
            img = QImage(sz, QImage::Format_ARGB32);
            img.fill(QColor(0xEE, 0xEE, 0xEE));
            QPainter pt(&img);
            pt.setPen(QColor(0x99, 0x99, 0x99));
            pt.drawRect(0, 0, sz.width() - 1, sz.height() - 1);
            pt.drawText(img.rect(), Qt::AlignCenter, QStringLiteral("image"));
        }
        if (w <= 0 || h <= 0) {
            w = img.width();
            h = img.height();
        }
        const QString name = QStringLiteral("docx:") + part;
        doc->addResource(QTextDocument::ImageResource, QUrl(name), img);
        QTextImageFormat f;
        f.merge(cf);
        f.setName(name);
        f.setWidth(w);
        f.setHeight(h);
        ensureSplit(cur);
        cur.insertImage(f);
    }

    void table(const XElem *tbl, QTextCursor &cur)
    {
        QList<int> grid;
        if (const XElem *g = tbl->child(u"tblGrid"))
            for (const XElem *c : g->children(u"gridCol"))
                grid.append(c->intAttr(u"w"));

        struct CellPos {
            int row, col, span;
            const XElem *tc;
            int vmerge;  // 0 none, 1 restart, 2 continue
        };
        QList<CellPos> cells;
        QList<const XElem *> rows = tbl->children(u"tr");
        int nCols = 0;
        for (int r = 0; r < rows.size(); ++r) {
            int col = 0;
            if (const XElem *trPr = rows[r]->child(u"trPr"))
                if (const XElem *gb = trPr->child(u"gridBefore"))
                    col = gb->intAttr(u"val");
            for (const XElem *tc : rows[r]->children(u"tc")) {
                int span = 1;
                int vm = 0;
                if (const XElem *tcPr = tc->child(u"tcPr")) {
                    if (const XElem *gs = tcPr->child(u"gridSpan"))
                        span = qMax(1, gs->intAttr(u"val", 1));
                    if (const XElem *v = tcPr->child(u"vMerge"))
                        vm = v->attr(u"val") == QLatin1String("restart") ? 1 : 2;
                }
                cells.append({r, col, span, tc, vm});
                col += span;
            }
            nCols = qMax(nCols, col);
        }
        nCols = qMax(nCols, int(grid.size()));
        const int nRows = rows.size();
        if (nRows == 0 || nCols == 0)
            return;

        const XElem *tblPr = tbl->child(u"tblPr");
        QString tblStyle;
        bool borders = false;
        Qt::Alignment align = Qt::AlignLeft;
        if (tblPr) {
            if (const XElem *ts = tblPr->child(u"tblStyle"))
                tblStyle = ts->attr(u"val");
            if (const XElem *tb = tblPr->child(u"tblBorders")) {
                for (const auto &side : tb->kids) {
                    const QString v = side->attr(u"val");
                    if (!v.isEmpty() && v != QLatin1String("nil") && v != QLatin1String("none"))
                        borders = true;
                }
            } else if (tblStyle.contains(QLatin1String("Grid"), Qt::CaseInsensitive)) {
                borders = true;
            }
            if (const XElem *jc = tblPr->child(u"jc")) {
                const QString v = jc->attr(u"val");
                if (v == QLatin1String("center"))
                    align = Qt::AlignHCenter;
                else if (v == QLatin1String("right") || v == QLatin1String("end"))
                    align = Qt::AlignRight;
            }
        }

        QTextTableFormat tf;
        tf.setBorder(borders ? 1 : 0);
        tf.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
        tf.setBorderBrush(QBrush(QColor(0x80, 0x80, 0x80)));
        tf.setBorderCollapse(true);
        tf.setCellSpacing(0);
        tf.setCellPadding(4);
        tf.setAlignment(align);
        tf.setProperty(TableBorders, borders);
        if (!tblStyle.isEmpty())
            tf.setProperty(TableStyle, tblStyle);
        if (grid.size() == nCols) {
            QList<QTextLength> widths;
            for (int w : grid)
                widths.append(QTextLength(QTextLength::FixedLength, twipsToPx(w)));
            tf.setColumnWidthConstraints(widths);
        }

        QTextTable *table = cur.insertTable(nRows, nCols, tf);

        for (const CellPos &cp : cells) {
            if (cp.row >= nRows || cp.col >= nCols || cp.vmerge == 2)
                continue;
            QTextTableCell cell = table->cellAt(cp.row, cp.col);
            QTextCursor cc = cell.firstCursorPosition();
            const bool savedNeed = needFirst;
            needFirst = true;
            prevBlock = QTextBlock();
            body(cp.tc, cc);
            needFirst = savedNeed;
        }

        // Horizontal spans, then vertical merges.
        for (const CellPos &cp : cells) {
            if (cp.vmerge == 2 || cp.row >= nRows || cp.col >= nCols)
                continue;
            int rowSpan = 1;
            if (cp.vmerge == 1) {
                for (int r = cp.row + 1; r < nRows; ++r) {
                    bool cont = false;
                    for (const CellPos &o : cells)
                        if (o.row == r && o.col == cp.col && o.vmerge == 2)
                            cont = true;
                    if (!cont)
                        break;
                    ++rowSpan;
                }
            }
            const int colSpan = qMin(cp.span, nCols - cp.col);
            if (rowSpan > 1 || colSpan > 1)
                table->mergeCells(cp.row, cp.col, rowSpan, colSpan);
        }

        // Cell formats last: Qt stores a cell's format as the char format of its first
        // block, so setting paragraph formatting inside the cell would overwrite it.
        for (const CellPos &cp : cells) {
            if (cp.vmerge == 2 || cp.row >= nRows || cp.col >= nCols)
                continue;
            const XElem *tcPr = cp.tc->child(u"tcPr");
            const XElem *shd = tcPr ? tcPr->child(u"shd") : nullptr;
            if (!shd)
                continue;
            const QString fill = shd->attr(u"fill").toUpper();
            if (fill.isEmpty() || fill == QLatin1String("AUTO"))
                continue;
            QTextTableCell cell = table->cellAt(cp.row, cp.col);
            QTextTableCellFormat cfmt = cell.format().toTableCellFormat();
            cfmt.setBackground(QColor(QLatin1Char('#') + fill));
            cell.setFormat(cfmt);
        }

        cur = table->lastCursorPosition();
        cur.movePosition(QTextCursor::NextBlock);
        needFirst = true;
        prevBlock = QTextBlock();
    }
};

} // namespace

void DocxReader::applyPageSetup(const Package &pkg, QTextDocument *doc)
{
    doc->setIndentWidth(48.0);  // 0.5 inch per list/indent level, Word's default step
    // Plain tabs advance to the document's default tab stop (Word: 0.5"), not Qt's 80px.
    // A document property, not a format: no undo entry, no modified flag.
    QTextOption opt = doc->defaultTextOption();
    opt.setTabStopDistance(twipsToPx(pkg.defaultTabStop));
    doc->setDefaultTextOption(opt);
    doc->setDocumentMargin(0);
    QTextFrameFormat ff = doc->rootFrame()->frameFormat();
    ff.setLeftMargin(twipsToPx(pkg.page.left));
    ff.setRightMargin(twipsToPx(pkg.page.right));
    ff.setTopMargin(twipsToPx(pkg.page.top));
    ff.setBottomMargin(twipsToPx(pkg.page.bottom));
    doc->rootFrame()->setFrameFormat(ff);

    const RunProps normal = pkg.styles.resolvedParaRun(pkg.styles.defaultParagraphStyle());
    QFont f(normal.font.value_or(QString::fromLatin1(kDefaultFont)));
    f.setPointSizeF(normal.halfPts.value_or(kDefaultHalfPts) / 2.0);
    doc->setDefaultFont(f);
}

bool DocxReader::build(const Package &pkg, QTextDocument *doc, QString *error)
{
    auto root = parseXml(pkg.parts.value(pkg.mainPart), error);
    if (!root)
        return false;
    const XElem *body = root->child(u"body");
    if (!body) {
        if (error)
            *error = QStringLiteral("document has no body");
        return false;
    }

    const bool undo = doc->isUndoRedoEnabled();
    doc->setUndoRedoEnabled(false);
    doc->clear();
    applyPageSetup(pkg, doc);

    Ctx ctx(pkg, doc);
    QTextCursor cur(doc);
    cur.beginEditBlock();
    ctx.body(body, cur);
    if (ctx.needFirst && doc->blockCount() == 1) {
        // Empty document: give the single block Normal style formatting.
        const QString normal = pkg.styles.defaultParagraphStyle();
        QTextBlockFormat bf = toBlockFormat(pkg.styles.resolvedPara(normal), false);
        bf.setProperty(PStyle, normal);
        const QTextCharFormat cf = toCharFormat(pkg.styles.resolvedParaRun(normal));
        compensateLineHeight(bf, cf);
        cur.setBlockFormat(bf);
        cur.setBlockCharFormat(cf);
    }
    cur.endEditBlock();

    doc->setUndoRedoEnabled(undo);
    doc->setModified(false);
    return true;
}

bool DocxReader::buildPart(const Package &pkg, const QString &part, QTextDocument *doc, QString *error)
{
    auto root = parseXml(pkg.parts.value(part), error);
    if (!root)
        return false;
    const bool undo = doc->isUndoRedoEnabled();
    doc->setUndoRedoEnabled(false);
    doc->clear();
    applyPageSetup(pkg, doc);
    QTextFrameFormat ff = doc->rootFrame()->frameFormat();
    ff.setMargin(0);
    doc->rootFrame()->setFrameFormat(ff);
    doc->setTextWidth(twipsToPx(pkg.page.width - pkg.page.left - pkg.page.right));

    const QList<Relationship> rels = pkg.relsOf(part);
    Ctx ctx(pkg, doc);
    ctx.partRels = &rels;
    QTextCursor cur(doc);
    cur.beginEditBlock();
    ctx.body(root.get(), cur);
    cur.endEditBlock();
    doc->setUndoRedoEnabled(undo);
    doc->setModified(false);
    return true;
}

bool DocxReader::loadData(const QByteArray &data, QTextDocument *doc, Package *pkg, QString *error)
{
    Package p;
    if (!p.loadFrom(data, error))
        return false;
    if (!build(p, doc, error))
        return false;
    *pkg = p;
    return true;
}

bool DocxReader::loadFile(const QString &path, QTextDocument *doc, Package *pkg, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return loadData(f.readAll(), doc, pkg, error);
}

} // namespace docx
