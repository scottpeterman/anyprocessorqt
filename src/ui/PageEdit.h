#pragma once

#include "docx/Package.h"

#include <QTextEdit>

class QPainter;

// Rich-text editor laid out at the document's page width, centred on a grey desk.
// Paints the paragraph borders and page-break markers Qt's layout doesn't draw.
class PageEdit : public QTextEdit {
    Q_OBJECT
public:
    explicit PageEdit(QWidget *parent = nullptr);

    void setPageSetup(const docx::PageSetup &page);
    const docx::PageSetup &pageSetup() const { return m_page; }
    int pageWidthPx() const;

    static void paintDecorations(QPainter &p, QTextDocument *doc, const QRectF &clip, bool showBreaks);

protected:
    void paintEvent(QPaintEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    bool canInsertFromMimeData(const QMimeData *source) const override;
    void insertFromMimeData(const QMimeData *source) override;

private:
    void updateMargins();
    docx::PageSetup m_page;
};
