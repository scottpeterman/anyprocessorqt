// Headless DOCX tests: builds content the way the editor does, saves, reloads,
// and checks what survived. Optional arguments are extra .docx files to
// round-trip and compare (reader signature before vs after save).
//
//   anyprocessorqt_tests [--keep out.docx] [file.docx ...]

#include "docx/Convert.h"
#include "docx/DocxReader.h"
#include "docx/DocxWriter.h"
#include "docx/Units.h"
#include "zip/Zip.h"
#include "spell/SpellChecker.h"
#include "spell/SpellHighlighter.h"
#include "ui/PageEdit.h"
#include "ui/Theme.h"
#include "ui/TablePropertiesDialog.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include "docx/TableFormat.h"
#include "ui/RecentFiles.h"
#include <QDir>
#include <QFileInfo>
#include "ui/HeaderFooterDialog.h"
#include "docx/HeaderFooter.h"
#include <QPdfWriter>
#include <QBuffer>
#include <QPageLayout>
#include <QPageSize>
#include <QScrollBar>
#include "ui/FindBar.h"
#include "ui/ZoomView.h"
#include <QGraphicsProxyWidget>
#include <QScrollBar>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include "docx/CodeStyles.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QMenu>
#include <QTimer>
#include <QStandardPaths>
#include <QTextLayout>
#include <memory>
#include <QImage>
#include <QPainter>
#include <QRegularExpression>
#include <QAbstractTextDocumentLayout>
#include <QTextBlock>
#include <QTextLayout>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextList>
#include <QTextTable>
#include <QFile>
#include <QFontMetricsF>
#include <QUrl>

#include <cstdio>

using namespace docx;

static int g_fail = 0;
static int g_pass = 0;

#define CHECK(cond, what)                                                        \
    do {                                                                         \
        if (cond) {                                                              \
            ++g_pass;                                                            \
        } else {                                                                 \
            ++g_fail;                                                            \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, what);  \
        }                                                                        \
    } while (0)

static QTextBlock findBlock(QTextDocument &doc, const QString &startsWith)
{
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next())
        if (b.text().startsWith(startsWith))
            return b;
    return QTextBlock();
}

static QTextCharFormat formatAt(const QTextBlock &b, const QString &word)
{
    const int off = b.text().indexOf(word);
    if (off < 0)
        return QTextCharFormat();
    for (auto it = b.begin(); !it.atEnd(); ++it) {
        const QTextFragment f = it.fragment();
        const int rel = f.position() - b.position();
        if (off >= rel && off < rel + f.length())
            return f.charFormat();
    }
    return QTextCharFormat();
}

static void setStyle(QTextCursor &c, const Package &pkg, const QString &id)
{
    QTextBlockFormat bf = toBlockFormat(pkg.styles.resolvedPara(id), false);
    bf.setProperty(PStyle, id);
    c.setBlockFormat(bf);
    c.setBlockCharFormat(toCharFormat(pkg.styles.resolvedParaRun(id)));
}

static QTextCharFormat styleChar(const Package &pkg, const QString &id)
{
    return toCharFormat(pkg.styles.resolvedParaRun(id));
}

// --------------------------------------------------------------------------
// Editor-built document
// --------------------------------------------------------------------------

static QByteArray buildEditorDoc(Package &pkg)
{
    QTextDocument doc;
    pkg = Package::blank();
    QString err;
    DocxReader::build(pkg, &doc, &err);
    QTextCursor c(&doc);

    setStyle(c, pkg, QStringLiteral("Heading1"));
    c.insertText(QStringLiteral("Heading One"), styleChar(pkg, QStringLiteral("Heading1")));

    c.insertBlock();
    setStyle(c, pkg, QStringLiteral("Normal"));
    QTextCharFormat normal = styleChar(pkg, QStringLiteral("Normal"));
    c.insertText(QStringLiteral("Plain "), normal);
    QTextCharFormat bold = normal;
    bold.setFontWeight(QFont::Bold);
    c.insertText(QStringLiteral("bold"), bold);
    c.insertText(QStringLiteral(" and "), normal);
    QTextCharFormat ital = normal;
    ital.setFontItalic(true);
    ital.setForeground(QColor(0xC0, 0x00, 0x00));
    c.insertText(QStringLiteral("red italic"), ital);
    c.insertText(QStringLiteral(" and "), normal);
    QTextCharFormat hl = normal;
    hl.setBackground(highlightColor(QStringLiteral("yellow")));
    hl.setProperty(Highlight, QStringLiteral("yellow"));
    c.insertText(QStringLiteral("highlighted"), hl);
    c.insertText(QStringLiteral(" text.\tTabbed") + QString(QChar::LineSeparator) + QStringLiteral("Second line."), normal);

    // Bulleted list with a nested item
    c.insertBlock();
    setStyle(c, pkg, QStringLiteral("Normal"));
    QTextListFormat bullets;
    bullets.setStyle(QTextListFormat::ListDisc);
    bullets.setIndent(1);
    c.createList(bullets);
    c.insertText(QStringLiteral("Bullet one"), normal);
    c.insertBlock();
    c.insertText(QStringLiteral("Bullet nested"), normal);
    QTextBlockFormat nested = c.blockFormat();
    nested.setIndent(1);
    c.setBlockFormat(nested);
    c.insertBlock();
    QTextBlockFormat back = c.blockFormat();
    back.setIndent(0);
    c.setBlockFormat(back);
    c.insertText(QStringLiteral("Bullet two"), normal);

    // Paragraph out of the list
    c.insertBlock();
    QTextBlockFormat plain = toBlockFormat(pkg.styles.resolvedPara(QStringLiteral("Normal")), false);
    plain.setProperty(PStyle, QStringLiteral("Normal"));
    plain.setObjectIndex(-1);
    c.setBlockFormat(plain);
    if (QTextList *l = c.currentList())
        l->remove(c.block());
    c.insertText(QStringLiteral("Between lists"), normal);

    // Numbered list
    c.insertBlock();
    QTextListFormat numbers;
    numbers.setStyle(QTextListFormat::ListDecimal);
    numbers.setIndent(1);
    numbers.setNumberSuffix(QStringLiteral("."));
    c.createList(numbers);
    c.insertText(QStringLiteral("Step one"), normal);
    c.insertBlock();
    c.insertText(QStringLiteral("Step two"), normal);

    // Hyperlink paragraph
    c.insertBlock();
    c.setBlockFormat(plain);
    if (QTextList *l = c.currentList())
        l->remove(c.block());
    c.insertText(QStringLiteral("See "), normal);
    QTextCharFormat link = normal;
    link.setAnchor(true);
    link.setAnchorHref(QStringLiteral("https://github.com/scottpeterman/anyprocessorqt"));
    link.setProperty(RStyle, QStringLiteral("Hyperlink"));
    link.setForeground(QColor(0x05, 0x63, 0xC1));
    link.setFontUnderline(true);
    c.insertText(QStringLiteral("the repo"), link);
    c.insertText(QStringLiteral(" for details."), normal);

    // Table 3x3: row 0 cols 0-1 merged, col 2 rows 1-2 merged, shaded header cell
    c.insertBlock();
    QTextTableFormat tf;
    tf.setBorder(1);
    tf.setBorderCollapse(true);
    tf.setCellPadding(4);
    tf.setCellSpacing(0);
    tf.setColumnWidthConstraints({QTextLength(QTextLength::FixedLength, 200), QTextLength(QTextLength::FixedLength, 200),
                                  QTextLength(QTextLength::FixedLength, 224)});
    QTextTable *t = c.insertTable(3, 3, tf);
    t->cellAt(0, 0).firstCursorPosition().insertText(QStringLiteral("Merged header"), bold);
    t->cellAt(0, 2).firstCursorPosition().insertText(QStringLiteral("H3"), bold);
    t->cellAt(1, 0).firstCursorPosition().insertText(QStringLiteral("a1"), normal);
    t->cellAt(1, 1).firstCursorPosition().insertText(QStringLiteral("b1"), normal);
    t->cellAt(1, 2).firstCursorPosition().insertText(QStringLiteral("tall"), normal);
    t->cellAt(2, 0).firstCursorPosition().insertText(QStringLiteral("a2"), normal);
    t->cellAt(2, 1).firstCursorPosition().insertText(QStringLiteral("b2"), normal);
    QTextTableCellFormat shade = t->cellAt(0, 2).format().toTableCellFormat();
    shade.setBackground(QColor(0xDD, 0xEB, 0xF7));
    t->cellAt(0, 2).setFormat(shade);
    t->mergeCells(0, 0, 1, 2);
    t->mergeCells(1, 2, 2, 1);

    // After the table: image + page break + final paragraph
    c = t->lastCursorPosition();
    c.movePosition(QTextCursor::NextBlock);
    c.setBlockFormat(plain);
    QImage img(120, 60, QImage::Format_RGB32);
    img.fill(QColor(0x33, 0x66, 0x99));
    {
        QPainter p(&img);
        p.fillRect(10, 10, 40, 40, Qt::white);
    }
    doc.addResource(QTextDocument::ImageResource, QUrl(QStringLiteral("image:test-1")), img);
    QTextImageFormat imf;
    imf.setName(QStringLiteral("image:test-1"));
    imf.setWidth(240);
    imf.setHeight(120);
    c.insertImage(imf);

    c.insertBlock();
    QTextBlockFormat pb = plain;
    pb.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
    c.setBlockFormat(pb);
    c.insertText(QStringLiteral("After the break."), normal);

    QByteArray first = DocxWriter::build(&doc, pkg, &err);
    CHECK(!first.isEmpty(), "editor doc builds");
    // Save a second time from the same live document: numbering must not grow.
    QByteArray second = DocxWriter::build(&doc, pkg, &err);
    zip::Reader z1, z2;
    z1.open(first);
    z2.open(second);
    const QString n1 = QString::fromUtf8(z1.read(QStringLiteral("word/numbering.xml")));
    const QString n2 = QString::fromUtf8(z2.read(QStringLiteral("word/numbering.xml")));
    CHECK(n1.count(QStringLiteral("<w:abstractNum ")) == 2, "two abstract numbering definitions written");
    CHECK(n2.count(QStringLiteral("<w:abstractNum ")) == 2, "second save does not add numbering definitions");
    CHECK(n1.contains(QStringLiteral("<w:lvlText w:val=\"%1.\"/>")) && n1.contains(QStringLiteral("<w:lvlText w:val=\"%9.\"/>")),
          "numbered lvlText is %N.");
    CHECK(n1.contains(QStringLiteral("w:val=\"\u25CF\"")), "bullet glyph written");
    int media1 = 0, media2 = 0;
    for (const QString &n : z1.names())
        media1 += n.contains(QLatin1String("/media/"));
    for (const QString &n : z2.names())
        media2 += n.contains(QLatin1String("/media/"));
    CHECK(media1 == 1 && media2 == 1, "exactly one media part across saves");
    return second;
}

