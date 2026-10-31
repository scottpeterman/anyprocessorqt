#include "ui/TablePropertiesDialog.h"
#include "docx/Units.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextTable>
#include <QToolButton>
#include <QVBoxLayout>

using namespace docx;

namespace {
constexpr double kPxPerInch = 96.0;
const int kBorderSizes[] = {4, 6, 8, 12, 18, 24};  // eighths of a point
const char *kBorderSizeNames[] = {"½ pt", "¾ pt", "1 pt", "1½ pt", "2¼ pt", "3 pt"};

QIcon swatch(const QColor &c)
{
    QPixmap pm(16, 16);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setPen(QColor(0x70, 0x70, 0x70));
    if (c.isValid())
        p.setBrush(c);
    p.drawRect(0, 0, 15, 15);
    if (!c.isValid())
        p.drawLine(1, 14, 14, 1);
    return QIcon(pm);
}
} // namespace

QList<double> TablePropertiesDialog::distributed(int columns, double totalPx)
{
    QList<double> out;
    for (int i = 0; i < columns; ++i)
        out.append(totalPx / qMax(1, columns));
    return out;
}

QList<double> TablePropertiesDialog::scaledTo(const QList<double> &widths, double totalPx)
{
    double sum = 0;
    for (double w : widths)
        sum += w;
    if (sum <= 0)
        return distributed(int(widths.size()), totalPx);
    QList<double> out;
    for (double w : widths)
        out.append(w * totalPx / sum);
    return out;
}

