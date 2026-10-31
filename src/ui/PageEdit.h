#pragma once

#include "docx/Package.h"

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

    static void paintDecorations(QPainter &p, QTextDocument *doc, const QRectF &clip, bool showBreaks);

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
    bool m_embedded = false;
    docx::PageSetup m_page;
    ViewMode m_mode = ViewMode::Page;
    spell::SpellHighlighter *m_spell = nullptr;
};
