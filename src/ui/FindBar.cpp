#include "ui/FindBar.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>

FindBar::FindBar(QTextEdit *edit, QWidget *parent) : QWidget(parent), m_edit(edit)
{
    m_find = new QLineEdit(this);
    m_find->setPlaceholderText(tr("Find"));
    m_find->setClearButtonEnabled(true);
    m_replace = new QLineEdit(this);
    m_replace->setPlaceholderText(tr("Replace with"));
    m_case = new QCheckBox(tr("Match case"), this);
    m_words = new QCheckBox(tr("Whole words"), this);
    m_count = new QLabel(this);
    m_count->setMinimumWidth(90);

    auto *prev = new QPushButton(tr("Previous"), this);
    auto *next = new QPushButton(tr("Next"), this);
    auto *replace = new QPushButton(tr("Replace"), this);
    auto *replaceAll = new QPushButton(tr("Replace All"), this);
    auto *close = new QToolButton(this);
    close->setText(QStringLiteral("✕"));
    close->setAutoRaise(true);
    close->setToolTip(tr("Close (Esc)"));
    for (QPushButton *b : {prev, next, replace, replaceAll})
        b->setAutoDefault(false);

    auto *grid = new QGridLayout(this);
    grid->setContentsMargins(8, 4, 8, 4);
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(4);
    grid->addWidget(m_find, 0, 0);
    grid->addWidget(prev, 0, 1);
    grid->addWidget(next, 0, 2);
    grid->addWidget(m_case, 0, 3);
    grid->addWidget(m_words, 0, 4);
    grid->addWidget(m_count, 0, 5);
    grid->addWidget(close, 0, 6, Qt::AlignRight);
    m_replaceRow = new QWidget(this);
    auto *row = new QHBoxLayout(m_replaceRow);
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(replace);
    row->addWidget(replaceAll);
    row->addStretch();
    grid->addWidget(m_replace, 1, 0);
    grid->addWidget(m_replaceRow, 1, 1, 1, 6);
    grid->setColumnStretch(0, 1);
    m_replace->setVisible(false);
    m_replaceRow->setVisible(false);
    m_find->installEventFilter(this);

    m_refresh = new QTimer(this);
    m_refresh->setSingleShot(true);
    m_refresh->setInterval(150);
    connect(m_refresh, &QTimer::timeout, this, &FindBar::refreshHighlights);

    connect(prev, &QPushButton::clicked, this, &FindBar::findPrevious);
    connect(next, &QPushButton::clicked, this, &FindBar::findNext);
    connect(replace, &QPushButton::clicked, this, &FindBar::replaceOne);
    connect(replaceAll, &QPushButton::clicked, this, &FindBar::replaceAllClicked);
    connect(close, &QToolButton::clicked, this, &FindBar::closeBar);
    connect(m_find, &QLineEdit::returnPressed, this, [this] { findNext(); });
    connect(m_replace, &QLineEdit::returnPressed, this, &FindBar::replaceOne);
    connect(m_find, &QLineEdit::textChanged, this, [this] {
        // Incremental: jump to the first match from where the search started.
        QTextCursor c = m_edit->textCursor();
        c.setPosition(c.selectionStart());
        m_edit->setTextCursor(c);
        if (!m_find->text().isEmpty())
            find(false);
        scheduleRefresh();
    });
    connect(m_case, &QCheckBox::toggled, this, &FindBar::scheduleRefresh);
    connect(m_words, &QCheckBox::toggled, this, &FindBar::scheduleRefresh);
    connect(m_edit->document(), &QTextDocument::contentsChanged, this, [this] {
        if (isVisible())
            scheduleRefresh();
    });
    hide();
}

void FindBar::showFind()
{
    const QString sel = m_edit->textCursor().selectedText();
    if (!sel.isEmpty() && !sel.contains(QChar::ParagraphSeparator))
        m_find->setText(sel);
    show();
    m_find->setFocus();
    m_find->selectAll();
    scheduleRefresh();
}

void FindBar::showReplace()
{
    m_replace->setVisible(true);
    m_replaceRow->setVisible(true);
    showFind();
    if (!m_find->text().isEmpty())
        m_replace->setFocus();
}

QTextDocument::FindFlags FindBar::flags() const
{
    QTextDocument::FindFlags f;
    if (m_case->isChecked())
        f |= QTextDocument::FindCaseSensitively;
    if (m_words->isChecked())
        f |= QTextDocument::FindWholeWords;
    return f;
}

bool FindBar::find(bool backward)
{
    const QString q = m_find->text();
    if (q.isEmpty())
        return false;
    QTextDocument::FindFlags f = flags();
    if (backward)
        f |= QTextDocument::FindBackward;
    QTextDocument *doc = m_edit->document();
    QTextCursor hit = doc->find(q, m_edit->textCursor(), f);
    if (hit.isNull()) {
        // Wrap around once.
        QTextCursor from(doc);
        from.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
        hit = doc->find(q, from, f);
    }
    if (hit.isNull()) {
        m_count->setText(tr("No matches"));
        return false;
    }
    m_edit->setTextCursor(hit);
    m_edit->ensureCursorVisible();
    return true;
}

