#pragma once

#include <QColor>
#include <QSyntaxHighlighter>

namespace spell {

// Display-only formatting layered over the document:
//  - red wavy underline under misspelled words;
//  - on dark paper, readable ink for explicitly coloured text (dark colours are
//    lightened; text on a light shading, highlight or cell fill keeps dark ink).
// Highlighter formats live in the text layout, not the document, so they never
// reach the saved file, print or PDF, and don't touch undo or the modified flag.
class SpellHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit SpellHighlighter(QTextDocument *doc);

    // Re-check every block. QSyntaxHighlighter::rehighlight() marks the document
    // modified (no undo step, but it would trigger "unsaved changes"), so this
    // restores the flag afterwards.
    void recheck();

    void setDarkPaper(bool dark);
    bool darkPaper() const { return m_dark; }

    // Colour to show for `c` drawn straight on dark paper: dark colours become a
    // light tint of the same hue; colours already light enough are kept.
    static QColor onDarkPaper(const QColor &c);

    // Near-white, near-grey shading (code blocks, inline code). On dark paper it is
    // shown as a dark card instead: PageEdit repaints paragraph shading, and the
    // highlighter swaps character shading.
    static bool isNeutralLight(const QColor &c);
    static QColor darkCard();

protected:
    void highlightBlock(const QString &text) override;

private:
    void applyDarkPaperInk();
    bool m_dark = false;
};

} // namespace spell