static void checkEditorDoc(const QByteArray &data)
{
    QTextDocument doc;
    Package pkg;
    QString err;
    const bool ok = DocxReader::loadData(data, &doc, &pkg, &err);
    CHECK(ok, "editor doc reloads");
    if (!ok)
        return;

    const QTextBlock h = findBlock(doc, QStringLiteral("Heading One"));
    CHECK(h.isValid() && h.blockFormat().stringProperty(PStyle) == QLatin1String("Heading1"), "heading style kept");

    const QTextBlock p = findBlock(doc, QStringLiteral("Plain bold"));
    CHECK(p.isValid(), "formatted paragraph present");
    CHECK(p.text().contains(QLatin1Char('\t')), "tab preserved");
    CHECK(p.text().contains(QChar::LineSeparator), "line break preserved");
    CHECK(formatAt(p, QStringLiteral("bold")).fontWeight() >= QFont::Bold, "bold preserved");
    const QTextCharFormat it = formatAt(p, QStringLiteral("red italic"));
    CHECK(it.fontItalic() && it.foreground().color() == QColor(0xC0, 0, 0), "italic + color preserved");
    CHECK(formatAt(p, QStringLiteral("highlighted")).stringProperty(Highlight) == QLatin1String("yellow"),
          "highlight preserved");
    CHECK(formatAt(p, QStringLiteral("Plain")).fontWeight() < QFont::DemiBold, "plain text not bold");

    const QTextBlock b1 = findBlock(doc, QStringLiteral("Bullet one"));
    const QTextBlock bn = findBlock(doc, QStringLiteral("Bullet nested"));
    const QTextBlock b2 = findBlock(doc, QStringLiteral("Bullet two"));
    CHECK(b1.textList() && b1.textList()->format().style() == QTextListFormat::ListDisc, "bullet list kept");
    CHECK(b2.textList() && b1.textList() == b2.textList(), "bullets one/two in same list");
    CHECK(bn.textList() && bn.textList()->format().intProperty(Ilvl) == 1, "nested bullet at level 1");
    CHECK(!findBlock(doc, QStringLiteral("Between lists")).textList(), "paragraph between lists not in a list");
    const QTextBlock s1 = findBlock(doc, QStringLiteral("Step one"));
    const QTextBlock s2 = findBlock(doc, QStringLiteral("Step two"));
    CHECK(s1.textList() && s1.textList()->format().style() == QTextListFormat::ListDecimal, "numbered list kept");
    CHECK(s1.textList() && s1.textList() == s2.textList(), "numbered items share a list");
    CHECK(s1.textList() != b1.textList(), "numbered and bulleted lists are distinct");

    const QTextBlock l = findBlock(doc, QStringLiteral("See the repo"));
    const QTextCharFormat lf = formatAt(l, QStringLiteral("the repo"));
    CHECK(lf.isAnchor() && lf.anchorHref() == QLatin1String("https://github.com/scottpeterman/anyprocessorqt"),
          "hyperlink preserved");
    CHECK(!formatAt(l, QStringLiteral("See")).isAnchor(), "text before link not linked");

    QTextTable *table = nullptr;
    for (QTextFrame *f : doc.rootFrame()->childFrames())
        if ((table = qobject_cast<QTextTable *>(f)))
            break;
    CHECK(table != nullptr, "table present");
    if (table) {
        CHECK(table->rows() == 3 && table->columns() == 3, "table is 3x3");
        CHECK(table->cellAt(0, 0).columnSpan() == 2, "horizontal merge kept");
        CHECK(table->cellAt(1, 2).rowSpan() == 2, "vertical merge kept");
        CHECK(table->cellAt(0, 2).format().background().color() == QColor(0xDD, 0xEB, 0xF7), "cell shading kept");
        QTextCursor cc = table->cellAt(2, 1).firstCursorPosition();
        cc.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        CHECK(cc.selectedText() == QLatin1String("b2"), "cell text kept");
        CHECK(TblBorders::of(table->format()).detect() == TblBorders::All
                  && table->cellAt(2, 0).format().toTableCellFormat().leftBorder() > 0,
              "table borders kept (drawn per cell)");
    }

    int images = 0;
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next())
        for (auto i = b.begin(); !i.atEnd(); ++i)
            if (i.fragment().charFormat().isImageFormat()) {
                const QTextImageFormat f = i.fragment().charFormat().toImageFormat();
                ++images;
                CHECK(qAbs(f.width() - 240) < 0.5 && qAbs(f.height() - 120) < 0.5, "image size kept");
                const QImage img = doc.resource(QTextDocument::ImageResource, QUrl(f.name())).value<QImage>();
                CHECK(img.size() == QSize(120, 60) && img.pixelColor(30, 30) == Qt::white, "image pixels kept");
            }
    CHECK(images == 1, "one image");

    const QTextBlock after = findBlock(doc, QStringLiteral("After the break"));
    CHECK(after.isValid() && (after.blockFormat().pageBreakPolicy() & QTextFormat::PageBreak_AlwaysBefore),
          "page break kept");
}

// --------------------------------------------------------------------------
// Round trip of external documents
// --------------------------------------------------------------------------

static QStringList signature(QTextDocument &doc)
{
    QStringList out;
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
        const QTextBlockFormat bf = b.blockFormat();
        QString s = QStringLiteral("[%1] L%2 T%3 B%4 a%5 %6 %7")
                        .arg(bf.stringProperty(PStyle))
                        .arg(bf.leftMargin(), 0, 'f', 1)
                        .arg(bf.topMargin(), 0, 'f', 1)
                        .arg(bf.bottomMargin(), 0, 'f', 1)
                        .arg(int(bf.alignment()))
                        .arg(bf.stringProperty(BorderLeft) + bf.stringProperty(BorderBottom))
                        .arg(b.textList() ? QStringLiteral("list%1").arg(int(b.textList()->format().style())) : QString());
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat cf = it.fragment().charFormat();
            const RunProps r = fromCharFormat(cf);
            s += QStringLiteral(" {%1|%2|%3%4%5|%6}")
                     .arg(it.fragment().text(), *r.font)
                     .arg(*r.halfPts)
                     .arg(*r.b ? QStringLiteral("B") : QString())
                     .arg(*r.i ? QStringLiteral("I") : QString())
                     .arg(*r.color);
        }
        out << s;
    }
    return out;
}

