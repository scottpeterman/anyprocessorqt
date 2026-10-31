#pragma once

#include <QSyntaxHighlighter>

namespace spell {

// Red wavy underline under misspelled words. Display only: highlighter formats
// live in the text layout, not the document, so they never reach the saved file
// and don't touch undo or the modified flag.
class SpellHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit SpellHighlighter(QTextDocument *doc);

    // Re-check every block. QSyntaxHighlighter::rehighlight() marks the document
    // modified (no undo step, but it would trigger "unsaved changes"), so this
    // restores the flag afterwards.
    void recheck();

protected:
    void highlightBlock(const QString &text) override;
};

} // namespace spell
