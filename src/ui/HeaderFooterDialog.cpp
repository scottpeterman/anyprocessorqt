#include "ui/HeaderFooterDialog.h"
#include "docx/Convert.h"
#include "docx/DocxReader.h"
#include "docx/DocxWriter.h"
#include "docx/HeaderFooter.h"
#include "docx/Package.h"
#include "docx/Units.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QRegularExpression>
#include <QTabWidget>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextFrame>
#include <QToolButton>
#include <QVBoxLayout>

#include <memory>

using namespace docx;

namespace {
constexpr int kFrameMargin = 4;

const QRegularExpression &tokenRe()
{
    static const QRegularExpression re(QStringLiteral("\\{(PAGE|NUMPAGES)\\}"));
    return re;
}
} // namespace

void HeaderFooterDialog::fieldsToTokens(QTextDocument *doc)
{
    struct Hit {
        int pos, len;
        QString field;
        QTextCharFormat fmt;
    };
    QList<Hit> hits;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            const QString field = f.charFormat().stringProperty(FieldInstr);
            if (!field.isEmpty())
                hits.append({f.position(), f.length(), field, f.charFormat()});
        }
    for (int i = int(hits.size()) - 1; i >= 0; --i) {
        QTextCharFormat fmt = hits[i].fmt;
        fmt.clearProperty(FieldInstr);
        QTextCursor c(doc);
        c.setPosition(hits[i].pos);
        c.setPosition(hits[i].pos + hits[i].len, QTextCursor::KeepAnchor);
        c.insertText(QLatin1Char('{') + hits[i].field + QLatin1Char('}'), fmt);
    }
}

void HeaderFooterDialog::tokensToFields(QTextDocument *doc)
{
    struct Hit {
        int pos, len;
        QString field;
    };
    QList<Hit> hits;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        auto it = tokenRe().globalMatch(b.text());
        while (it.hasNext()) {
            const auto m = it.next();
            hits.append({b.position() + int(m.capturedStart()), int(m.capturedLength()), m.captured(1)});
        }
    }
    for (int i = int(hits.size()) - 1; i >= 0; --i) {
        QTextCursor at(doc);
        at.setPosition(hits[i].pos + 1);
        QTextCharFormat fmt = at.charFormat();
        fmt.setProperty(FieldInstr, hits[i].field);
        QTextCursor c(doc);
        c.setPosition(hits[i].pos);
        c.setPosition(hits[i].pos + hits[i].len, QTextCursor::KeepAnchor);
        c.insertText(QStringLiteral("1"), fmt);
    }
}

HeaderFooterDialog::HeaderFooterDialog(Package &pkg, const HeaderFooters &hf, bool focusFooter, bool focusFirstPage,
                                       QWidget *parent)
    : QDialog(parent), m_pkg(pkg), m_hf(hf)
{
    setWindowTitle(tr("Header & Footer"));
    m_initialTitlePg = pkg.titlePg;

    auto *layout = new QVBoxLayout(this);
    m_firstPage = new QCheckBox(tr("Different first page"), this);
    m_firstPage->setChecked(pkg.titlePg);
    layout->addWidget(m_firstPage);

    m_tabs = new QTabWidget(this);
    m_firstTab = m_tabs->addTab(makePage(QStringLiteral("first")), tr("First Page"));
    m_defaultTab = m_tabs->addTab(makePage(QStringLiteral("default")), QString());
    if (pkg.evenAndOdd)
        m_tabs->addTab(makePage(QStringLiteral("even")), tr("Even Pages"));
    layout->addWidget(m_tabs, 1);

    auto *hint = new QLabel(tr("Tab moves to the centre and right tab stops. {PAGE} and {NUMPAGES} are filled in "
                               "on each page."),
                            this);
    hint->setWordWrap(true);
    QFont small = hint->font();
    small.setPointSizeF(small.pointSizeF() * 0.9);
    hint->setFont(small);
    layout->addWidget(hint);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(m_firstPage, &QCheckBox::toggled, this, &HeaderFooterDialog::updateTabs);
    updateTabs();
    m_tabs->setCurrentIndex(focusFirstPage && pkg.titlePg ? m_firstTab : m_defaultTab);
    const QString want = focusFirstPage && pkg.titlePg ? QStringLiteral("first") : QStringLiteral("default");
    for (const Slot &s : m_slots)
        if (s.type == want && s.footer == focusFooter && s.edit->isEnabled())
            s.edit->setFocus();
}