static void roundTripFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        CHECK(false, qPrintable(QStringLiteral("open %1").arg(path)));
        return;
    }
    const QByteArray original = f.readAll();
    QTextDocument a, b;
    Package pa, pb;
    QString err;
    const bool okA = DocxReader::loadData(original, &a, &pa, &err);
    CHECK(okA, qPrintable(QStringLiteral("load %1").arg(path)));
    if (!okA)
        return;
    const QByteArray saved = DocxWriter::build(&a, pa, &err);
    const bool okB = DocxReader::loadData(saved, &b, &pb, &err);
    CHECK(okB, qPrintable(QStringLiteral("reload %1").arg(path)));
    if (!okB)
        return;
    const QStringList sa = signature(a), sb = signature(b);
    CHECK(sa.size() == sb.size(), qPrintable(QStringLiteral("%1 block count").arg(path)));
    int diffs = 0;
    for (int i = 0; i < qMin(sa.size(), sb.size()); ++i) {
        if (sa[i] != sb[i]) {
            if (diffs++ < 3)
                std::fprintf(stderr, "  block %d\n    %s\n    %s\n", i, qPrintable(sa[i].left(300)),
                             qPrintable(sb[i].left(300)));
        }
    }
    CHECK(diffs == 0, qPrintable(QStringLiteral("%1 blocks identical after save (%2 differ)").arg(path).arg(diffs)));
    // Cell backgrounds must match too (they live outside block formats).
    auto shades = [](QTextDocument &d) {
        QStringList out;
        for (QTextFrame *f : d.rootFrame()->childFrames())
            if (auto *t = qobject_cast<QTextTable *>(f))
                for (int r = 0; r < t->rows(); ++r)
                    for (int c = 0; c < t->columns(); ++c) {
                        const QBrush bg = t->cellAt(r, c).format().background();
                        if (bg.style() != Qt::NoBrush)
                            out << QStringLiteral("%1,%2=%3").arg(r).arg(c).arg(bg.color().name());
                    }
        return out;
    };
    CHECK(shades(a) == shades(b), qPrintable(QStringLiteral("%1 cell shading identical after save").arg(path)));
    std::printf("roundtrip %s: %lld blocks, %d differ\n", qPrintable(path), static_cast<long long>(sa.size()), diffs);
}

// Expectations for tests/fixtures/features.docx (python-docx, Word default template).
static void fixtureChecks(const QString &path)
{
    QTextDocument doc;
    Package pkg;
    QString err;
    if (!DocxReader::loadFile(path, &doc, &pkg, &err)) {
        CHECK(false, "fixture loads");
        return;
    }
    const QTextBlock b1 = findBlock(doc, QStringLiteral("First bullet"));
    const QTextBlock b2 = findBlock(doc, QStringLiteral("Second bullet"));
    CHECK(b1.textList() && b1.textList()->format().boolProperty(NumFromStyle), "style-driven bullet list");
    CHECK(b1.textList() == b2.textList(), "style-driven bullets share a list");
    CHECK(b1.blockFormat().bottomMargin() == 0 && b2.blockFormat().topMargin() == 0,
          "contextual spacing collapses space between list items");
    const QTextBlock s1 = findBlock(doc, QStringLiteral("Step one"));
    CHECK(s1.textList() && s1.textList()->format().style() == QTextListFormat::ListDecimal, "List Number is decimal");
    // Lay out at the real page width (otherwise QTextDocument never wraps).
    doc.setTextWidth(twipsToPx(pkg.page.width));
    doc.documentLayout()->documentSize();
    const QTextBlock role = findBlock(doc, QStringLiteral("Role at Company"));
    CHECK(role.layout() && role.layout()->lineCount() == 1, "right tab on margin stays on one line");
    const QTextBlock longPara = findBlock(doc, QStringLiteral("A left-border callout"));
    CHECK(longPara.layout() && longPara.layout()->lineCount() >= 1, "layout ran");
    const QTextBlock callout = findBlock(doc, QStringLiteral("A left-border callout"));
    CHECK(!callout.blockFormat().stringProperty(BorderLeft).isEmpty(), "callout border read");
    QTextTable *table = nullptr;
    for (QTextFrame *f : doc.rootFrame()->childFrames())
        if ((table = qobject_cast<QTextTable *>(f)))
            break;
    CHECK(table && table->cellAt(0, 2).format().background().color() == QColor(0xDD, 0xEB, 0xF7), "fixture cell shading");
    CHECK(table && table->cellAt(0, 0).columnSpan() == 2 && table->cellAt(1, 2).rowSpan() == 2, "fixture merges");
    const QTextBlock link = findBlock(doc, QStringLiteral("See the repo"));
    CHECK(formatAt(link, QStringLiteral("the repo")).anchorHref().startsWith(QLatin1String("https://github.com/")),
          "fixture hyperlink");
    const QTextBlock p2 = findBlock(doc, QStringLiteral("First line on page two"));
    CHECK(p2.blockFormat().pageBreakPolicy() & QTextFormat::PageBreak_AlwaysBefore, "fixture page break");

    // Display line height is compensated to Word's pitch, but never written to the file.
    const QTextBlock plainPara = findBlock(doc, QStringLiteral("Plain,"));
    CHECK(plainPara.blockFormat().hasProperty(LineOrig), "line height compensated for display");
    if (const QTextLayout *pl = plainPara.layout(); pl && pl->lineCount() >= 1) {
        const QFontMetricsF fm(dominantCharFormat(plainPara).font());
        CHECK(pl->lineAt(0).height() > 0, "compensated line laid out");
    }

    // Saving must write the original spacing back, not the collapsed display value.
    const QByteArray saved = DocxWriter::build(&doc, pkg, &err);
    zip::Reader z;
    z.open(saved);
    const QString xml = QString::fromUtf8(z.read(QStringLiteral("word/document.xml")));
    const int at = xml.indexOf(QStringLiteral("First bullet"));
    const int pStart = xml.lastIndexOf(QStringLiteral("<w:p>"), at);
    CHECK(at > 0 && !xml.mid(pStart, at - pStart).contains(QStringLiteral("w:spacing")),
          "collapsed list spacing is not written as direct formatting");
    const int pat = xml.indexOf(QStringLiteral("Plain, "));
    const int pps = xml.lastIndexOf(QStringLiteral("<w:p>"), pat);
    CHECK(pat > 0 && !xml.mid(pps, pat - pps).contains(QStringLiteral("w:line=")),
          "compensated line height is not written to the file");
}

static void spellTests()
{
    spell::SpellChecker &sc = spell::SpellChecker::instance();
    CHECK(sc.setLanguage(QStringLiteral("en_US")), "bundled en_US loads");
    CHECK(sc.isCorrect(QStringLiteral("hello")) && sc.isCorrect(QStringLiteral("Hello")), "known word accepted");
    CHECK(!sc.isCorrect(QStringLiteral("recieved")), "misspelling rejected");
    CHECK(sc.suggestions(QStringLiteral("recieved")).contains(QStringLiteral("received")), "suggestion offered");
    CHECK(sc.isCorrect(QStringLiteral("Junos")) && sc.isCorrect(QStringLiteral("failover")), "tech supplement loaded");
    CHECK(sc.isCorrect(QStringLiteral("don't")), "apostrophe word accepted");

    const QString sample = QStringLiteral("Teh BGP NetBox PyQt6 netlapse.yaml https://x.io/a b@c.com edge1-01.fra1 multi-vendr it\u2019s fine.");
    QStringList got;
    for (const spell::Word &w : spell::SpellChecker::words(sample))
        got << w.text;
    CHECK(got == QStringList({QStringLiteral("Teh"), QStringLiteral("multi"), QStringLiteral("vendr"), QStringLiteral("it's"),
                              QStringLiteral("fine")}),
          qPrintable(QStringLiteral("tokenizer skips acronyms/names/paths/urls: got %1").arg(got.join(QLatin1Char('|')))));

    sc.addToPersonal(QStringLiteral("Penguinish"));
    CHECK(sc.isCorrect(QStringLiteral("Penguinish")), "personal word accepted");
    QFile pd(sc.personalDictionaryPath());
    CHECK(pd.open(QIODevice::ReadOnly) && pd.readAll().contains("Penguinish"), "personal word persisted");

    // Highlighting is display-only: no document change, undo or saved formatting.
    QTextDocument doc;
    Package pkg = Package::blank();
    QString err;
    DocxReader::build(pkg, &doc, &err);
    QTextCursor(&doc).insertText(QStringLiteral("Teh quick brown fox"));
    doc.setModified(false);
    doc.clearUndoRedoStacks();
    spell::SpellHighlighter hl(&doc);
    hl.recheck();
    sc.ignore(QStringLiteral("zzzqx"));  // any word-list change re-checks every open document
    bool underlined = false;
    for (const QTextLayout::FormatRange &r : doc.begin().layout()->formats())
        if (r.start == 0 && r.length == 3 && r.format.underlineStyle() == QTextCharFormat::SpellCheckUnderline)
            underlined = true;
    CHECK(underlined, "misspelling underlined");
    CHECK(!doc.isModified() && doc.availableUndoSteps() == 0, "highlighting leaves document unmodified");
    zip::Reader z;
    z.open(DocxWriter::build(&doc, pkg, &err));
    CHECK(!QString::fromUtf8(z.read(QStringLiteral("word/document.xml"))).contains(QStringLiteral("<w:u ")),
          "spell underline not saved");
}

