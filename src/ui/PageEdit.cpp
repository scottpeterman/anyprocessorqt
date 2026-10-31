#include "ui/PageEdit.h"
#include "docx/Units.h"
#include "spell/SpellChecker.h"
#include "spell/SpellHighlighter.h"

#include <QAbstractTextDocumentLayout>
#include <QContextMenuEvent>
#include <QMenu>
#include <QImage>
#include <QMimeData>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextFrame>
#include <QTextImageFormat>
#include <QUrl>
#include <QUuid>

#include <memory>

using namespace docx;

PageEdit::PageEdit(QWidget *parent) : QTextEdit(parent)
{
    setAcceptRichText(true);
    setLineWrapMode(QTextEdit::FixedPixelWidth);
    setFrameShape(QFrame::NoFrame);
    setAutoFillBackground(true);
    viewport()->setAutoFillBackground(true);
    m_spell = new spell::SpellHighlighter(document());
    updatePalette();
    setPageSetup(PageSetup());
}

void PageEdit::setViewMode(ViewMode mode)
{
    if (m_mode == mode)
        return;
    m_mode = mode;
    updatePalette();
    updateMargins();
    ensureCursorVisible();
    emit geometryHintsChanged();
}

void PageEdit::setColors(const QColor &desk, theme::Paper paper)
{
    m_desk = desk;
    m_paper = paper;
    updatePalette();
    m_spell->setDarkPaper(paper == theme::Paper::Dark);
    viewport()->update();
}

void PageEdit::updatePalette()
{
    const QColor paper = theme::paperColor(m_paper);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, m_mode == ViewMode::Page ? m_desk : paper);
    pal.setColor(QPalette::Base, paper);
    pal.setColor(QPalette::Text, theme::inkColor(m_paper));
    setPalette(pal);
}

