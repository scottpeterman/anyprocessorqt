#include "spell/SpellHighlighter.h"
#include "spell/SpellChecker.h"

#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextTable>

namespace spell {

SpellHighlighter::SpellHighlighter(QTextDocument *doc) : QSyntaxHighlighter(doc)
{
    connect(&SpellChecker::instance(), &SpellChecker::changed, this, &SpellHighlighter::recheck);
}

void SpellHighlighter::recheck()
{
    QTextDocument *doc = document();
    if (!doc)
        return;
    const bool modified = doc->isModified();
    rehighlight();
    doc->setModified(modified);
}

void SpellHighlighter::setDarkPaper(bool dark)
{
    if (m_dark == dark)
        return;
    m_dark = dark;
    recheck();
}

QColor SpellHighlighter::onDarkPaper(const QColor &c)
{
    float h, s, l, a;
    c.getHslF(&h, &s, &l, &a);
    if (l >= 0.62f)
        return c;
    // Invert lightness into the light band, capped so black lands on the paper's
    // own ink tone rather than pure white.
    const float nl = qBound(0.68f, 1.0f - l * 0.6f, 0.86f);
    return QColor::fromHslF(h, s, nl, a);
}

static bool isLight(const QColor &c)
{
    return c.lightnessF() > 0.5;
}

void SpellHighlighter::applyDarkPaperInk()
{
    const QTextBlock block = currentBlock();
    QColor under;  // shading behind the whole block: paragraph shading, else table cell fill
    const QTextBlockFormat bf = block.blockFormat();
    if (bf.background().style() != Qt::NoBrush) {
        under = bf.background().color();
    } else if (QTextTable *t = QTextCursor(block).currentTable()) {
        const QTextCharFormat cellFmt = t->cellAt(block.position()).format();
        if (cellFmt.background().style() != Qt::NoBrush)
            under = cellFmt.background().color();
    }

    for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment frag = it.fragment();
        if (!frag.isValid())
            continue;
        const QTextCharFormat cf = frag.charFormat();
        if (cf.isImageFormat())
            continue;
        const bool hasInk = cf.foreground().style() != Qt::NoBrush;
        const QColor bg = cf.background().style() != Qt::NoBrush ? cf.background().color() : under;

        QColor ink;
        if (bg.isValid()) {
            // Text on its own fill: keep the author's colour; "auto" text on a light
            // fill needs dark ink (the paper's light ink would vanish).
            if (!hasInk && isLight(bg))
                ink = Qt::black;
        } else if (hasInk) {
            const QColor orig = cf.foreground().color();
            const QColor shown = onDarkPaper(orig);
            if (shown != orig)
                ink = shown;
        }
        if (!ink.isValid())
            continue;
        QTextCharFormat f;
        f.setForeground(ink);
        setFormat(frag.position() - block.position(), frag.length(), f);
    }
}

void SpellHighlighter::highlightBlock(const QString &text)
{
    Q_UNUSED(text);
    if (m_dark)
        applyDarkPaperInk();

    SpellChecker &sc = SpellChecker::instance();
    if (!sc.isEnabled() || !sc.isLoaded())
        return;
    for (const Word &w : SpellChecker::checkableWords(currentBlock())) {
        if (sc.isCorrect(w.text))
            continue;
        // Add the underline to whatever display format is already there (dark-paper
        // ink), one run of identical formats at a time.
        int i = w.start;
        const int end = w.start + w.length;
        while (i < end) {
            QTextCharFormat f = format(i);
            int j = i + 1;
            while (j < end && format(j) == f)
                ++j;
            f.setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
            f.setUnderlineColor(QColor(0xD0, 0x21, 0x21));
            setFormat(i, j - i, f);
            i = j;
        }
    }
}

} // namespace spell