static void viewTests(const QString &file)
{
    PageEdit edit;
    Package pkg;
    QString err;
    if (!DocxReader::loadFile(file, edit.document(), &pkg, &err)) {
        CHECK(false, "view test doc loads");
        return;
    }
    edit.setPageSetup(pkg.page);
    edit.resize(1200, 800);
    edit.show();
    QApplication::processEvents();
    const QByteArray before = DocxWriter::build(edit.document(), pkg, &err);
    edit.document()->setModified(false);
    edit.setViewMode(PageEdit::ViewMode::Draft);
    QApplication::processEvents();
    CHECK(edit.document()->textWidth() > twipsToPx(pkg.page.width), "draft view widens the text");
    CHECK(!edit.document()->isModified() && edit.document()->availableUndoSteps() == 0,
          "switching to draft does not modify the document");
    edit.setViewMode(PageEdit::ViewMode::Page);
    QApplication::processEvents();
    CHECK(qAbs(edit.document()->textWidth() - twipsToPx(pkg.page.width)) < 1.0, "page view restores page width");
    zip::Reader a, b;
    a.open(before);
    b.open(DocxWriter::build(edit.document(), pkg, &err));
    CHECK(a.read(QStringLiteral("word/document.xml")) == b.read(QStringLiteral("word/document.xml")),
          "view switching does not change the saved document");
}

static QColor shownInk(QTextDocument *doc, int pos)
{
    const QTextBlock b = doc->findBlock(pos);
    for (const QTextLayout::FormatRange &r : b.layout()->formats())
        if (pos - b.position() >= r.start && pos - b.position() < r.start + r.length && r.format.hasProperty(QTextFormat::ForegroundBrush))
            return r.format.foreground().color();
    return QColor();
}

static void paperTests()
{
    using spell::SpellHighlighter;
    CHECK(SpellHighlighter::onDarkPaper(Qt::black).lightnessF() >= 0.68, "black lightened for dark paper");
    CHECK(SpellHighlighter::onDarkPaper(QColor(0xFF, 0xE0, 0xA0)) == QColor(0xFF, 0xE0, 0xA0), "light colours kept");
    const QColor link = SpellHighlighter::onDarkPaper(QColor(0x05, 0x63, 0xC1));
    CHECK(link.lightnessF() > 0.6 && qAbs(link.hslHueF() - QColor(0x05, 0x63, 0xC1).hslHueF()) < 0.02,
          "dark colours keep their hue");

    PageEdit edit;
    Package pkg = Package::blank();
    QString err;
    DocxReader::build(pkg, edit.document(), &err);
    QTextDocument *doc = edit.document();
    QTextCursor c(doc);
    const QTextCharFormat base = c.charFormat();
    c.insertText(QStringLiteral("auto "), base);           // 0..4
    QTextCharFormat black = base;
    black.setForeground(QColor(Qt::black));
    c.insertText(QStringLiteral("black "), black);         // 5..10
    QTextCharFormat hl = base;
    hl.setBackground(QColor(Qt::yellow));
    c.insertText(QStringLiteral("marked"), hl);            // 11..16
    doc->setModified(false);
    doc->clearUndoRedoStacks();
    const QByteArray before = DocxWriter::build(doc, pkg, &err);

    edit.setColors(theme::deskColor(), theme::Paper::Dark);
    QApplication::processEvents();
    CHECK(edit.palette().color(QPalette::Base).lightness() < 64, "dark paper base");
    CHECK(!shownInk(doc, 1).isValid(), "auto text uses the paper ink");
    CHECK(shownInk(doc, 6).isValid() && shownInk(doc, 6).lightnessF() > 0.6, "explicit black shown light");
    CHECK(shownInk(doc, 12) == QColor(Qt::black), "text on a highlight keeps dark ink");
    CHECK(!doc->isModified() && !doc->isUndoAvailable(), "dark paper leaves document unmodified");
    zip::Reader a, b;
    a.open(before);
    b.open(DocxWriter::build(doc, pkg, &err));
    CHECK(a.read(QStringLiteral("word/document.xml")) == b.read(QStringLiteral("word/document.xml")),
          "dark paper does not change the saved document");
    std::unique_ptr<QTextDocument> clone(doc->clone());
    CHECK(clone->findBlock(6).layout()->formats().isEmpty(), "print copy carries no display ink");

    edit.setColors(theme::deskColor(), theme::Paper::White);
    QApplication::processEvents();
    CHECK(!shownInk(doc, 6).isValid(), "white paper drops the display ink");
    CHECK(edit.palette().color(QPalette::Base) == QColor(Qt::white), "white paper base");

    // Light neutral paragraph shading (code blocks) shows as a dark card on dark
    // paper, with selection still drawn on top.
    {
        PageEdit e2;
        Package p2 = Package::blank();
        DocxReader::build(p2, e2.document(), &err);
        e2.setPageSetup(p2.page);
        QTextCursor cc(e2.document());
        cc.insertText(QStringLiteral("before"));
        QTextBlockFormat shaded;
        shaded.setBackground(QColor(0xF3, 0xF4, 0xF6));
        cc.insertBlock(shaded);
        cc.insertText(QStringLiteral("int x = 1;            "));
        e2.document()->setModified(false);
        e2.resize(1000, 600);
        e2.show();
        e2.setColors(theme::deskColor(), theme::Paper::Dark);
        QApplication::processEvents();
        const QTextBlock code = e2.document()->lastBlock();
        const QRectF r = e2.document()->documentLayout()->blockBoundingRect(code);
        const QPoint offs(-e2.horizontalScrollBar()->value(), -e2.verticalScrollBar()->value());
        const QPoint blank = (QPointF(r.right() - 20, r.center().y())).toPoint() + offs;
        QImage img = e2.viewport()->grab().toImage();
        CHECK(QColor(img.pixel(blank)).lightness() < 80, "code block shading shown dark on dark paper");
        QTextCursor sel(code);
        sel.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        e2.setTextCursor(sel);
        QApplication::processEvents();
        img = e2.viewport()->grab().toImage();
        const QPoint mid = e2.cursorRect(QTextCursor(code)).center() + QPoint(30, 0);
        bool hasHighlight = false;
        const QColor hl = e2.palette().color(QPalette::Highlight);
        for (int dx = -10; dx <= 10 && !hasHighlight; ++dx)
            hasHighlight = QColor(img.pixel(mid + QPoint(dx, 0))) == hl;
        CHECK(hasHighlight, "selection drawn over the dark code card");
        CHECK(!e2.document()->isModified(), "dark code card leaves document unmodified");
    }

    const QPalette sys = QApplication::palette();
    theme::captureSystem();
    theme::apply(theme::Theme::Dark);
    CHECK(theme::appIsDark() && theme::deskColor().lightness() < 100, "dark theme");
    theme::apply(theme::Theme::Light);
    CHECK(!theme::appIsDark(), "light theme");
    theme::apply(theme::Theme::System);
    CHECK(QApplication::palette().color(QPalette::Window) == sys.color(QPalette::Window), "system theme restored");
    CHECK(theme::themeFromString(QStringLiteral("bogus")) == theme::Theme::System
              && theme::paperFromString(theme::toString(theme::Paper::Dark)) == theme::Paper::Dark,
          "theme settings strings");
}

