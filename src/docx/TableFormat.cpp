#include "docx/TableFormat.h"
#include "docx/Units.h"

#include <QTextTable>
#include <QTextTableCellFormat>

namespace docx {

const char *TblBorders::xmlName(Side s)
{
    static const char *names[Count] = {"top", "left", "bottom", "right", "insideH", "insideV"};
    return names[s];
}

TblBorders TblBorders::preset(Preset p, int sz, const QString &color)
{
    TblBorders b;
    BorderSide on{QStringLiteral("single"), sz, color};
    BorderSide off{QString(), sz, color};
    for (int i = 0; i < Count; ++i)
        b.side[i] = off;
    auto set = [&](std::initializer_list<Side> sides) {
        for (Side s : sides)
            b.side[s] = on;
    };
    switch (p) {
    case All: set({Top, Left, Bottom, Right, InsideH, InsideV}); break;
    case Outside: set({Top, Left, Bottom, Right}); break;
    case Inside: set({InsideH, InsideV}); break;
    case Horizontal: set({Top, Bottom, InsideH}); break;
    default: break;
    }
    return b;
}

TblBorders::Preset TblBorders::detect() const
{
    bool o[Count];
    for (int i = 0; i < Count; ++i)
        o[i] = side[i].on();
    auto is = [&](bool t, bool l, bool b, bool r, bool ih, bool iv) {
        return o[Top] == t && o[Left] == l && o[Bottom] == b && o[Right] == r && o[InsideH] == ih && o[InsideV] == iv;
    };
    if (is(false, false, false, false, false, false))
        return None;
    if (is(true, true, true, true, true, true))
        return All;
    if (is(true, true, true, true, false, false))
        return Outside;
    if (is(false, false, false, false, true, true))
        return Inside;
    if (is(true, false, true, false, true, false))
        return Horizontal;
    return Custom;
}

QString TblBorders::spec() const
{
    QStringList parts;
    for (const BorderSide &s : side)
        parts << (s.on() ? QStringLiteral("%1|%2|%3").arg(s.val).arg(s.sz).arg(s.color) : QString());
    return parts.join(QLatin1Char(';'));
}

TblBorders TblBorders::fromSpec(const QString &spec)
{
    TblBorders b;
    const QStringList parts = spec.split(QLatin1Char(';'));
    for (int i = 0; i < Count && i < parts.size(); ++i) {
        const QStringList f = parts[i].split(QLatin1Char('|'));
        if (f.size() == 3)
            b.side[i] = BorderSide{f[0], f[1].toInt(), f[2]};
    }
    return b;
}

TblBorders TblBorders::of(const QTextTableFormat &tf)
{
    const QString spec = tf.stringProperty(TableBorderSpec);
    if (!spec.isEmpty())
        return fromSpec(spec);
    // Tables made with Qt's uniform border (older builds, pasted HTML) count as a grid.
    return preset(tf.boolProperty(TableBorders) || tf.border() > 0 ? All : None, 4, QStringLiteral("auto"));
}

QColor borderColor(const QString &color)
{
    if (color.isEmpty() || color.compare(QLatin1String("auto"), Qt::CaseInsensitive) == 0)
        return QColor(0x80, 0x80, 0x80);  // shows on white and dark paper
    return QColor(QLatin1Char('#') + color);
}

QTextTableFormat newTableFormat(int columns, double contentWidthPx)
{
    QTextTableFormat tf;
    tf.setBorder(0);
    tf.setBorderCollapse(true);
    tf.setCellSpacing(0);
    tf.setCellPadding(4);
    tf.setProperty(TableBorders, true);
    tf.setProperty(TableBorderSpec, TblBorders::preset(TblBorders::All, 4, QStringLiteral("auto")).spec());
    QList<QTextLength> widths;
    for (int i = 0; i < columns; ++i)
        widths.append(QTextLength(QTextLength::FixedLength, contentWidthPx / columns));
    tf.setColumnWidthConstraints(widths);
    return tf;
}

static QTextFrameFormat::BorderStyle qtStyle(const QString &val)
{
    if (val == QLatin1String("dotted"))
        return QTextFrameFormat::BorderStyle_Dotted;
    if (val == QLatin1String("dashed") || val == QLatin1String("dashSmallGap"))
        return QTextFrameFormat::BorderStyle_Dashed;
    if (val == QLatin1String("double"))
        return QTextFrameFormat::BorderStyle_Double;
    return QTextFrameFormat::BorderStyle_Solid;
}

void applyTableCellFormats(QTextTable *table)
{
    QTextTableFormat tf = table->format();
    const int rows = table->rows(), cols = table->columns();
    bool tfChanged = false;

    // Borders are drawn per cell; the table's own (uniform) border stays off.
    if (tf.border() != 0) {
        if (!tf.hasProperty(TableBorderSpec))
            tf.setProperty(TableBorders, true);
        tf.setBorder(0);
        tfChanged = true;
    }
    if (!tf.borderCollapse()) {
        tf.setBorderCollapse(true);
        tfChanged = true;
    }
    // One width per column: inserting/removing columns can leave the list stale.
    QList<QTextLength> widths = tf.columnWidthConstraints();
    if (!widths.isEmpty() && widths.size() != cols) {
        double total = 0;
        for (const QTextLength &l : widths)
            total += l.type() == QTextLength::FixedLength ? l.rawValue() : 0;
        QList<QTextLength> fixed;
        for (int c = 0; c < cols; ++c)
            fixed.append(QTextLength(QTextLength::FixedLength, total > 0 ? total / cols : 96));
        tf.setColumnWidthConstraints(fixed);
        tfChanged = true;
    }
    if (tfChanged)
        table->setFormat(tf);

    const TblBorders b = TblBorders::of(tf);
    const double pad = twipsToPx(tf.hasProperty(TableCellMarLR) ? tf.intProperty(TableCellMarLR) : kDefaultCellMarLR);
    auto width = [](const BorderSide &s) { return s.on() ? qMax(1.0, s.sz / 6.0) : 0.0; };

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            QTextTableCell cell = table->cellAt(r, c);
            if (!cell.isValid() || cell.row() != r || cell.column() != c)
                continue;
            const BorderSide &top = r == 0 ? b.side[TblBorders::Top] : b.side[TblBorders::InsideH];
            const BorderSide &bottom = r + cell.rowSpan() >= rows ? b.side[TblBorders::Bottom] : b.side[TblBorders::InsideH];
            const BorderSide &left = c == 0 ? b.side[TblBorders::Left] : b.side[TblBorders::InsideV];
            const BorderSide &right = c + cell.columnSpan() >= cols ? b.side[TblBorders::Right] : b.side[TblBorders::InsideV];

            QTextTableCellFormat f = cell.format().toTableCellFormat();
            QTextTableCellFormat want = f;
            want.setTopBorder(width(top));
            want.setBottomBorder(width(bottom));
            want.setLeftBorder(width(left));
            want.setRightBorder(width(right));
            want.setTopBorderBrush(borderColor(top.color));
            want.setBottomBorderBrush(borderColor(bottom.color));
            want.setLeftBorderBrush(borderColor(left.color));
            want.setRightBorderBrush(borderColor(right.color));
            want.setTopBorderStyle(qtStyle(top.val));
            want.setBottomBorderStyle(qtStyle(bottom.val));
            want.setLeftBorderStyle(qtStyle(left.val));
            want.setRightBorderStyle(qtStyle(right.val));
            want.setLeftPadding(pad);
            want.setRightPadding(pad);
            if (want != f)
                cell.setFormat(want);
        }
    }
}

} // namespace docx