TablePropertiesDialog::TablePropertiesDialog(QTextTable *table, double textWidthPx, int firstRow, int numRows,
                                             int firstCol, int numCols, QWidget *parent)
    : QDialog(parent), m_table(table), m_textWidth(textWidthPx), m_r0(firstRow), m_nr(numRows), m_c0(firstCol),
      m_nc(numCols)
{
    setWindowTitle(tr("Table Properties"));
    const QTextTableFormat tf = table->format();
    auto *layout = new QVBoxLayout(this);

    // ---- table
    auto *tableBox = new QGroupBox(tr("Table"), this);
    auto *tform = new QFormLayout(tableBox);
    m_align = new QComboBox(tableBox);
    m_align->setObjectName(QStringLiteral("align"));
    m_align->addItems({tr("Left"), tr("Center"), tr("Right")});
    const Qt::Alignment a = tf.alignment() & Qt::AlignHorizontal_Mask;
    m_align->setCurrentIndex((a & Qt::AlignHCenter) ? 1 : (a & Qt::AlignRight) ? 2 : 0);
    tform->addRow(tr("Alignment:"), m_align);
    m_padding = new QDoubleSpinBox(tableBox);
    m_padding->setObjectName(QStringLiteral("padding"));
    m_padding->setRange(0, 36);
    m_padding->setSingleStep(0.5);
    m_padding->setDecimals(1);
    m_padding->setSuffix(tr(" pt"));
    m_padding->setValue((tf.hasProperty(TableCellMarLR) ? tf.intProperty(TableCellMarLR) : kDefaultCellMarLR) / 20.0);
    tform->addRow(tr("Cell padding (left/right):"), m_padding);
    layout->addWidget(tableBox);

    // ---- columns
    auto *colBox = new QGroupBox(tr("Column widths"), this);
    auto *cv = new QVBoxLayout(colBox);
    auto *gridHost = new QWidget;
    auto *grid = new QGridLayout(gridHost);
    grid->setContentsMargins(0, 0, 0, 0);
    const QList<QTextLength> cons = tf.columnWidthConstraints();
    QList<double> px;
    for (int c = 0; c < table->columns(); ++c) {
        double w = 0;
        if (c < cons.size()) {
            const QTextLength &l = cons[c];
            w = l.type() == QTextLength::FixedLength ? l.rawValue()
                : l.type() == QTextLength::PercentageLength ? m_textWidth * l.rawValue() / 100.0 : 0;
        }
        px.append(w);
    }
    if (px.contains(0.0))
        px = distributed(table->columns(), m_textWidth);
    for (int c = 0; c < table->columns(); ++c) {
        auto *sb = new QDoubleSpinBox(gridHost);
        sb->setRange(0.2, 22);
        sb->setDecimals(2);
        sb->setSingleStep(0.05);
        sb->setSuffix(tr(" in"));
        connect(sb, &QDoubleSpinBox::valueChanged, this, &TablePropertiesDialog::updateTotal);
        m_cols.append(sb);
        const int row = c / 4, col = (c % 4) * 2;
        grid->addWidget(new QLabel(tr("%1:").arg(c + 1), gridHost), row, col);
        grid->addWidget(sb, row, col + 1);
    }
    if (table->columns() > 12) {
        auto *scroll = new QScrollArea(colBox);
        scroll->setWidget(gridHost);
        scroll->setWidgetResizable(true);
        scroll->setMinimumHeight(140);
        cv->addWidget(scroll);
    } else {
        cv->addWidget(gridHost);
    }
    auto *crow = new QHBoxLayout;
    m_total = new QLabel(colBox);
    crow->addWidget(m_total, 1);
    auto *even = new QPushButton(tr("Distribute Evenly"), colBox);
    connect(even, &QPushButton::clicked, this, [this] {
        double sum = 0;
        for (double w : widths())
            sum += w;
        setWidths(distributed(int(m_cols.size()), sum));
    });
    auto *fit = new QPushButton(tr("Fit to Page Width"), colBox);
    connect(fit, &QPushButton::clicked, this, [this] { setWidths(scaledTo(widths(), m_textWidth)); });
    crow->addWidget(even);
    crow->addWidget(fit);
    cv->addLayout(crow);
    layout->addWidget(colBox);
    setWidths(px);

    // ---- borders
    auto *bBox = new QGroupBox(tr("Borders"), this);
    auto *bform = new QFormLayout(bBox);
    m_initialBorders = TblBorders::of(tf);
    m_preset = new QComboBox(bBox);
    m_preset->setObjectName(QStringLiteral("preset"));
    m_preset->addItem(tr("None"), TblBorders::None);
    m_preset->addItem(tr("All borders"), TblBorders::All);
    m_preset->addItem(tr("Outside only"), TblBorders::Outside);
    m_preset->addItem(tr("Inside only"), TblBorders::Inside);
    m_preset->addItem(tr("Horizontal lines"), TblBorders::Horizontal);
    const TblBorders::Preset cur = m_initialBorders.detect();
    if (cur == TblBorders::Custom)
        m_preset->addItem(tr("Keep current (custom)"), TblBorders::Custom);
    m_preset->setCurrentIndex(m_preset->findData(cur));
    bform->addRow(tr("Lines:"), m_preset);

    // Width and colour of the first visible side.
    BorderSide ref{QString(), 4, QStringLiteral("auto")};
    for (const BorderSide &s : m_initialBorders.side)
        if (s.on()) {
            ref = s;
            break;
        }
    m_width = new QComboBox(bBox);
    m_width->setObjectName(QStringLiteral("borderWidth"));
    for (int i = 0; i < int(std::size(kBorderSizes)); ++i)
        m_width->addItem(QString::fromUtf8(kBorderSizeNames[i]), kBorderSizes[i]);
    int wi = m_width->findData(ref.sz);
    if (wi < 0) {
        m_width->addItem(tr("%1 pt").arg(ref.sz / 8.0), ref.sz);
        wi = m_width->count() - 1;
    }
    m_width->setCurrentIndex(wi);
    bform->addRow(tr("Width:"), m_width);

    m_borderColor = ref.color;
    m_color = new QToolButton(bBox);
    m_color->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_color->setPopupMode(QToolButton::InstantPopup);
    auto showColor = [this] {
        const bool autoC = m_borderColor == QLatin1String("auto");
        m_color->setText(autoC ? tr("Automatic") : QLatin1Char('#') + m_borderColor);
        m_color->setIcon(swatch(autoC ? QColor(Qt::black) : QColor(QLatin1Char('#') + m_borderColor)));
    };
    auto *cmenu = new QMenu(m_color);
    cmenu->addAction(tr("Automatic"), this, [this, showColor] {
        m_borderColor = QStringLiteral("auto");
        showColor();
    });
    cmenu->addAction(tr("Choose…"), this, [this, showColor] {
        const QColor c = QColorDialog::getColor(
            m_borderColor == QLatin1String("auto") ? QColor(Qt::black) : QColor(QLatin1Char('#') + m_borderColor), this,
            tr("Border Color"));
        if (c.isValid()) {
            m_borderColor = c.name(QColor::HexRgb).mid(1).toUpper();
            showColor();
        }
    });
    m_color->setMenu(cmenu);
    showColor();
    bform->addRow(tr("Color:"), m_color);
    layout->addWidget(bBox);

    // ---- cells
    const bool many = m_nr * m_nc > 1;
    auto *cBox = new QGroupBox(many ? tr("Selected cells") : tr("This cell"), this);
    auto *cform = new QFormLayout(cBox);
    m_valign = new QComboBox(cBox);
    m_valign->setObjectName(QStringLiteral("valign"));
    m_valign->addItem(tr("Top"), int(QTextCharFormat::AlignNormal));
    m_valign->addItem(tr("Center"), int(QTextCharFormat::AlignMiddle));
    m_valign->addItem(tr("Bottom"), int(QTextCharFormat::AlignBottom));
    // Common value across the selection, else "unchanged".
    int common = -2;
    QColor commonBg;
    bool bgMixed = false, bgFirst = true;
    for (int r = m_r0; r < m_r0 + m_nr; ++r)
        for (int c = m_c0; c < m_c0 + m_nc; ++c) {
            const QTextTableCell cell = table->cellAt(r, c);
            if (!cell.isValid())
                continue;
            int va = int(cell.format().verticalAlignment());
            if (va != QTextCharFormat::AlignMiddle && va != QTextCharFormat::AlignBottom)
                va = QTextCharFormat::AlignNormal;
            common = common == -2 ? va : (common == va ? va : -1);
            const QBrush b = cell.format().background();
            const QColor bg = b.style() == Qt::NoBrush ? QColor() : b.color();
            if (bgFirst) {
                commonBg = bg;
                bgFirst = false;
            } else if (bg != commonBg) {
                bgMixed = true;
            }
        }
    if (common == -1) {
        m_valign->insertItem(0, tr("(mixed — unchanged)"), -1);
        m_valign->setCurrentIndex(0);
    } else {
        m_valign->setCurrentIndex(qMax(0, m_valign->findData(common)));
    }
    cform->addRow(tr("Vertical alignment:"), m_valign);

    m_shade = new QToolButton(cBox);
    m_shade->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_shade->setPopupMode(QToolButton::InstantPopup);
    m_shadeColor = commonBg;
    auto showShade = [this, bgMixed] {
        if (m_shadeMode == Shade::Unchanged && bgMixed) {
            m_shade->setText(tr("(mixed — unchanged)"));
            m_shade->setIcon(QIcon());
            return;
        }
        const bool none = m_shadeMode == Shade::None || (m_shadeMode == Shade::Unchanged && !m_shadeColor.isValid());
        m_shade->setText(none ? tr("No shading") : m_shadeColor.name(QColor::HexRgb).toUpper());
        m_shade->setIcon(swatch(none ? QColor() : m_shadeColor));
    };
    auto *smenu = new QMenu(m_shade);
    smenu->addAction(tr("No shading"), this, [this, showShade] {
        m_shadeMode = Shade::None;
        showShade();
    });
    smenu->addAction(tr("Choose…"), this, [this, showShade] {
        const QColor c = QColorDialog::getColor(m_shadeColor.isValid() ? m_shadeColor : QColor(0xDD, 0xEB, 0xF7), this,
                                                tr("Cell Shading"));
        if (c.isValid()) {
            m_shadeMode = Shade::Color;
            m_shadeColor = c;
            showShade();
        }
    });
    m_shade->setMenu(smenu);
    showShade();
    cform->addRow(tr("Shading:"), m_shade);
    layout->addWidget(cBox);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QList<double> TablePropertiesDialog::widths() const
{
    QList<double> out;
    for (QDoubleSpinBox *sb : m_cols)
        out.append(sb->value() * kPxPerInch);
    return out;
}

void TablePropertiesDialog::setWidths(const QList<double> &px)
{
    for (int i = 0; i < m_cols.size() && i < px.size(); ++i) {
        const QSignalBlocker block(m_cols[i]);
        m_cols[i]->setValue(px[i] / kPxPerInch);
    }
    updateTotal();
}

void TablePropertiesDialog::updateTotal()
{
    double sum = 0;
    for (double w : widths())
        sum += w;
    const double page = m_textWidth / kPxPerInch;
    const double total = sum / kPxPerInch;
    QString text = tr("Total %1 in of %2 in text width").arg(total, 0, 'f', 2).arg(page, 0, 'f', 2);
    if (total > page + 0.005)
        text += tr(" — wider than the page");
    m_total->setText(text);
}

void TablePropertiesDialog::apply()
{
    QTextDocument *doc = m_table->document();
    QTextCursor block(doc);
    block.beginEditBlock();  // one undo step

    QTextTableFormat tf = m_table->format();
    const Qt::Alignment align = m_align->currentIndex() == 1 ? Qt::AlignHCenter
                                : m_align->currentIndex() == 2 ? Qt::AlignRight : Qt::AlignLeft;
    tf.setAlignment(align);
    tf.setProperty(TableCellMarLR, int(qRound(m_padding->value() * 20)));
    QList<QTextLength> cons;
    for (double w : widths())
        cons.append(QTextLength(QTextLength::FixedLength, w));
    tf.setColumnWidthConstraints(cons);

    const auto preset = TblBorders::Preset(m_preset->currentData().toInt());
    const int sz = m_width->currentData().toInt();
    TblBorders b = m_initialBorders;
    if (preset != TblBorders::Custom) {
        b = TblBorders::preset(preset, sz, m_borderColor);
    } else {
        for (BorderSide &s : b.side)
            if (s.on()) {
                s.sz = sz;
                s.color = m_borderColor;
            }
    }
    tf.setProperty(TableBorderSpec, b.spec());
    tf.setProperty(TableBorders, b.detect() != TblBorders::None);
    m_table->setFormat(tf);

    const int va = m_valign->currentData().toInt();
    for (int r = m_r0; r < m_r0 + m_nr; ++r)
        for (int c = m_c0; c < m_c0 + m_nc; ++c) {
            QTextTableCell cell = m_table->cellAt(r, c);
            if (!cell.isValid() || cell.row() != r || cell.column() != c)
                continue;
            QTextTableCellFormat f = cell.format().toTableCellFormat();
            if (va >= 0)
                f.setVerticalAlignment(QTextCharFormat::VerticalAlignment(va));
            if (m_shadeMode == Shade::None)
                f.clearBackground();
            else if (m_shadeMode == Shade::Color)
                f.setBackground(m_shadeColor);
            cell.setFormat(f);
        }
    applyTableCellFormats(m_table);
    block.endEditBlock();
}