static void pageTests(const QString &fixture)
{
    // Page view paginates like print: same page count, every paragraph on the same
    // page as the printed copy, and none of it touches the document.
    auto *edit = new PageEdit;
    ZoomView view(edit);
    Package pkg = Package::blank();
    QString err;
    DocxReader::build(pkg, edit->document(), &err);
    edit->setPageSetup(pkg.page);
    QTextCursor c(edit->document());
    for (int i = 0; i < 140; ++i)
        c.insertText(QStringLiteral("Paragraph %1 with enough words to wrap onto a second line at letter width, "
                                    "so the page breaks fall inside wrapped text as well as between paragraphs.\n")
                         .arg(i));
    edit->document()->setModified(false);
    edit->document()->clearUndoRedoStacks();
    view.resize(1000, 800);
    view.show();
    QApplication::processEvents();
    QApplication::processEvents();
    QTextDocument *doc = edit->document();
    const qreal ph = edit->pageHeightPx();
    CHECK(qAbs(doc->pageSize().height() - ph) < 0.5, "page view sets the page height");
    CHECK(doc->pageCount() > 3, qPrintable(QStringLiteral("long document spans pages (%1)").arg(doc->pageCount())));

    std::unique_ptr<QTextDocument> copy(doc->clone());
    copy->setPageSize(QSizeF(twipsToPx(pkg.page.width), twipsToPx(pkg.page.height)));
    CHECK(copy->pageCount() == doc->pageCount(), "screen page count matches print");
    bool samePages = true;
    for (QTextBlock a = doc->begin(), b = copy->begin(); a.isValid() && b.isValid(); a = a.next(), b = b.next()) {
        const int pa = int(doc->documentLayout()->blockBoundingRect(a).top() / ph);
        const int pb = int(copy->documentLayout()->blockBoundingRect(b).top() / ph);
        if (pa != pb || a.layout()->lineCount() != b.layout()->lineCount())
            samePages = false;
    }
    CHECK(samePages, "every paragraph on the same page and lines as print");
    CHECK(edit->height() >= doc->pageCount() * int(ph), "editor shows whole sheets");
    CHECK(!doc->isModified() && !doc->isUndoAvailable(), "pagination leaves document unmodified");

    view.resize(700, 600);  // QTextEdit relayouts on resize and drops the page height
    QApplication::processEvents();
    QApplication::processEvents();
    CHECK(qAbs(doc->pageSize().height() - ph) < 0.5, "pagination survives a resize");
    view.setZoom(2.0);
    QApplication::processEvents();
    CHECK(qAbs(doc->pageSize().height() - ph) < 0.5, "pagination survives zoom");
    edit->setViewMode(PageEdit::ViewMode::Draft);
    QApplication::processEvents();
    QApplication::processEvents();
    CHECK(doc->pageSize().height() <= 0, "draft view is continuous");
    edit->setViewMode(PageEdit::ViewMode::Page);
    QApplication::processEvents();
    QApplication::processEvents();
    CHECK(qAbs(doc->pageSize().height() - ph) < 0.5 && doc->pageCount() == copy->pageCount(), "page view paginates again");
    CHECK(!doc->isModified() && !doc->isUndoAvailable(), "view switching with pages leaves document unmodified");

    // The fixture's explicit page break starts page two on screen too.
    Package fpkg;
    auto *fedit = new PageEdit;
    ZoomView fview(fedit);
    if (DocxReader::loadFile(fixture, fedit->document(), &fpkg, &err)) {
        fedit->setPageSetup(fpkg.page);
        fview.resize(1000, 800);
        fview.show();
        QApplication::processEvents();
        QApplication::processEvents();
        const QTextBlock p2 = findBlock(*fedit->document(), QStringLiteral("First line on page two"));
        const qreal fph = fedit->pageHeightPx();
        CHECK(p2.isValid() && int(fedit->document()->documentLayout()->blockBoundingRect(p2).top() / fph) == 1,
              "page break starts the next page on screen");
    }
}

static QString docText(QTextDocument *d)
{
    return d ? d->toPlainText() : QString();
}

static int pdfPages(const QByteArray &pdf)
{
    static const QRegularExpression re(QStringLiteral("/Type\\s*/Page[^s]"));
    int n = 0;
    auto it = re.globalMatch(QString::fromLatin1(pdf));
    while (it.hasNext()) {
        it.next();
        ++n;
    }
    return n;
}

static void headerFooterTests(const QString &file)
{
    Package pkg;
    QTextDocument body;
    QString err;
    if (!DocxReader::loadFile(file, &body, &pkg, &err)) {
        CHECK(false, "headers fixture loads");
        return;
    }
    CHECK(pkg.hdrFtr.size() == 3 && pkg.titlePg, "header/footer references and titlePg parsed");
    CHECK(pkg.page.header == 720 && pkg.page.footer == 720, "header/footer distances parsed");
    HeaderFooters hf;
    hf.load(pkg);
    CHECK(hf.typeForPage(0) == QLatin1String("first") && hf.typeForPage(1) == QLatin1String("default")
              && hf.typeForPage(2) == QLatin1String("default"),
          "first page uses the first-page header");
    QTextDocument *hdr = hf.doc(false, QStringLiteral("default"));
    CHECK(docText(hdr).startsWith(QLatin1String("Fixture Report")), "default header text");
    CHECK(docText(hf.doc(false, QStringLiteral("first"))) == QLatin1String("FIRST PAGE HEADER"), "first-page header text");
    CHECK(!hf.doc(true, QStringLiteral("first")), "no first-page footer: page one footer is blank");
    if (hdr) {
        std::unique_ptr<QTextDocument> c(hdr->clone());
        HeaderFooters::fillFields(c.get(), 3, 5);
        CHECK(c->toPlainText().endsWith(QLatin1String("Page 3 of 5")), "complex PAGE / NUMPAGES fields filled");
    }
    if (QTextDocument *ftr = hf.doc(true, QStringLiteral("default"))) {
        std::unique_ptr<QTextDocument> c(ftr->clone());
        HeaderFooters::fillFields(c.get(), 4, 5);
        CHECK(c->toPlainText() == QLatin1String("- 4 -"), "simple PAGE field filled");
    }
    CHECK(hf.editable(false, QStringLiteral("default")) && hf.editable(true, QStringLiteral("default")),
          "text + PAGE fields are editable");
    CHECK(!HeaderFooters::isTextOnly("<w:hdr><w:p><w:r><w:drawing/></w:r></w:p></w:hdr>")
              && !HeaderFooters::isTextOnly("<w:hdr><w:p><w:fldSimple w:instr=\" DATE \"/></w:p></w:hdr>"),
          "images and other fields are read-only");

    // Save keeps the parts untouched.
    Package saved = pkg;
    const QByteArray out = DocxWriter::build(&body, saved, &err);
    Package back;
    CHECK(back.loadFrom(out, &err) && back.hdrFtr.size() == 3 && back.titlePg, "headers survive save");
    bool sameParts = true;
    for (const HdrFtrRef &r : pkg.hdrFtr)
        if (back.parts.value(r.part) != pkg.parts.value(r.part))
            sameParts = false;
    CHECK(sameParts, "header/footer parts saved byte-for-byte");

    // Print/PDF draws every page with headers.
    QBuffer buf;
    buf.open(QIODevice::WriteOnly);
    {
        QPdfWriter pdf(&buf);
        pdf.setPageLayout(QPageLayout(QPageSize(QSizeF(pkg.page.width / 1440.0, pkg.page.height / 1440.0), QPageSize::Inch),
                                      QPageLayout::Portrait, QMarginsF(0, 0, 0, 0)));
        PageEdit::printPages(&body, pkg.page, &hf, &pdf);
    }
    std::unique_ptr<QTextDocument> paged(body.clone());
    paged->setPageSize(QSizeF(twipsToPx(pkg.page.width), twipsToPx(pkg.page.height)));
    CHECK(paged->pageCount() >= 3 && pdfPages(buf.data()) == paged->pageCount(),
          qPrintable(QStringLiteral("PDF has every page (%1 of %2)").arg(pdfPages(buf.data())).arg(paged->pageCount())));

    // Tokens <-> fields.
    {
        std::unique_ptr<QTextDocument> c(hdr ? hdr->clone() : new QTextDocument);
        HeaderFooterDialog::fieldsToTokens(c.get());
        CHECK(c->toPlainText().endsWith(QLatin1String("Page {PAGE} of {NUMPAGES}")), "fields shown as tokens");
        HeaderFooterDialog::tokensToFields(c.get());
        HeaderFooters::fillFields(c.get(), 2, 9);
        CHECK(c->toPlainText().endsWith(QLatin1String("Page 2 of 9")), "tokens become fields again");
    }

    // New header and footer in a blank document, through the dialog.
    Package blank = Package::blank();
    QTextDocument bdoc;
    DocxReader::build(blank, &bdoc, &err);
    HeaderFooters bhf;
    bhf.load(blank);
    {
        HeaderFooterDialog dlg(blank, bhf, false, false);
        const QList<QTextEdit *> edits = dlg.findChildren<QTextEdit *>();
        CHECK(edits.size() == 4, "dialog has first-page and all-pages header/footer editors");
        if (edits.size() == 4) {
            edits[2]->insertPlainText(QStringLiteral("My Report\t\tPage {PAGE} of {NUMPAGES}"));
            edits[3]->insertPlainText(QStringLiteral("Confidential"));
        }
        CHECK(dlg.apply(), "dialog applies new header/footer");
    }
    const QByteArray bout = DocxWriter::build(&bdoc, blank, &err);
    Package b2;
    QTextDocument b2doc;
    CHECK(DocxReader::loadData(bout, &b2doc, &b2, &err), "document with new header reloads");
    CHECK(b2.hdrFtr.size() == 2 && !b2.titlePg, "new header and footer referenced");
    const QString docXml = QString::fromUtf8(b2.parts.value(b2.mainPart));
    const QString ct = QString::fromUtf8(b2.parts.value(QStringLiteral("[Content_Types].xml")));
    CHECK(docXml.contains(QLatin1String("<w:sectPr><w:footerReference")) || docXml.contains(QLatin1String("<w:sectPr><w:headerReference")),
          "references are the first children of sectPr");
    CHECK(ct.contains(QLatin1String("wordprocessingml.header+xml")) && ct.contains(QLatin1String("wordprocessingml.footer+xml")),
          "content types for new parts");
    HeaderFooters h2;
    h2.load(b2);
    if (QTextDocument *nh = h2.doc(false, QStringLiteral("default"))) {
        std::unique_ptr<QTextDocument> c(nh->clone());
        HeaderFooters::fillFields(c.get(), 7, 8);
        CHECK(c->toPlainText() == QLatin1String("My Report\t\tPage 7 of 8"), "new header round-trips with fields");
    } else {
        CHECK(false, "new header loads");
    }
    CHECK(docText(h2.doc(true, QStringLiteral("default"))) == QLatin1String("Confidential"), "new footer round-trips");

    // Different first page goes in schema order (before docGrid).
    Package fp = pkg;
    fp.setTitlePg(false);
    CHECK(!fp.sectPrXml.contains(QLatin1String("titlePg")), "titlePg removed");
    fp.setTitlePg(true);
    CHECK(fp.sectPrXml.indexOf(QLatin1String("<w:titlePg/>")) < fp.sectPrXml.indexOf(QLatin1String("<w:docGrid")),
          "titlePg inserted before docGrid");
}

