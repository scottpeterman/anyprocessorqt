#pragma once

#include "docx/TableFormat.h"

#include <QColor>
#include <QDialog>
#include <QList>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QTextTable;
class QToolButton;

// Table Properties: alignment, cell padding, column widths, borders, and
// vertical alignment / shading for the selected cells. apply() makes the
// change as one undo step.
class TablePropertiesDialog : public QDialog {
    Q_OBJECT
public:
    // Cells firstRow..firstRow+numRows-1 x firstCol..firstCol+numCols-1 are "selected".
    TablePropertiesDialog(QTextTable *table, double textWidthPx, int firstRow, int numRows, int firstCol, int numCols,
                          QWidget *parent = nullptr);

    void apply();

    // Pure helpers (tested).
    static QList<double> distributed(int columns, double totalPx);
    static QList<double> scaledTo(const QList<double> &widths, double totalPx);

private:
    void updateTotal();
    void setWidths(const QList<double> &px);
    QList<double> widths() const;

    QTextTable *m_table;
    double m_textWidth;
    int m_r0, m_nr, m_c0, m_nc;

    QComboBox *m_align;
    QDoubleSpinBox *m_padding;
    QList<QDoubleSpinBox *> m_cols;
    QLabel *m_total;
    QComboBox *m_preset;
    QComboBox *m_width;
    QToolButton *m_color;
    QString m_borderColor;  // "auto" or RRGGBB
    docx::TblBorders m_initialBorders;
    QComboBox *m_valign;
    QToolButton *m_shade;
    enum class Shade { Unchanged, None, Color } m_shadeMode = Shade::Unchanged;
    QColor m_shadeColor;
};
