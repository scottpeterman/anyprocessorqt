#pragma once

#include <QTextFormat>
#include <QtMath>

namespace docx {

// QTextDocument works in logical pixels at 96 DPI.
// 1 inch = 1440 twips = 96 px  ->  1 px = 15 twips.
// 1 inch = 914400 EMU          ->  1 px = 9525 EMU.
inline double twipsToPx(double tw) { return tw / 15.0; }
inline int pxToTwips(double px) { return int(qRound(px * 15.0)); }
inline double emuToPx(double emu) { return emu / 9525.0; }
inline qint64 pxToEmu(double px) { return qRound64(px * 9525.0); }
inline double ptToPx(double pt) { return pt * 96.0 / 72.0; }

// Custom properties carried on Qt formats so the writer can round-trip
// docx concepts Qt has no field for.
enum Prop : int {
    PStyle = QTextFormat::UserProperty + 1,  // block: paragraph style id
    KeepNext,                                // block: bool
    KeepLines,                               // block: bool
    BorderTop,                               // block: "val|sz|space|color"
    BorderLeft,
    BorderBottom,
    BorderRight,
    NumId,                                   // list format: original w:numId
    Ilvl,                                    // list format: original w:ilvl
    NumFromStyle,                            // list format: numbering came from the paragraph style
    RStyle,                                  // char: character style id
    Highlight,                               // char: w:highlight name
    TableStyle,                              // table: w:tblStyle id
    TableBorders,                            // table: bool
    TabsOrig,                                // block: original tab stops "val@twips;..." when display was adjusted
    TabsShown,                               // block: tab positions as adjusted for display
    ContextualSpacing,                       // block: bool (w:contextualSpacing)
    SpacingOrig,                             // block: "before,after" twips when display suppressed spacing
    SpacingShown,                            // block: "top,bottom" px as displayed
    LineOrig,                                // block: "line|rule" docx spacing when display compensated
    LineShown,                               // block: line height as displayed ("type|value")
};

} // namespace docx
