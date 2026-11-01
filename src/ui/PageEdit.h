#pragma once

#include "docx/Package.h"
#include "ui/Theme.h"

#include <QTextEdit>

class QPagedPaintDevice;
class QPainter;

namespace docx {
class HeaderFooters;
}

namespace spell {
class SpellHighlighter;
}

// Rich-text editor laid out at the document's page width, centred on a grey desk.
// Paints the paragraph borders and page-break markers Qt's layout doesn't draw.
class PageEdit : public QTextEdit {
    Q_OBJECT
public:
    explicit PageEdit(QWidget *parent = nullptr);

    enum class ViewMode { Page, Draft };
    void setViewMode(ViewMode mode);
    ViewMode viewMode() const { return m_mode; }

    // Select the next misspelled word after the cursor (wrapping once). False if none.
    bool selectNextMisspelling();

    // Embedded in ZoomView: no scrollbars or desk margins of its own; the view
    // scales, scrolls and centres it.
    void setEmbedded(bool embedded);

    // Give this editor keyboard focus. Embedded in ZoomView, the view must hold
    // window focus too, or keys go to whatever toolbar widget has it.
    void takeFocus();
    bool isEmbedded() const { return m_embedded; }

    void setPageSetup(const docx::PageSetup &page);
    const docx::PageSetup &pageSetup() const { return m_page; }
    int pageWidthPx() const;
    int pageHeightPx() const;

    // Set the text width (as QTextDocument::setTextWidth) and, in Page view, the
    // page height, so Qt's layout breaks pages exactly as print and PDF do.
    // QTextEdit and setTextWidth() both reset the height to -1, so every width
    // change goes through here.
    void setDocumentWidth(qreal width);

    // Desk (around the page) and paper colours. Display only: the document's own
    // colours are untouched, so save, print and PDF are unaffected.
    void setColors(const QColor &desk, theme::Paper paper);
    theme::Paper paper() const { return m_paper; }

    // Headers/footers shown in Page view (not owned). Null hides them.
    void setHeaderFooters(const docx::HeaderFooters *hf);

    // Print/PDF: every page at the device's full size, body as laid out here
    // (same page breaks as Page view), with paragraph borders and headers/footers.
    // fromPage/toPage are 1-based and inclusive; 0 means the first/last page.
    static void printPages(QTextDocument *doc, const docx::PageSetup &page, const docx::HeaderFooters *hf,
                           QPagedPaintDevice *device, int fromPage = 0, int toPage = 0);

    static constexpr int kPageGap = 16;  // desk band drawn between pages, document px
    static void paintPageGaps(QPainter &p, QTextDocument *doc, const QRectF &clip, const QColor &desk);

    static void paintDecorations(QPainter &p, QTextDocument *doc, const QRectF &clip, bool showBreaks,
                                 bool darkPaper = false);

protected:
    void paintEvent(QPaintEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    bool canInsertFromMimeData(const QMimeData *source) const override;
    void insertFromMimeData(const QMimeData *source) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;

signals:
    void geometryHintsChanged();
    void headerFooterRequested(bool footer, int page);
    void tablePropertiesRequested();  // double-click in a page's top/bottom margin  // view mode or page setup changed: the host should re-lay out

private:
    void updateMargins();
    void updatePalette();
    void applyPagination();
    void paintDarkCards(QPainter &p, const QRectF &clip);
    bool m_embedded = false;
    docx::PageSetup m_page;
    ViewMode m_mode = ViewMode::Page;
    spell::SpellHighlighter *m_spell = nullptr;
    QColor m_desk = QColor(0xE6, 0xE6, 0xE6);
    theme::Paper m_paper = theme::Paper::White;
    const docx::HeaderFooters *m_hf = nullptr;
};