static void recentTests()
{
    QStringList l;
    for (int i = 0; i < 12; ++i)
        l = recent::pushed(l, QStringLiteral("/docs/f%1.docx").arg(i));
    CHECK(l.size() == recent::kMax && l.first() == QLatin1String("/docs/f11.docx"), "recent list capped, newest first");
    l = recent::pushed(l, QStringLiteral("/docs/f5.docx"));
    CHECK(l.first() == QLatin1String("/docs/f5.docx") && l.count(QStringLiteral("/docs/f5.docx")) == 1,
          "reopening moves a file to the top without duplicating it");
    l = recent::pushed(l, QStringLiteral("/docs/sub/../f5.docx"));
    CHECK(l.count(QStringLiteral("/docs/f5.docx")) == 1, "paths normalised");
    // Real absolute paths: "/tmp/x" is drive-relative on Windows.
    const QString a = QDir(QDir::tempPath()).filePath(QStringLiteral("apq-a.docx"));
    const QString b = QDir(QDir::tempPath()).filePath(QStringLiteral("apq-b.docx"));
    recent::clear();
    recent::add(a);
    recent::add(b);
    recent::remove(a);
    const QStringList got = recent::load();
    CHECK(got.size() == 1 && QFileInfo(got.value(0)) == QFileInfo(b),
          qPrintable(QStringLiteral("recent list persists add/remove (%1)").arg(got.join(QLatin1Char('|')))));
    recent::clear();
    CHECK(recent::load().isEmpty(), "clear list");
}

static QTextTable *firstTable(QTextDocument &doc)
{
    for (QTextFrame *f : doc.rootFrame()->childFrames())
        if (auto *t = qobject_cast<QTextTable *>(f))
            return t;
    return nullptr;
}

static void tableTests()
{
    // Spec round trip and preset detection.
    for (auto p : {TblBorders::None, TblBorders::All, TblBorders::Outside, TblBorders::Inside, TblBorders::Horizontal}) {
        const TblBorders b = TblBorders::preset(p, 12, QStringLiteral("FF0000"));
        CHECK(TblBorders::fromSpec(b.spec()).detect() == p, qPrintable(QStringLiteral("border preset %1 round-trips").arg(int(p))));
    }
    CHECK(TablePropertiesDialog::distributed(3, 300) == QList<double>({100, 100, 100}), "distribute evenly");
    CHECK(TablePropertiesDialog::scaledTo({100, 300}, 200) == QList<double>({50, 150}), "fit to width keeps proportions");

    // Read: explicit outside-only red 1.5pt borders, 0.15" cell margins, centred table, centred cell.
    const QString docXml = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><w:body>"
        "<w:tbl><w:tblPr><w:tblW w:w=\"4320\" w:type=\"dxa\"/><w:jc w:val=\"center\"/><w:tblBorders>"
        "<w:top w:val=\"single\" w:sz=\"12\" w:space=\"0\" w:color=\"FF0000\"/><w:left w:val=\"single\" w:sz=\"12\" w:space=\"0\" w:color=\"FF0000\"/>"
        "<w:bottom w:val=\"single\" w:sz=\"12\" w:space=\"0\" w:color=\"FF0000\"/><w:right w:val=\"single\" w:sz=\"12\" w:space=\"0\" w:color=\"FF0000\"/>"
        "<w:insideH w:val=\"nil\"/><w:insideV w:val=\"nil\"/></w:tblBorders>"
        "<w:tblCellMar><w:left w:w=\"216\" w:type=\"dxa\"/><w:right w:w=\"216\" w:type=\"dxa\"/></w:tblCellMar></w:tblPr>"
        "<w:tblGrid><w:gridCol w:w=\"1440\"/><w:gridCol w:w=\"2880\"/></w:tblGrid>"
        "<w:tr><w:tc><w:tcPr><w:tcW w:w=\"1440\" w:type=\"dxa\"/><w:vAlign w:val=\"center\"/></w:tcPr><w:p><w:r><w:t>a</w:t></w:r></w:p></w:tc>"
        "<w:tc><w:tcPr><w:tcW w:w=\"2880\" w:type=\"dxa\"/></w:tcPr><w:p><w:r><w:t>b</w:t></w:r></w:p></w:tc></w:tr>"
        "<w:tr><w:tc><w:p><w:r><w:t>c</w:t></w:r></w:p></w:tc><w:tc><w:p><w:r><w:t>d</w:t></w:r></w:p></w:tc></w:tr>"
        "</w:tbl><w:p/><w:sectPr><w:pgSz w:w=\"12240\" w:h=\"15840\"/><w:pgMar w:top=\"1440\" w:right=\"1440\" w:bottom=\"1440\" w:left=\"1440\"/></w:sectPr>"
        "</w:body></w:document>");
    Package pkg = Package::blank();
    pkg.parts.insert(pkg.mainPart, docXml.toUtf8());
    QTextDocument doc;
    QString err;
    CHECK(DocxReader::build(pkg, &doc, &err), "table doc builds");
    QTextTable *t = firstTable(doc);
    CHECK(t != nullptr, "table read");
    if (!t)
        return;
    const TblBorders b = TblBorders::of(t->format());
    CHECK(b.detect() == TblBorders::Outside && b.side[TblBorders::Top].sz == 12 && b.side[TblBorders::Top].color == QLatin1String("FF0000"),
          "outside-only borders read with width and colour");
    CHECK(t->format().intProperty(TableCellMarLR) == 216, "cell margins read");
    CHECK((t->format().alignment() & Qt::AlignHCenter), "table alignment read");
    CHECK(t->cellAt(0, 0).format().verticalAlignment() == QTextCharFormat::AlignMiddle, "cell vAlign read");
    const QTextTableCellFormat c00 = t->cellAt(0, 0).format().toTableCellFormat();
    const QTextTableCellFormat c11 = t->cellAt(1, 1).format().toTableCellFormat();
    CHECK(c00.leftBorder() >= 2 && c00.topBorder() >= 2 && c00.rightBorder() == 0 && c00.bottomBorder() == 0,
          "outer sides drawn, inside sides not");
    CHECK(c11.rightBorder() >= 2 && c11.bottomBorder() >= 2 && c11.rightBorderBrush().color() == QColor(Qt::red)
              && qAbs(c00.leftPadding() - twipsToPx(216)) < 0.01,
          "per-cell border colour and padding");

    // Write and read back.
    Package out = pkg;
    const QByteArray saved = DocxWriter::build(&doc, out, &err);
    zip::Reader z;
    z.open(saved);
    const QString xml = QString::fromUtf8(z.read(QStringLiteral("word/document.xml")));
    CHECK(xml.contains(QLatin1String("<w:insideH w:val=\"nil\"/>")) && xml.contains(QLatin1String("w:sz=\"12\" w:space=\"0\" w:color=\"FF0000\"")),
          "borders written");
    CHECK(xml.contains(QLatin1String("<w:tblCellMar><w:left w:w=\"216\"")), "cell margins written");
    CHECK(xml.contains(QLatin1String("<w:vAlign w:val=\"center\"/>")), "cell vAlign written");
    CHECK(xml.indexOf(QLatin1String("<w:tblLayout")) < xml.indexOf(QLatin1String("<w:tblCellMar")), "tblPr children in schema order");

    // Structural edit: a new last row takes the bottom border.
    t->insertRows(2, 1);
    applyTableCellFormats(t);
    CHECK(t->cellAt(2, 0).format().toTableCellFormat().bottomBorder() >= 2
              && t->cellAt(1, 0).format().toTableCellFormat().bottomBorder() == 0,
          "borders follow inserted rows");

    // Dialog: switch to All borders, 1pt, one undo step.
    doc.clearUndoRedoStacks();
    const QString before = t->format().stringProperty(TableBorderSpec);
    {
        TablePropertiesDialog dlg(t, 624, 0, 1, 0, 1);
        auto *preset = dlg.findChild<QComboBox *>(QStringLiteral("preset"));
        auto *width = dlg.findChild<QComboBox *>(QStringLiteral("borderWidth"));
        auto *valign = dlg.findChild<QComboBox *>(QStringLiteral("valign"));
        CHECK(preset && preset->currentData().toInt() == TblBorders::Outside, "dialog shows current preset");
        CHECK(valign && valign->currentData().toInt() == int(QTextCharFormat::AlignMiddle), "dialog shows cell vAlign");
        if (preset && width && valign) {
            preset->setCurrentIndex(preset->findData(TblBorders::All));
            width->setCurrentIndex(width->findData(8));
            valign->setCurrentIndex(valign->findData(int(QTextCharFormat::AlignBottom)));
        }
        dlg.apply();
    }
    const TblBorders after = TblBorders::of(t->format());
    CHECK(after.detect() == TblBorders::All && after.side[TblBorders::InsideV].sz == 8 && after.side[TblBorders::InsideV].color == QLatin1String("FF0000"),
          "dialog applies preset, width and keeps colour");
    CHECK(t->cellAt(0, 0).format().verticalAlignment() == QTextCharFormat::AlignBottom, "dialog applies cell vAlign");
    CHECK(t->cellAt(0, 0).format().toTableCellFormat().rightBorder() > 0, "inside borders now drawn");
    doc.undo();
    CHECK(t->format().stringProperty(TableBorderSpec) == before && !doc.isUndoAvailable(), "table properties undo in one step");
}

