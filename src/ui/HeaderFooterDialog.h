#pragma once

#include <QDialog>

#include <memory>
#include <vector>

class QCheckBox;
class QTabWidget;
class QTextDocument;
class QTextEdit;

namespace docx {
struct Package;
class HeaderFooters;
}

// Edits the body section's headers and footers as text with PAGE / NUMPAGES
// fields, shown in the editor as {PAGE} and {NUMPAGES} tokens. Parts with
// content this app can't rewrite (images, tables, other fields) are read-only.
class HeaderFooterDialog : public QDialog {
    Q_OBJECT
public:
    HeaderFooterDialog(docx::Package &pkg, const docx::HeaderFooters &hf, bool focusFooter, bool focusFirstPage,
                       QWidget *parent = nullptr);
    ~HeaderFooterDialog() override;

    // Write the edits into the package. True if anything changed.
    bool apply();

    // Field fragments <-> {PAGE} / {NUMPAGES} tokens.
    static void fieldsToTokens(QTextDocument *doc);
    static void tokensToFields(QTextDocument *doc);

private:
    struct Slot {
        bool footer = false;
        QString type;
        QTextEdit *edit = nullptr;
        bool editable = true;
        bool existed = false;
    };
    QWidget *makePage(const QString &type);
    QWidget *makeEditor(bool footer, const QString &type);
    void updateTabs();

    docx::Package &m_pkg;
    const docx::HeaderFooters &m_hf;
    QCheckBox *m_firstPage = nullptr;
    QTabWidget *m_tabs = nullptr;
    std::vector<Slot> m_slots;
    bool m_initialTitlePg = false;
    int m_firstTab = -1;
    int m_defaultTab = -1;
};
