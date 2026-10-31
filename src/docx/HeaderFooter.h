#pragma once

#include "docx/Package.h"

#include <QColor>
#include <QString>

#include <functional>
#include <memory>
#include <vector>

class QPainter;
class QTextDocument;

namespace docx {

// The body section's headers and footers, parsed into their own documents for
// display and print. PAGE / NUMPAGES results are filled in per page when painted.
class HeaderFooters {
public:
    HeaderFooters();
    ~HeaderFooters();
    HeaderFooters(const HeaderFooters &) = delete;
    HeaderFooters &operator=(const HeaderFooters &) = delete;

    void load(const Package &pkg);
    void clear();
    bool isEmpty() const { return m_entries.empty(); }

    // Document for one header/footer type ("default", "first", "even"), or null.
    QTextDocument *doc(bool footer, const QString &type) const;
    // Text-only parts this app can rewrite without losing anything.
    bool editable(bool footer, const QString &type) const;
    // Which type applies to a page (0-based), given titlePg / evenAndOdd.
    QString typeForPage(int page) const;

    // Paint page `page` of `pageCount`'s header and footer; the painter's origin
    // is the page's top-left corner, in document pixels. `ink` maps explicit text
    // colours for display (dark paper); null keeps them.
    void paint(QPainter &p, int page, int pageCount, const PageSetup &setup, const QColor &defaultInk,
               const std::function<QColor(const QColor &)> &ink = {}) const;

    // Fill PAGE / NUMPAGES results in a header/footer document.
    static void fillFields(QTextDocument *doc, int pageNumber, int pageCount);
    // True if the part's XML is plain text (+ PAGE/NUMPAGES fields).
    static bool isTextOnly(const QByteArray &xml);

private:
    struct Entry {
        bool footer = false;
        QString type;
        std::unique_ptr<QTextDocument> doc;
        bool editable = false;
    };
    std::vector<Entry> m_entries;
    bool m_titlePg = false;
    bool m_evenAndOdd = false;
};

} // namespace docx