static void spellMenuTest()
{
    PageEdit edit;
    Package pkg = Package::blank();
    QString err;
    DocxReader::build(pkg, edit.document(), &err);
    QTextCursor c(edit.document());
    QTextCharFormat bold = c.charFormat();
    bold.setFontWeight(QFont::Bold);
    c.insertText(QStringLiteral("We recieved it"), bold);
    edit.resize(900, 500);
    edit.show();
    QApplication::processEvents();

    // Point at the middle of "recieved".
    QTextCursor at(edit.document());
    at.setPosition(6);
    const QPoint pos = edit.cursorRect(at).center();
    QStringList seen;
    QTimer::singleShot(150, [&] {
        if (auto *m = qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
            for (QAction *a : m->actions())
                if (!a->isSeparator())
                    seen << a->text();
            QAction *first = m->actions().value(0);
            m->close();
            if (first)
                first->trigger();
        }
    });
    QContextMenuEvent ev(QContextMenuEvent::Mouse, pos, edit.viewport()->mapToGlobal(pos));
    QApplication::sendEvent(edit.viewport(), &ev);
    QApplication::processEvents();
    CHECK(!seen.isEmpty() && seen.first() == QLatin1String("received"),
          qPrintable(QStringLiteral("first suggestion is 'received' (menu: %1)").arg(seen.join(QLatin1Char('|')))));
    CHECK(seen.contains(QStringLiteral("Add \"recieved\" to Dictionary")) && seen.contains(QStringLiteral("Ignore All")),
          "menu offers add/ignore");
    CHECK(edit.document()->begin().text() == QLatin1String("We received it"), "suggestion replaces the word");
    QTextCursor chk(edit.document());
    chk.setPosition(5);
    CHECK(chk.charFormat().fontWeight() >= QFont::Bold, "replacement keeps the word's formatting");
}

static void findReplaceTests()
{
    QTextDocument doc;
    QTextCursor c(&doc);
    QTextCharFormat plain, bold;
    bold.setFontWeight(QFont::Bold);
    c.insertText(QStringLiteral("the cat"), bold);
    c.insertText(QStringLiteral(" and the hat. The end."), plain);
    doc.clearUndoRedoStacks();
    CHECK(FindBar::matches(&doc, QStringLiteral("the"), {}).size() == 3, "case-insensitive match count");
    CHECK(FindBar::matches(&doc, QStringLiteral("the"), QTextDocument::FindCaseSensitively).size() == 2,
          "case-sensitive match count");
    const int n = FindBar::replaceAll(&doc, QStringLiteral("the"), QStringLiteral("a"), {});
    CHECK(n == 3 && doc.toPlainText() == QLatin1String("a cat and a hat. a end."), "replace all");
    QTextCursor f(&doc);
    f.setPosition(1);
    CHECK(f.charFormat().fontWeight() >= QFont::Bold, "replacement keeps the match's formatting");
    // availableUndoSteps() counts Qt's internal commands; what matters is that one
    // undo() reverts the whole replace and leaves nothing else to undo.
    doc.undo();
    CHECK(doc.toPlainText() == QLatin1String("the cat and the hat. The end.") && !doc.isUndoAvailable(),
          "replace all is a single undo step");

    QTextDocument d2;
    QTextCursor(&d2).insertText(QStringLiteral("a b a"));
    CHECK(FindBar::replaceAll(&d2, QStringLiteral("a"), QStringLiteral("aa"), {}) == 2
              && d2.toPlainText() == QLatin1String("aa b aa"),
          "replacement containing the query doesn't loop");

    QTextDocument d3;
    QTextCursor(&d3).insertText(QStringLiteral("cat category Cat cat."));
    CHECK(FindBar::replaceAll(&d3, QStringLiteral("cat"), QStringLiteral("dog"),
                              QTextDocument::FindWholeWords | QTextDocument::FindCaseSensitively) == 2
              && d3.toPlainText() == QLatin1String("dog category Cat dog."),
          "whole words + match case");
}

static void codeTests(const QString &keepPath)
{
    QTextDocument doc;
    Package pkg = Package::blank();
    QString err;
    DocxReader::build(pkg, &doc, &err);
    CHECK(code::ensureStyles(pkg), "code styles added");
    CHECK(pkg.styles.style(QStringLiteral("Code")) && pkg.styles.style(QStringLiteral("CodeChar")), "code styles parsed");
    CHECK(code::ensureStyles(pkg), "ensureStyles is idempotent");
    zip::Reader zs;
    zs.open(DocxWriter::build(&doc, pkg, &err));
    CHECK(QString::fromUtf8(zs.read(QStringLiteral("word/styles.xml"))).count(QStringLiteral("w:styleId=\"Code\"")) == 1,
          "Code style written once");

    QTextCursor c(&doc);
    setStyle(c, pkg, QStringLiteral("Normal"));
    const QTextCharFormat normal = styleChar(pkg, QStringLiteral("Normal"));
    c.insertText(QStringLiteral("Call "), normal);
    QTextCharFormat inl = normal;
    inl.setProperty(RStyle, QStringLiteral("CodeChar"));
    inl.setFontFamilies(QStringList{QStringLiteral("Consolas")});
    inl.setBackground(QColor(0xF3, 0xF4, 0xF6));
    c.insertText(QStringLiteral("get_fooo()"), inl);
    c.insertText(QStringLiteral(" in this sentance."), normal);
    const QStringList lines = {QStringLiteral("def main():"), QStringLiteral("    retrun 0  # tpyo in code"),
                               QStringLiteral("\tindented")};
    QTextBlock firstCode;
    for (const QString &line : lines) {
        c.insertBlock();
        setStyle(c, pkg, QStringLiteral("Code"));
        c.insertText(line, styleChar(pkg, QStringLiteral("Code")));
        if (!firstCode.isValid())
            firstCode = c.block();
    }
    c.insertBlock();
    setStyle(c, pkg, QStringLiteral("Normal"));
    c.insertText(QStringLiteral("After the code."), normal);
    applyContextualSpacing(&doc, firstCode, c.block().previous());

    const QTextBlock mid = firstCode.next();
    CHECK(firstCode.blockFormat().bottomMargin() == 0 && mid.blockFormat().topMargin() == 0
              && mid.blockFormat().bottomMargin() == 0 && firstCode.blockFormat().topMargin() > 0,
          "code lines packed together, space kept around the block");

    // Spelling: code is never checked.
    spell::SpellChecker::instance().setLanguage(QStringLiteral("en_US"));
    QStringList checked;
    for (const spell::Word &w : spell::SpellChecker::checkableWords(doc.begin()))
        checked << w.text;
    CHECK(!checked.contains(QStringLiteral("fooo")) && checked.contains(QStringLiteral("sentance")),
          "inline code skipped by spell check, prose still checked");
    CHECK(spell::SpellChecker::checkableWords(mid).isEmpty(), "code block skipped by spell check");

    const QByteArray saved = DocxWriter::build(&doc, pkg, &err);
    if (!keepPath.isEmpty()) {
        QFile out(keepPath);
        if (out.open(QIODevice::WriteOnly))
            out.write(saved);
    }
    zip::Reader z;
    z.open(saved);
    const QString xml = QString::fromUtf8(z.read(QStringLiteral("word/document.xml")));
    CHECK(xml.count(QStringLiteral("<w:pStyle w:val=\"Code\"/>")) == 3, "three Code paragraphs written");
    CHECK(xml.contains(QStringLiteral("<w:rStyle w:val=\"CodeChar\"/>")), "inline code written as CodeChar");
    CHECK(xml.contains(QStringLiteral("    retrun 0")) && xml.contains(QStringLiteral("<w:tab/><w:t xml:space=\"preserve\">indented")),
          "code indentation preserved");
    const int codeAt = xml.indexOf(QStringLiteral("    retrun"));
    const int codeP = xml.lastIndexOf(QStringLiteral("<w:p>"), codeAt);
    CHECK(!xml.mid(codeP, codeAt - codeP).contains(QStringLiteral("w:spacing")),
          "packed code spacing not written as direct formatting");

    QTextDocument back;
    Package pb;
    CHECK(DocxReader::loadData(saved, &back, &pb, &err), "code doc reloads");
    CHECK(qAbs(back.defaultTextOption().tabStopDistance() - 48.0) < 0.01, "default tab stop from settings.xml (0.5in)");
    const QTextBlock rb = findBlock(back, QStringLiteral("    retrun"));
    CHECK(rb.blockFormat().stringProperty(PStyle) == QLatin1String("Code") && rb.blockFormat().topMargin() == 0
              && rb.blockFormat().bottomMargin() == 0,
          "reloaded code block style and packing");
    CHECK(fromCharFormat(formatAt(rb, QStringLiteral("retrun"))).font == QLatin1String("Consolas"), "code font kept");
    CHECK(!rb.blockFormat().stringProperty(BorderLeft).isEmpty() && rb.blockFormat().background().style() != Qt::NoBrush,
          "code block shading and rule");
}

