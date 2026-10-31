#pragma once

#include "docx/Props.h"

#include <QColor>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextListFormat>
#include <QTextOption>

class QTextBlock;

namespace docx {

// Fully resolved docx props -> Qt formats (every field set explicitly).
QTextBlockFormat toBlockFormat(const ParaProps &p, bool isListItem);
QTextCharFormat toCharFormat(const RunProps &r);

// Qt formats -> docx props (every field set), for diffing against styles on save.
ParaProps fromBlockFormat(const QTextBlockFormat &bf, qreal indentWidth);
RunProps fromCharFormat(const QTextCharFormat &cf);

// Qt rounds each line to whole pixels (Carlito 9.5pt: 16px vs Word's 15.46px),
// which makes long documents ~3% taller than Word. Re-express "auto" line
// spacing as the proportional height that reproduces Word's pitch for the
// block's largest font; the docx value is kept on the block for save.
void compensateLineHeight(QTextBlockFormat &bf, const QTextCharFormat &dominant);
QTextCharFormat dominantCharFormat(const QTextBlock &block);

// Re-apply w:contextualSpacing for display around [first, last] (plus one block on
// each side): paragraphs of the same style with contextual spacing lose the space
// between them. The docx values stay in SpacingOrig for save. Edits go through
// cursors, so call inside the caller's edit block.
void applyContextualSpacing(QTextDocument *doc, const QTextBlock &first, const QTextBlock &last);

// Serialization used to detect whether display-adjusted tab stops were edited.
QString tabsKey(const QList<QTextOption::Tab> &tabs);
QString encodeTabStops(const QList<TabStop> &tabs);
QList<TabStop> decodeTabStops(const QString &s);

QColor highlightColor(const QString &name);  // invalid if unknown
QString normJc(const std::optional<QString> &jc);

// Word defaults when a property is unset everywhere.
inline constexpr int kDefaultHalfPts = 20;
inline const char *kDefaultFont = "Times New Roman";

} // namespace docx
