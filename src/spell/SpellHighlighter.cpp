#include "spell/SpellHighlighter.h"
#include "spell/SpellChecker.h"

#include <QTextCharFormat>

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

void SpellHighlighter::highlightBlock(const QString &text)
{
    SpellChecker &sc = SpellChecker::instance();
    if (!sc.isEnabled() || !sc.isLoaded())
        return;
    QTextCharFormat bad;
    bad.setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
    bad.setUnderlineColor(QColor(0xD0, 0x21, 0x21));
    for (const Word &w : SpellChecker::words(text))
        if (!sc.isCorrect(w.text))
            setFormat(w.start, w.length, bad);
}

} // namespace spell