static void zoomTests(const QString &file)
{
    auto *edit = new PageEdit;
    ZoomView view(edit);
    Package pkg;
    QString err;
    if (!DocxReader::loadFile(file, edit->document(), &pkg, &err)) {
        CHECK(false, "zoom test doc loads");
        return;
    }
    edit->setPageSetup(pkg.page);
    view.resize(1600, 900);
    view.show();
    QApplication::processEvents();
    QApplication::processEvents();
    auto lineCounts = [&] {
        QList<int> n;
        for (QTextBlock b = edit->document()->begin(); b.isValid(); b = b.next())
            n << (b.layout() ? b.layout()->lineCount() : 0);
        return n;
    };
    view.setZoom(1.0);
    QApplication::processEvents();
    const QList<int> at100 = lineCounts();
    view.setFitWidth();
    QApplication::processEvents();
    CHECK(view.zoom() > 1.5, qPrintable(QStringLiteral("fit width zooms in on a wide window (%1)").arg(view.zoom())));
    CHECK(lineCounts() == at100, "line breaks identical at fit width and 100%");
    view.setZoom(3.0);
    QApplication::processEvents();
    CHECK(lineCounts() == at100, "line breaks identical at 300%");
    view.setFitWidth();
    QApplication::processEvents();

    // Click on a word through the scaled view: the cursor must land in it.
    const QTextBlock target = findBlock(*edit->document(), QStringLiteral("Network Engineer"));
    QTextCursor tc(target);
    tc.setPosition(target.position() + 3);
    const QRect cr = edit->cursorRect(tc);
    view.ensureVisible(QRectF(QPointF(edit->viewport()->mapTo(edit, cr.topLeft())), QSizeF(2, cr.height())));
    QApplication::processEvents();
    auto *proxy = qgraphicsitem_cast<QGraphicsProxyWidget *>(view.items().value(0));
    QPointF scenePt = QPointF(edit->viewport()->mapTo(edit, cr.center()));
    if (proxy)
        scenePt = proxy->mapToScene(scenePt);
    const QPoint vp = view.mapFromScene(scenePt);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(vp), view.viewport()->mapToGlobal(QPointF(vp)), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(vp), view.viewport()->mapToGlobal(QPointF(vp)), Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &press);
    QApplication::sendEvent(view.viewport(), &release);
    QApplication::processEvents();
    const int clicked = edit->textCursor().position() - target.position();
    CHECK(clicked >= 2 && clicked <= 4, qPrintable(QStringLiteral("click maps through the zoom (offset %1, want ~3)").arg(clicked)));

    // Typing goes into the document at that point.
    QKeyEvent key(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier, QStringLiteral("X"));
    QApplication::sendEvent(edit, &key);
    CHECK(target.text().startsWith(QLatin1String("NetXwork")) || target.text().startsWith(QLatin1String("NeXtwork"))
              || target.text().startsWith(QLatin1String("NetwXork")),
          qPrintable(QStringLiteral("typing lands at the click: %1").arg(target.text().left(12))));
    edit->document()->undo();

    // Wheel scrolls the page; PageDown pages by the visible height, not to the end.
    QScrollBar *v = view.verticalScrollBar();
    v->setValue(0);
    QWheelEvent wheel(QPointF(400, 400), view.viewport()->mapToGlobal(QPointF(400, 400)), QPoint(), QPoint(0, -360),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(view.viewport(), &wheel);
    CHECK(v->value() > 0, "mouse wheel scrolls the zoomed page");
    v->setValue(0);
    QTextCursor home(edit->document());
    edit->setTextCursor(home);
    QKeyEvent pgdn(QEvent::KeyPress, Qt::Key_PageDown, Qt::NoModifier);
    QApplication::sendEvent(edit, &pgdn);
    QApplication::processEvents();
    const int after = edit->textCursor().position();
    CHECK(after > 0 && after < edit->document()->characterCount() - 50 && v->value() > 0,
          qPrintable(QStringLiteral("PageDown moves one screen (pos %1 of %2)").arg(after).arg(edit->document()->characterCount())));

    // Zooming and view switching leave the document alone.
    CHECK(!edit->document()->isModified(), "zooming does not modify the document");
}

static void zipTests()
{
    QByteArray big;
    for (int i = 0; i < 200000; ++i)
        big.append(char('a' + (i * 7 + i / 13) % 26));
    QList<zip::Entry> entries = {{QStringLiteral("empty.txt"), QByteArray()},
                                 {QStringLiteral("one.txt"), QByteArray("x")},
                                 {QStringLiteral("big.txt"), big},
                                 {QStringLiteral("dir/ünïcode.xml"), QByteArray("<a>é</a>")}};
    const QByteArray z = zip::write(entries);
    zip::Reader r;
    CHECK(r.open(z), "zip reopens");
    for (const zip::Entry &e : entries) {
        bool ok = false;
        CHECK(r.read(e.name, &ok) == e.data && ok, qPrintable(QStringLiteral("zip entry %1").arg(e.name)));
    }
}

int main(int argc, char **argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QStandardPaths::setTestModeEnabled(true);  // personal dictionary etc. go to a test location
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("anyprocessorqt-tests"));
    QApplication::setOrganizationName(QStringLiteral("anyprocessorqt-tests"));  // QSettings needs one on Windows

    QString keep;
    QStringList files;
    const QStringList args = app.arguments().mid(1);
    for (int i = 0; i < args.size(); ++i) {
        if (args[i] == QLatin1String("--keep") && i + 1 < args.size())
            keep = args[++i];
        else
            files << args[i];
    }

    zipTests();
    Package pkg;
    const QByteArray editorDoc = buildEditorDoc(pkg);
    checkEditorDoc(editorDoc);
    if (!keep.isEmpty()) {
        QFile out(keep);
        if (out.open(QIODevice::WriteOnly))
            out.write(editorDoc);
    }
    spellTests();
    spellMenuTest();
    recentTests();
    tableTests();
    paperTests();
    findReplaceTests();
    codeTests(keep.isEmpty() ? QString() : keep + QStringLiteral(".code.docx"));
    for (const QString &f : files) {
        roundTripFile(f);
        if (f.endsWith(QLatin1String("features.docx"))) {
            viewTests(f);
            pageTests(f);
        }
        if (f.endsWith(QLatin1String("headers.docx")))
            headerFooterTests(f);
        if (f.endsWith(QLatin1String("resume.docx")))
            zoomTests(f);
        if (f.endsWith(QLatin1String("features.docx")))
            fixtureChecks(f);
    }

    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