HeaderFooterDialog::~HeaderFooterDialog() = default;

void HeaderFooterDialog::updateTabs()
{
    const bool first = m_firstPage->isChecked();
    m_tabs->setTabVisible(m_firstTab, first);
    m_tabs->setTabText(m_defaultTab, first ? tr("Other Pages") : tr("All Pages"));
}

QWidget *HeaderFooterDialog::makePage(const QString &type)
{
    auto *page = new QWidget(this);
    auto *v = new QVBoxLayout(page);
    v->addWidget(makeEditor(false, type));
    v->addWidget(makeEditor(true, type));
    v->addStretch(1);
    return page;
}

QWidget *HeaderFooterDialog::makeEditor(bool footer, const QString &type)
{
    auto *box = new QWidget(this);
    auto *v = new QVBoxLayout(box);
    v->setContentsMargins(0, 0, 0, 0);
    auto *bar = new QHBoxLayout;
    bar->addWidget(new QLabel(footer ? tr("<b>Footer</b>") : tr("<b>Header</b>"), box));
    bar->addStretch(1);
    v->addLayout(bar);

    auto *edit = new QTextEdit(box);
    edit->setAcceptRichText(false);
    edit->setLineWrapMode(QTextEdit::FixedPixelWidth);
    const int width = int(twipsToPx(m_pkg.page.width - m_pkg.page.left - m_pkg.page.right));
    // Page text width plus the 4px frame margin each side, so tab stops land
    // exactly where they do on the page.
    edit->setLineWrapColumnOrWidth(width + 2 * kFrameMargin + 2);
    edit->setMinimumWidth(width + 2 * kFrameMargin + 2 + 2 * edit->frameWidth() + 4);
    edit->setFixedHeight(96);
    v->addWidget(edit);

    Slot slot;
    slot.footer = footer;
    slot.type = type;
    slot.edit = edit;
    slot.editable = m_hf.editable(footer, type);

    QTextDocument *d = nullptr;
    if (const QTextDocument *src = m_hf.doc(footer, type)) {
        d = src->clone(edit);
        slot.existed = true;
    } else {
        d = new QTextDocument(edit);
        DocxReader::applyPageSetup(m_pkg, d);
        QString style = footer ? QStringLiteral("Footer") : QStringLiteral("Header");
        if (!m_pkg.styles.style(style))
            style = m_pkg.styles.defaultParagraphStyle();
        QTextBlockFormat bf = toBlockFormat(m_pkg.styles.resolvedPara(style), false);
        bf.setProperty(PStyle, style);
        bf.setTopMargin(0);
        bf.setBottomMargin(0);
        if (bf.tabPositions().isEmpty())
            bf.setTabPositions({QTextOption::Tab(width / 2.0, QTextOption::CenterTab),
                                QTextOption::Tab(width - 1.5, QTextOption::RightTab)});
        QTextCursor c(d);
        c.setBlockFormat(bf);
        const QTextCharFormat cf = toCharFormat(m_pkg.styles.resolvedParaRun(style));
        c.setBlockCharFormat(cf);
        c.setCharFormat(cf);
    }
    QTextFrameFormat ff = d->rootFrame()->frameFormat();
    ff.setMargin(kFrameMargin);
    d->rootFrame()->setFrameFormat(ff);
    fieldsToTokens(d);
    d->setModified(false);
    d->clearUndoRedoStacks();
    edit->setDocument(d);
    if (!slot.existed)
        edit->setCurrentCharFormat(QTextCursor(d).charFormat());

    // Show tokens as fields.
    auto mark = [edit] {
        QList<QTextEdit::ExtraSelection> sels;
        QTextCharFormat f;
        QColor bg = edit->palette().color(QPalette::Highlight);
        bg.setAlpha(80);  // readable over light or dark editor backgrounds
        f.setBackground(bg);
        for (QTextBlock b = edit->document()->begin(); b.isValid(); b = b.next()) {
            auto it = tokenRe().globalMatch(b.text());
            while (it.hasNext()) {
                const auto m = it.next();
                QTextEdit::ExtraSelection s;
                s.cursor = QTextCursor(edit->document());
                s.cursor.setPosition(b.position() + int(m.capturedStart()));
                s.cursor.setPosition(b.position() + int(m.capturedEnd()), QTextCursor::KeepAnchor);
                s.format = f;
                sels.append(s);
            }
        }
        edit->setExtraSelections(sels);
    };
    connect(edit, &QTextEdit::textChanged, edit, mark);
    mark();

    auto button = [&](const QString &text, const QString &tip, auto fn) {
        auto *b = new QToolButton(box);
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        b->setEnabled(slot.editable);
        connect(b, &QToolButton::clicked, edit, [edit, fn] {
            fn(edit);
            edit->setFocus();
        });
        bar->addWidget(b);
    };
    auto align = [](Qt::Alignment a) {
        return [a](QTextEdit *e) {
            QTextCursor c = e->textCursor();
            QTextBlockFormat bf;
            bf.setAlignment(a);
            c.mergeBlockFormat(bf);
        };
    };
    button(tr("Left"), tr("Align left"), align(Qt::AlignLeft | Qt::AlignAbsolute));
    button(tr("Center"), tr("Center"), align(Qt::AlignHCenter));
    button(tr("Right"), tr("Align right"), align(Qt::AlignRight | Qt::AlignAbsolute));
    button(tr("B"), tr("Bold"), [](QTextEdit *e) {
        QTextCharFormat f;
        f.setFontWeight(e->currentCharFormat().fontWeight() >= QFont::Bold ? QFont::Normal : QFont::Bold);
        e->mergeCurrentCharFormat(f);
    });
    button(tr("I"), tr("Italic"), [](QTextEdit *e) {
        QTextCharFormat f;
        f.setFontItalic(!e->currentCharFormat().fontItalic());
        e->mergeCurrentCharFormat(f);
    });
    button(tr("Page #"), tr("Insert the page number"), [](QTextEdit *e) { e->insertPlainText(QStringLiteral("{PAGE}")); });
    button(tr("Pages"), tr("Insert the page count"), [](QTextEdit *e) { e->insertPlainText(QStringLiteral("{NUMPAGES}")); });

    if (!slot.editable) {
        edit->setReadOnly(true);
        edit->setEnabled(false);
        auto *note = new QLabel(tr("Contains images, tables or other fields; kept as it is."), box);
        note->setWordWrap(true);
        v->addWidget(note);
    }
    m_slots.push_back(slot);
    return box;
}

bool HeaderFooterDialog::apply()
{
    bool changed = false;
    for (const Slot &s : m_slots) {
        if (!s.editable || !s.edit->document()->isModified())
            continue;
        if (!s.existed && s.edit->document()->toPlainText().trimmed().isEmpty())
            continue;
        std::unique_ptr<QTextDocument> copy(s.edit->document()->clone());
        tokensToFields(copy.get());
        m_pkg.setHdrFtrXml(s.footer, s.type, DocxWriter::buildPart(copy.get(), m_pkg, s.footer));
        changed = true;
    }
    if (m_firstPage->isChecked() != m_initialTitlePg) {
        m_pkg.setTitlePg(m_firstPage->isChecked());
        changed = true;
    }
    return changed;
}
