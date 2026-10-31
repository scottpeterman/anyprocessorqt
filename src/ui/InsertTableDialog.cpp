#include "ui/InsertTableDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSpinBox>

InsertTableDialog::InsertTableDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Insert Table"));
    m_rows = new QSpinBox(this);
    m_rows->setRange(1, 500);
    m_rows->setValue(3);
    m_cols = new QSpinBox(this);
    m_cols->setRange(1, 63);  // Word's column limit
    m_cols->setValue(3);
    auto *form = new QFormLayout(this);
    form->addRow(tr("Rows:"), m_rows);
    form->addRow(tr("Columns:"), m_cols);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(buttons);
}

int InsertTableDialog::rows() const { return m_rows->value(); }
int InsertTableDialog::columns() const { return m_cols->value(); }
