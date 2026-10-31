#pragma once

#include <QColor>
#include <QString>

class QTextTable;
class QTextTableFormat;

namespace docx {

// One w:tblBorders side.
struct BorderSide {
    QString val;               // "single", "double", "dotted", "dashed", ... empty = none
    int sz = 4;                // eighths of a point
    QString color = QStringLiteral("auto");
    bool on() const { return !val.isEmpty() && val != QLatin1String("nil") && val != QLatin1String("none"); }
    bool operator==(const BorderSide &o) const { return val == o.val && sz == o.sz && color == o.color; }
};

// Table borders: top, left, bottom, right, insideH, insideV. Stored on the table
// format as TableBorderSpec when the document (or the user) sets them explicitly;
// absent means the table style decides.
struct TblBorders {
    enum Side { Top, Left, Bottom, Right, InsideH, InsideV, Count };
    BorderSide side[Count];

    enum Preset { None, All, Outside, Inside, Horizontal, Custom };
    static TblBorders preset(Preset p, int sz, const QString &color);
    Preset detect() const;

    QString spec() const;
    static TblBorders fromSpec(const QString &spec);
    // Explicit spec, else what the reader inferred (TableBorders bool: style grid).
    static TblBorders of(const QTextTableFormat &tf);
    static const char *xmlName(Side s);
};

constexpr int kDefaultCellMarLR = 108;  // twips: Word's Normal Table left/right cell margin

QColor borderColor(const QString &color);  // "auto" -> display grey
QTextTableFormat newTableFormat(int columns, double contentWidthPx);

// Re-derive per-cell display formats from the table format: each cell's border
// sides (outer vs inside), left/right padding, and one column width per column.
// Call after reading a table and after any structural edit (rows, columns,
// merge, split) or property change. Cell shading and vertical alignment kept.
void applyTableCellFormats(QTextTable *table);

} // namespace docx
