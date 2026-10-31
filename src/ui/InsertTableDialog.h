#pragma once

#include <QDialog>

class QSpinBox;

class InsertTableDialog : public QDialog {
    Q_OBJECT
public:
    explicit InsertTableDialog(QWidget *parent = nullptr);
    int rows() const;
    int columns() const;

private:
    QSpinBox *m_rows;
    QSpinBox *m_cols;
};
