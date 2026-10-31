#pragma once

#include "docx/Package.h"
#include "ui/Theme.h"

#include <QTextEdit>

class QPainter;

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
    bool isEmbedded() const { return m_embedded; }

    void setPageSetup(const docx::PageSetup &page);
    const docx::PageSetup &pageSetup() const { return m_page; }
    int pageWidthPx() const;

    // Desk (around the page) and paper colours. Display only: the document's own
    // colours are untouched, so save, print and PDF are unaffected.
    void setColors(const QColor &desk, theme::Paper paper);
    theme::Paper paper() const { return m_paper; }

    static void paintDecorations(QPainter &p, QTextDocument *doc, const QRectF &clip, bool showBreaks,
                                 bool darkPaper = false);

protected:
    void paintEvent(QPaintEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    bool canInsertFromMimeData(const QMimeData *source) const override;
    void insertFromMimeData(const QMimeData *source) override;
    void contextMenuEvent(QContextMenuEvent *e) override;

signals:
    void geometryHintsChanged();  // view mode or page setup changed: the host should re-lay out

private:
    void updateMargins();
    void updatePalette();
    bool m_embedded = false;
    docx::PageSetup m_page;
    ViewMode m_mode = ViewMode::Page;
    spell::SpellHighlighter *m_spell = nullptr;
    QColor m_desk = QColor(0xE6, 0xE6, 0xE6);
    theme::Paper m_paper = theme::Paper::White;
};