void PageEdit::setEmbedded(bool embedded)
{
    m_embedded = embedded;
    setVerticalScrollBarPolicy(embedded ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
    setHorizontalScrollBarPolicy(embedded ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
    updateMargins();
}

int PageEdit::pageWidthPx() const
{
    return int(qRound(twipsToPx(m_page.width)));
}

void PageEdit::setPageSetup(const PageSetup &page)
{
    m_page = page;
    updateMargins();
    emit geometryHintsChanged();
}

void PageEdit::updateMargins()
{
    const int sb = verticalScrollBar()->isVisible() ? verticalScrollBar()->width() : 0;
    const int avail = width() - sb;
    if (m_mode == ViewMode::Page && m_embedded) {
        setLineWrapColumnOrWidth(pageWidthPx());
        setViewportMargins(0, 0, 0, 0);
        return;
    }
    if (m_mode == ViewMode::Page) {
        const int pw = pageWidthPx();
        setLineWrapColumnOrWidth(pw);
        const int left = qMax(0, (avail - pw) / 2);
        const int right = qMax(0, avail - pw - left);
        setViewportMargins(left, 24, right, 0);
        return;
    }
    // Draft: text spans the window. The page margins are part of the document
    // (root frame), and changing them is an undoable edit, so instead the
    // viewport is widened by the margins and shifted so they sit off-screen.
    const QTextFrameFormat rf = document()->rootFrame()->frameFormat();
    const int pad = 28;
    const int hideL = qMax(0, int(rf.leftMargin()) - pad);
    const int hideR = qMax(0, int(rf.rightMargin()) - pad);
    const int hideT = qMax(0, int(rf.topMargin()) - 16);
    setLineWrapColumnOrWidth(qMax(200, avail + hideL + hideR));
    setViewportMargins(-hideL, -hideT, -hideR, 0);
}

void PageEdit::resizeEvent(QResizeEvent *e)
{
    QTextEdit::resizeEvent(e);
    updateMargins();
}

void PageEdit::paintEvent(QPaintEvent *e)
{
    QTextEdit::paintEvent(e);
    QPainter p(viewport());
    const QPointF off(-horizontalScrollBar()->value(), -verticalScrollBar()->value());
    p.translate(off);
    paintDecorations(p, document(), QRectF(viewport()->rect()).translated(-off), true, m_paper == theme::Paper::Dark);
}

static bool parseBorder(const QString &spec, QColor *color, double *width, double *space, bool darkPaper)
{
    const QStringList f = spec.split(QLatin1Char('|'));
    if (f.size() != 4)
        return false;
    *width = qMax(1.0, ptToPx(f[1].toDouble() / 8.0));
    *space = ptToPx(f[2].toDouble());
    *color = QColor(QLatin1Char('#') + f[3]);
    if (darkPaper)
        *color = spell::SpellHighlighter::onDarkPaper(*color);
    return true;
}

void PageEdit::paintDecorations(QPainter &p, QTextDocument *doc, const QRectF &clip, bool showBreaks, bool darkPaper)
{
    QAbstractTextDocumentLayout *layout = doc->documentLayout();
    const QTextFrameFormat rf = doc->rootFrame()->frameFormat();
    const double rootLeft = rf.leftMargin();
    const double rootRight = doc->textWidth() - rf.rightMargin();

    QRectF prevRect;
    QString prevLeft;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        const QRectF r = layout->blockBoundingRect(b);
        const QTextBlockFormat bf = b.blockFormat();
        const QString leftSpec = bf.stringProperty(BorderLeft);
        const bool inRoot = doc->frameAt(b.position()) == doc->rootFrame();
        const double contentLeft = inRoot ? rootLeft + bf.leftMargin() + bf.indent() * doc->indentWidth() : r.left();
        const double contentRight = inRoot ? rootRight - bf.rightMargin() : r.right();

        if (r.bottom() >= clip.top() - 50 && r.top() <= clip.bottom() + 50) {
            QColor c;
            double w = 0, space = 0;
            if (!leftSpec.isEmpty() && parseBorder(leftSpec, &c, &w, &space, darkPaper)) {
                const bool joined = prevLeft == leftSpec && prevRect.isValid();
                const double top = joined ? prevRect.bottom() : r.top() - 1;
                const double x = contentLeft - space - w / 2.0;
                p.fillRect(QRectF(x - w / 2.0, top, w, r.bottom() + 1 - top), c);
            }
            const QString rightSpec = bf.stringProperty(BorderRight);
            if (!rightSpec.isEmpty() && parseBorder(rightSpec, &c, &w, &space, darkPaper))
                p.fillRect(QRectF(contentRight + space, r.top() - 1, w, r.height() + 2), c);
            const QString topSpec = bf.stringProperty(BorderTop);
            if (!topSpec.isEmpty() && parseBorder(topSpec, &c, &w, &space, darkPaper))
                p.fillRect(QRectF(contentLeft, r.top() - space - w, contentRight - contentLeft, w), c);
            const QString bottomSpec = bf.stringProperty(BorderBottom);
            if (!bottomSpec.isEmpty() && parseBorder(bottomSpec, &c, &w, &space, darkPaper))
                p.fillRect(QRectF(contentLeft, r.bottom() + space, contentRight - contentLeft, w), c);

            if (showBreaks && (bf.pageBreakPolicy() & QTextFormat::PageBreak_AlwaysBefore) && b != doc->begin()) {
                // Dashed rule in the gap above the paragraph, label out in the right margin.
                const double gapTop = prevRect.isValid() ? prevRect.bottom() : r.top() - bf.topMargin();
                const double y = (gapTop + r.top()) / 2.0;
                p.setPen(QPen(darkPaper ? QColor(0x70, 0x70, 0x70) : QColor(0x9A, 0x9A, 0x9A), 1, Qt::DashLine));
                p.drawLine(QPointF(rootLeft, y), QPointF(rootRight, y));
                QFont f = p.font();
                f.setPointSizeF(6.5);
                p.setFont(f);
                p.drawText(QRectF(rootRight + 4, y - 6, 60, 12), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("break"));
            }
        }
        prevRect = r;
        prevLeft = leftSpec;
    }
}

bool PageEdit::selectNextMisspelling()
{
    spell::SpellChecker &sc = spell::SpellChecker::instance();
    if (!sc.isLoaded())
        return false;
    QTextDocument *doc = document();
    const QTextCursor cur = textCursor();
    const int from = cur.hasSelection() ? cur.selectionEnd() : cur.position();
    const QTextBlock startBlock = doc->findBlock(from);
    for (int pass = 0; pass < 2; ++pass) {
        QTextBlock b = pass == 0 ? startBlock : doc->begin();
        for (; b.isValid(); b = b.next()) {
            if (pass == 1 && b.position() > startBlock.position())
                break;
            for (const spell::Word &w : spell::SpellChecker::checkableWords(b)) {
                const int abs = b.position() + w.start;
                if (pass == 0 && b == startBlock && abs < from)
                    continue;
                if (!sc.isCorrect(w.text)) {
                    QTextCursor c(doc);
                    c.setPosition(abs);
                    c.setPosition(abs + w.length, QTextCursor::KeepAnchor);
                    setTextCursor(c);
                    ensureCursorVisible();
                    return true;
                }
            }
        }
    }
    return false;
}

void PageEdit::contextMenuEvent(QContextMenuEvent *e)
{
    std::unique_ptr<QMenu> menu(createStandardContextMenu(e->pos()));
    spell::SpellChecker &sc = spell::SpellChecker::instance();
    const QTextCursor at = cursorForPosition(e->pos());
    const QTextBlock block = at.block();
    const int offset = at.position() - block.position();
    if (sc.isEnabled() && sc.isLoaded() && !textCursor().hasSelection()) {
        for (const spell::Word &w : spell::SpellChecker::checkableWords(block)) {
            if (offset < w.start || offset > w.start + w.length || sc.isCorrect(w.text))
                continue;
            const int start = block.position() + w.start;
            const int length = w.length;
            QAction *first = menu->actions().value(0);
            const QStringList sugg = sc.suggestions(w.text);
            QList<QAction *> added;
            for (const QString &s : sugg) {
                QAction *a = new QAction(s, menu.get());
                QFont f = a->font();
                f.setBold(true);
                a->setFont(f);
                connect(a, &QAction::triggered, this, [this, start, length, s] {
                    QTextCursor fc(document());
                    fc.setPosition(start + 1);
                    const QTextCharFormat fmt = fc.charFormat();  // format of the word's first character
                    QTextCursor rc(document());
                    rc.setPosition(start);
                    rc.setPosition(start + length, QTextCursor::KeepAnchor);
                    rc.insertText(s, fmt);
                });
                added << a;
            }
            if (sugg.isEmpty()) {
                QAction *none = new QAction(tr("(no suggestions)"), menu.get());
                none->setEnabled(false);
                added << none;
            }
            QAction *sep1 = new QAction(menu.get());
            sep1->setSeparator(true);
            const QString word = w.text;
            QAction *addWord = new QAction(tr("Add \"%1\" to Dictionary").arg(word), menu.get());
            connect(addWord, &QAction::triggered, this, [word] { spell::SpellChecker::instance().addToPersonal(word); });
            QAction *ignore = new QAction(tr("Ignore All"), menu.get());
            connect(ignore, &QAction::triggered, this, [word] { spell::SpellChecker::instance().ignore(word); });
            QAction *sep2 = new QAction(menu.get());
            sep2->setSeparator(true);
            added << sep1 << addWord << ignore << sep2;
            menu->insertActions(first, added);
            break;
        }
    }
    menu->exec(e->globalPos());
}

bool PageEdit::canInsertFromMimeData(const QMimeData *source) const
{
    return source->hasImage() || QTextEdit::canInsertFromMimeData(source);
}

void PageEdit::insertFromMimeData(const QMimeData *source)
{
    // Inside a code block, paste plain text so indentation survives and no
    // outside formatting comes along. Newlines become new Code paragraphs.
    if (source->hasText() && textCursor().blockFormat().stringProperty(PStyle) == QLatin1String("Code")) {
        QString text = source->text();
        text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
        text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
        QTextCursor c = textCursor();
        c.insertText(text, c.blockCharFormat());
        return;
    }
    if (source->hasImage() && !source->hasHtml()) {
        QImage img = qvariant_cast<QImage>(source->imageData());
        if (img.isNull())
            return;
        const double maxW = twipsToPx(m_page.width - m_page.left - m_page.right);
        double w = img.width(), h = img.height();
        if (w > maxW) {
            h = h * maxW / w;
            w = maxW;
        }
        const QString name = QStringLiteral("image:") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        document()->addResource(QTextDocument::ImageResource, QUrl(name), img);
        QTextImageFormat f;
        f.setName(name);
        f.setWidth(w);
        f.setHeight(h);
        textCursor().insertImage(f);
        return;
    }
    QTextEdit::insertFromMimeData(source);
}