bool FindBar::findNext()
{
    if (!isVisible() || m_find->text().isEmpty()) {
        showFind();
        return false;
    }
    const bool ok = find(false);
    scheduleRefresh();
    return ok;
}

bool FindBar::findPrevious()
{
    if (!isVisible() || m_find->text().isEmpty()) {
        showFind();
        return false;
    }
    const bool ok = find(true);
    scheduleRefresh();
    return ok;
}

bool FindBar::selectionMatches() const
{
    const QString sel = m_edit->textCursor().selectedText();
    if (sel.isEmpty())
        return false;
    return m_case->isChecked() ? sel == m_find->text() : sel.compare(m_find->text(), Qt::CaseInsensitive) == 0;
}

void FindBar::replaceSelection(QTextCursor &cursor, const QString &replacement)
{
    QTextCursor first(cursor.document());
    first.setPosition(cursor.selectionStart() + 1);
    const QTextCharFormat fmt = first.charFormat();  // format of the match's first character
    cursor.insertText(replacement, fmt);
}

void FindBar::replaceOne()
{
    if (m_find->text().isEmpty())
        return;
    if (selectionMatches()) {
        QTextCursor c = m_edit->textCursor();
        replaceSelection(c, m_replace->text());
        m_edit->setTextCursor(c);
    }
    find(false);
    scheduleRefresh();
}

QList<QTextCursor> FindBar::matches(QTextDocument *doc, const QString &query, QTextDocument::FindFlags flags, int limit)
{
    QList<QTextCursor> out;
    if (query.isEmpty())
        return out;
    QTextCursor c(doc);
    while (out.size() < limit) {
        c = doc->find(query, c, flags & ~QTextDocument::FindBackward);
        if (c.isNull())
            break;
        out.append(c);
    }
    return out;
}

int FindBar::replaceAll(QTextDocument *doc, const QString &query, const QString &replacement,
                        QTextDocument::FindFlags flags)
{
    if (query.isEmpty())
        return 0;
    int count = 0;
    QTextCursor edit(doc);
    edit.beginEditBlock();
    QTextCursor c(doc);
    for (;;) {
        c = doc->find(query, c, flags & ~QTextDocument::FindBackward);
        if (c.isNull())
            break;
        replaceSelection(c, replacement);  // c now sits after the inserted text: no re-matching it
        ++count;
    }
    edit.endEditBlock();
    return count;
}

void FindBar::replaceAllClicked()
{
    const int n = replaceAll(m_edit->document(), m_find->text(), m_replace->text(), flags());
    m_count->setText(n == 1 ? tr("Replaced 1") : tr("Replaced %1").arg(n));
    refreshHighlights();
}

void FindBar::scheduleRefresh()
{
    m_refresh->start();
}

void FindBar::refreshHighlights()
{
    if (!isVisible() || m_find->text().isEmpty()) {
        m_edit->setExtraSelections({});
        if (!m_find->text().isEmpty() || isVisible())
            m_count->clear();
        return;
    }
    const QList<QTextCursor> hits = matches(m_edit->document(), m_find->text(), flags());
    QList<QTextEdit::ExtraSelection> sels;
    QTextCharFormat hl;
    hl.setBackground(QColor(0xFF, 0xE8, 0x8A));
    int current = 0;
    const QTextCursor cur = m_edit->textCursor();
    for (int i = 0; i < hits.size(); ++i) {
        QTextEdit::ExtraSelection s;
        s.cursor = hits[i];
        s.format = hl;
        sels.append(s);
        if (hits[i].selectionStart() == cur.selectionStart() && hits[i].selectionEnd() == cur.selectionEnd())
            current = i + 1;
    }
    m_edit->setExtraSelections(sels);  // display only: not part of the document
    if (hits.isEmpty())
        m_count->setText(tr("No matches"));
    else if (current)
        m_count->setText(tr("%1 of %2").arg(current).arg(hits.size()));
    else
        m_count->setText(hits.size() == 1 ? tr("1 match") : tr("%1 matches").arg(hits.size()));
}

void FindBar::closeBar()
{
    hide();
    m_edit->setExtraSelections({});
    m_edit->setFocus();
}

void FindBar::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape) {
        closeBar();
        return;
    }
    QWidget::keyPressEvent(e);
}

bool FindBar::eventFilter(QObject *obj, QEvent *e)
{
    // QLineEdit consumes Return itself, so Shift+Return (previous match) is caught here.
    if (obj == m_find && e->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(e);
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) && (ke->modifiers() & Qt::ShiftModifier)) {
            findPrevious();
            return true;
        }
    }
    return QWidget::eventFilter(obj, e);
}
