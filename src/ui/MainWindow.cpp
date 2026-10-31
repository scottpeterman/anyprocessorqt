#include "ui/MainWindow.h"
#include "docx/Convert.h"
#include "docx/DocxReader.h"
#include "docx/DocxWriter.h"
#include "docx/Units.h"
#include "ui/InsertTableDialog.h"
#include "ui/PageEdit.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QImageReader>
#include <QInputDialog>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPageLayout>
#include <QPageSize>
#include <QPrintDialog>
#include <QPrinter>
#include <QSettings>
#include <QStatusBar>
#include <QStyle>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextList>
#include <QTextTable>
#include <QTimer>
#include <QToolBar>
#include <QUrl>
#include <QUuid>

#include <memory>

using namespace docx;

static QIcon themed(const char *name, QStyle::StandardPixmap fallback = QStyle::SP_CustomBase)
{
    QIcon icon = QIcon::fromTheme(QString::fromLatin1(name));
    if (icon.isNull() && fallback != QStyle::SP_CustomBase)
        icon = QApplication::style()->standardIcon(fallback);
    return icon;
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    m_edit = new PageEdit(this);
    setCentralWidget(m_edit);

    buildActions();
    buildMenus();
    buildToolbars();

    m_wordCount = new QLabel(this);
    statusBar()->addPermanentWidget(m_wordCount);
    m_countTimer = new QTimer(this);
    m_countTimer->setSingleShot(true);
    m_countTimer->setInterval(400);
    connect(m_countTimer, &QTimer::timeout, this, &MainWindow::updateWordCount);

    connect(m_edit, &QTextEdit::currentCharFormatChanged, this, [this] { updateFormatUi(); });
    connect(m_edit, &QTextEdit::cursorPositionChanged, this, [this] { updateFormatUi(); });
    connect(m_edit->document(), &QTextDocument::contentsChanged, m_countTimer, qOverload<>(&QTimer::start));
    connect(m_edit->document(), &QTextDocument::modificationChanged, this, &QWidget::setWindowModified);
    connect(m_edit->document(), &QTextDocument::undoAvailable, m_undo, &QAction::setEnabled);
    connect(m_edit->document(), &QTextDocument::redoAvailable, m_redo, &QAction::setEnabled);

    resize(1100, 900);
    newDocument();
}

// ------------------------------------------------------------------ actions

void MainWindow::buildActions()
{
    auto mk = [this](const QString &text, const QKeySequence &key, auto slot, const char *icon = nullptr,
                     QStyle::StandardPixmap fb = QStyle::SP_CustomBase) {
        auto *a = new QAction(text, this);
        if (!key.isEmpty())
            a->setShortcut(key);
        if (icon)
            a->setIcon(themed(icon, fb));
        connect(a, &QAction::triggered, this, slot);
        return a;
    };

    m_new = mk(tr("&New"), QKeySequence::New, [this] { if (maybeSave()) newDocument(); }, "document-new", QStyle::SP_FileIcon);
    m_open = mk(tr("&Open…"), QKeySequence::Open, [this] { open(); }, "document-open", QStyle::SP_DialogOpenButton);
    m_save = mk(tr("&Save"), QKeySequence::Save, [this] { save(); }, "document-save", QStyle::SP_DialogSaveButton);
    m_saveAs = mk(tr("Save &As…"), QKeySequence::SaveAs, [this] { saveAs(); }, "document-save-as");
    m_exportPdf = mk(tr("Export as &PDF…"), QKeySequence(), [this] { exportPdf(); }, "application-pdf");
    m_print = mk(tr("&Print…"), QKeySequence::Print, [this] { print(); }, "document-print");
    m_quit = mk(tr("&Quit"), QKeySequence::Quit, [this] { close(); }, "application-exit");

    m_undo = mk(tr("&Undo"), QKeySequence::Undo, [this] { m_edit->undo(); }, "edit-undo");
    m_redo = mk(tr("&Redo"), QKeySequence::Redo, [this] { m_edit->redo(); }, "edit-redo");
    m_cut = mk(tr("Cu&t"), QKeySequence::Cut, [this] { m_edit->cut(); }, "edit-cut");
    m_copy = mk(tr("&Copy"), QKeySequence::Copy, [this] { m_edit->copy(); }, "edit-copy");
    m_paste = mk(tr("&Paste"), QKeySequence::Paste, [this] { m_edit->paste(); }, "edit-paste");
    m_selectAll = mk(tr("Select &All"), QKeySequence::SelectAll, [this] { m_edit->selectAll(); });
    m_find = mk(tr("&Find…"), QKeySequence::Find, [this] { find(); }, "edit-find");
    m_findNext = mk(tr("Find &Next"), QKeySequence::FindNext, [this] { findNext(); });

    m_bold = mk(tr("&Bold"), QKeySequence::Bold, [this] {
        QTextCharFormat f;
        f.setFontWeight(m_bold->isChecked() ? QFont::Bold : QFont::Normal);
        mergeFormat(f);
    }, "format-text-bold");
    m_italic = mk(tr("&Italic"), QKeySequence::Italic, [this] {
        QTextCharFormat f;
        f.setFontItalic(m_italic->isChecked());
        mergeFormat(f);
    }, "format-text-italic");
    m_underline = mk(tr("&Underline"), QKeySequence::Underline, [this] {
        QTextCharFormat f;
        f.setFontUnderline(m_underline->isChecked());
        mergeFormat(f);
    }, "format-text-underline");
    m_strike = mk(tr("S&trikethrough"), QKeySequence(), [this] {
        QTextCharFormat f;
        f.setFontStrikeOut(m_strike->isChecked());
        mergeFormat(f);
    }, "format-text-strikethrough");
    m_super = mk(tr("Su&perscript"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Plus), [this] {
        QTextCharFormat f;
        f.setVerticalAlignment(m_super->isChecked() ? QTextCharFormat::AlignSuperScript : QTextCharFormat::AlignNormal);
        mergeFormat(f);
    }, "format-text-superscript");
    m_sub = mk(tr("Su&bscript"), QKeySequence(Qt::CTRL | Qt::Key_Equal), [this] {
        QTextCharFormat f;
        f.setVerticalAlignment(m_sub->isChecked() ? QTextCharFormat::AlignSubScript : QTextCharFormat::AlignNormal);
        mergeFormat(f);
    }, "format-text-subscript");
    for (QAction *a : {m_bold, m_italic, m_underline, m_strike, m_super, m_sub})
        a->setCheckable(true);
    m_color = mk(tr("Text &Color…"), QKeySequence(), [this] { chooseTextColor(); }, "format-text-color");
    m_highlight = mk(tr("&Highlight"), QKeySequence(), [this] { toggleHighlight(); }, "format-text-highlight");
    m_clearFmt = mk(tr("C&lear Formatting"), QKeySequence(Qt::CTRL | Qt::Key_Space), [this] { clearFormatting(); },
                    "edit-clear");

    m_alignLeft = mk(tr("Align &Left"), QKeySequence(Qt::CTRL | Qt::Key_L), [this] { setAlignment(Qt::AlignLeft); },
                     "format-justify-left");
    m_alignCenter = mk(tr("C&enter"), QKeySequence(Qt::CTRL | Qt::Key_E), [this] { setAlignment(Qt::AlignHCenter); },
                       "format-justify-center");
    m_alignRight = mk(tr("Align &Right"), QKeySequence(Qt::CTRL | Qt::Key_R), [this] { setAlignment(Qt::AlignRight); },
                      "format-justify-right");
    m_alignJustify = mk(tr("&Justify"), QKeySequence(Qt::CTRL | Qt::Key_J), [this] { setAlignment(Qt::AlignJustify); },
                        "format-justify-fill");
    auto *alignGroup = new QActionGroup(this);
    for (QAction *a : {m_alignLeft, m_alignCenter, m_alignRight, m_alignJustify}) {
        a->setCheckable(true);
        alignGroup->addAction(a);
    }

    m_bullets = mk(tr("&Bulleted List"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_8),
                   [this] { toggleList(QTextListFormat::ListDisc); }, "format-list-unordered");
    m_numbers = mk(tr("&Numbered List"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_7),
                   [this] { toggleList(QTextListFormat::ListDecimal); }, "format-list-ordered");
    m_indentMore = mk(tr("&Increase Indent"), QKeySequence(Qt::CTRL | Qt::Key_BracketRight),
                      [this] { changeIndent(1); }, "format-indent-more");
    m_indentLess = mk(tr("&Decrease Indent"), QKeySequence(Qt::CTRL | Qt::Key_BracketLeft),
                      [this] { changeIndent(-1); }, "format-indent-less");

    m_insImage = mk(tr("&Image…"), QKeySequence(), [this] { insertImage(); }, "insert-image");
    m_resizeImage = mk(tr("Image &Size…"), QKeySequence(), [this] { resizeImage(); });
    m_insTable = mk(tr("&Table…"), QKeySequence(), [this] { insertTable(); }, "insert-table");
    m_insBreak = mk(tr("&Page Break"), QKeySequence(Qt::CTRL | Qt::Key_Return), [this] { insertPageBreak(); });
    m_insLink = mk(tr("&Hyperlink…"), QKeySequence(Qt::CTRL | Qt::Key_K), [this] { insertHyperlink(); }, "insert-link");

    auto table = [this] { return m_edit->textCursor().currentTable(); };
    m_rowAbove = mk(tr("Insert Row &Above"), QKeySequence(), [this, table] {
        if (QTextTable *t = table()) t->insertRows(t->cellAt(m_edit->textCursor()).row(), 1);
    });
    m_rowBelow = mk(tr("Insert Row &Below"), QKeySequence(), [this, table] {
        if (QTextTable *t = table()) { const auto c = t->cellAt(m_edit->textCursor()); t->insertRows(c.row() + c.rowSpan(), 1); }
    });
    m_colLeft = mk(tr("Insert Column &Left"), QKeySequence(), [this, table] {
        if (QTextTable *t = table()) t->insertColumns(t->cellAt(m_edit->textCursor()).column(), 1);
    });
    m_colRight = mk(tr("Insert Column &Right"), QKeySequence(), [this, table] {
        if (QTextTable *t = table()) { const auto c = t->cellAt(m_edit->textCursor()); t->insertColumns(c.column() + c.columnSpan(), 1); }
    });
    m_delRow = mk(tr("Delete R&ow"), QKeySequence(), [this, table] {
        if (QTextTable *t = table()) t->removeRows(t->cellAt(m_edit->textCursor()).row(), 1);
    });
    m_delCol = mk(tr("Delete Colu&mn"), QKeySequence(), [this, table] {
        if (QTextTable *t = table()) t->removeColumns(t->cellAt(m_edit->textCursor()).column(), 1);
    });
    m_delTable = mk(tr("Delete &Table"), QKeySequence(), [this, table] {
        if (QTextTable *t = table()) t->removeRows(0, t->rows());
    });
    m_merge = mk(tr("&Merge Cells"), QKeySequence(), [this, table] {
        if (QTextTable *t = table()) t->mergeCells(m_edit->textCursor());
    });
    m_split = mk(tr("&Split Cell"), QKeySequence(), [this, table] {
        if (QTextTable *t = table()) { const auto c = t->cellAt(m_edit->textCursor()); t->splitCell(c.row(), c.column(), 1, 1); }
    });

    m_about = mk(tr("&About anyprocessorqt"), QKeySequence(), [this] {
        QMessageBox::about(this, tr("About anyprocessorqt"),
                           tr("<b>anyprocessorqt %1</b><br>A lightweight DOCX word processor in Qt.<br><br>"
                              "Qt %2<br>github.com/scottpeterman/anyprocessorqt")
                               .arg(QStringLiteral(ANYPROCESSORQT_VERSION), QString::fromLatin1(qVersion())));
    });
}

void MainWindow::buildMenus()
{
    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addActions({m_new, m_open, m_save, m_saveAs});
    file->addSeparator();
    file->addActions({m_exportPdf, m_print});
    file->addSeparator();
    file->addAction(m_quit);

    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    edit->addActions({m_undo, m_redo});
    edit->addSeparator();
    edit->addActions({m_cut, m_copy, m_paste, m_selectAll});
    edit->addSeparator();
    edit->addActions({m_find, m_findNext});

    QMenu *format = menuBar()->addMenu(tr("F&ormat"));
    format->addActions({m_bold, m_italic, m_underline, m_strike, m_super, m_sub});
    format->addSeparator();
    format->addActions({m_color, m_highlight, m_clearFmt});
    format->addSeparator();
    format->addActions({m_alignLeft, m_alignCenter, m_alignRight, m_alignJustify});
    format->addSeparator();
    format->addActions({m_bullets, m_numbers, m_indentMore, m_indentLess});
    QMenu *spacing = format->addMenu(tr("Line &Spacing"));
    for (double m : {1.0, 1.15, 1.5, 2.0}) {
        QAction *a = spacing->addAction(QString::number(m, 'g', 3));
        connect(a, &QAction::triggered, this, [this, m] { setLineSpacing(m); });
    }
    format->addSeparator();
    format->addAction(m_resizeImage);

    QMenu *insert = menuBar()->addMenu(tr("&Insert"));
    insert->addActions({m_insImage, m_insTable, m_insLink, m_insBreak});

    QMenu *table = menuBar()->addMenu(tr("&Table"));
    table->addAction(m_insTable);
    table->addSeparator();
    table->addActions({m_rowAbove, m_rowBelow, m_colLeft, m_colRight});
    table->addSeparator();
    table->addActions({m_delRow, m_delCol, m_delTable});
    table->addSeparator();
    table->addActions({m_merge, m_split});

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(m_about);
}

void MainWindow::buildToolbars()
{
    QToolBar *fileBar = addToolBar(tr("File"));
    fileBar->setObjectName(QStringLiteral("fileBar"));
    fileBar->addActions({m_new, m_open, m_save});
    fileBar->addSeparator();
    fileBar->addActions({m_undo, m_redo});

    QToolBar *fmt = addToolBar(tr("Format"));
    fmt->setObjectName(QStringLiteral("formatBar"));
    m_styleCombo = new QComboBox(fmt);
    m_styleCombo->setMinimumContentsLength(14);
    m_styleCombo->setToolTip(tr("Paragraph style"));
    fmt->addWidget(m_styleCombo);
    connect(m_styleCombo, qOverload<int>(&QComboBox::activated), this, [this](int idx) {
        applyParagraphStyle(m_styleCombo->itemData(idx).toString());
        m_edit->setFocus();
    });

    m_fontCombo = new QFontComboBox(fmt);
    m_fontCombo->setToolTip(tr("Font"));
    fmt->addWidget(m_fontCombo);
    connect(m_fontCombo, &QFontComboBox::textActivated, this, [this](const QString &family) {
        QTextCharFormat f;
        f.setFontFamilies(QStringList{family});
        mergeFormat(f);
        m_edit->setFocus();
    });

    m_sizeCombo = new QComboBox(fmt);
    m_sizeCombo->setEditable(true);
    m_sizeCombo->setToolTip(tr("Font size"));
    for (int s : {8, 9, 10, 11, 12, 14, 16, 18, 20, 22, 24, 26, 28, 36, 48, 72})
        m_sizeCombo->addItem(QString::number(s));
    m_sizeCombo->setMinimumContentsLength(3);
    fmt->addWidget(m_sizeCombo);
    connect(m_sizeCombo, &QComboBox::textActivated, this, [this](const QString &t) {
        bool ok = false;
        const double pt = t.toDouble(&ok);
        if (ok && pt > 0 && pt < 1640) {
            QTextCharFormat f;
            f.setFontPointSize(pt);
            mergeFormat(f);
        }
        m_edit->setFocus();
    });

    fmt->addSeparator();
    fmt->addActions({m_bold, m_italic, m_underline, m_color, m_highlight});

    addToolBarBreak();
    QToolBar *para = addToolBar(tr("Paragraph"));
    para->setObjectName(QStringLiteral("paragraphBar"));
    para->addActions({m_alignLeft, m_alignCenter, m_alignRight, m_alignJustify});
    para->addSeparator();
    para->addActions({m_bullets, m_numbers, m_indentLess, m_indentMore});
    para->addSeparator();
    para->addActions({m_insImage, m_insTable});

    // Text fallbacks where the platform has no icon theme (Windows, macOS).
    for (QToolBar *bar : {fileBar, fmt, para})
        bar->setToolButtonStyle(Qt::ToolButtonFollowStyle);
    const QList<QPair<QAction *, QString>> fallbackText = {
        {m_bold, QStringLiteral("B")}, {m_italic, QStringLiteral("I")}, {m_underline, QStringLiteral("U")},
        {m_color, QStringLiteral("A")}, {m_highlight, QStringLiteral("H")}, {m_alignLeft, QStringLiteral("Left")},
        {m_alignCenter, QStringLiteral("Center")}, {m_alignRight, QStringLiteral("Right")},
        {m_alignJustify, QStringLiteral("Justify")}, {m_bullets, QStringLiteral("•")},
        {m_numbers, QStringLiteral("1.")}, {m_indentLess, QStringLiteral("⇤")}, {m_indentMore, QStringLiteral("⇥")},
        {m_insImage, QStringLiteral("Image")}, {m_insTable, QStringLiteral("Table")},
        {m_undo, QStringLiteral("Undo")}, {m_redo, QStringLiteral("Redo")},
    };
    for (const auto &[a, text] : fallbackText)
        if (a->icon().isNull())
            a->setIconText(text);
}

// --------------------------------------------------------------- file ops

void MainWindow::newDocument()
{
    m_pkg = Package::blank();
    QString err;
    DocxReader::build(m_pkg, m_edit->document(), &err);
    m_edit->setPageSetup(m_pkg.page);
    m_edit->document()->clearUndoRedoStacks();
    rebuildStyleCombo();
    setCurrentPath(QString());
    updateFormatUi();
    updateWordCount();
}

bool MainWindow::openFile(const QString &path)
{
    Package pkg;
    QString err;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = DocxReader::loadFile(path, m_edit->document(), &pkg, &err);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::warning(this, tr("Open failed"), tr("Could not open %1:\n%2").arg(path, err));
        return false;
    }
    m_pkg = pkg;
    m_edit->setPageSetup(m_pkg.page);
    m_edit->document()->clearUndoRedoStacks();
    m_edit->moveCursor(QTextCursor::Start);
    rebuildStyleCombo();
    setCurrentPath(path);
    QSettings().setValue(QStringLiteral("lastDir"), QFileInfo(path).absolutePath());
    updateFormatUi();
    updateWordCount();
    return true;
}

void MainWindow::open()
{
    if (!maybeSave())
        return;
    const QString dir = QSettings().value(QStringLiteral("lastDir")).toString();
    const QString path = QFileDialog::getOpenFileName(this, tr("Open Document"), dir, tr("Word documents (*.docx)"));
    if (!path.isEmpty())
        openFile(path);
}

bool MainWindow::save()
{
    if (m_path.isEmpty())
        return saveAs();
    return saveTo(m_path);
}

bool MainWindow::saveAs()
{
    QString dir = m_path.isEmpty() ? QSettings().value(QStringLiteral("lastDir")).toString() : m_path;
    QString path = QFileDialog::getSaveFileName(this, tr("Save As"), dir, tr("Word documents (*.docx)"));
    if (path.isEmpty())
        return false;
    if (!path.endsWith(QLatin1String(".docx"), Qt::CaseInsensitive))
        path += QStringLiteral(".docx");
    return saveTo(path);
}

bool MainWindow::saveTo(const QString &path)
{
    QString err;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = DocxWriter::saveFile(path, m_edit->document(), m_pkg, &err);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::warning(this, tr("Save failed"), tr("Could not save %1:\n%2").arg(path, err));
        return false;
    }
    m_edit->document()->setModified(false);
    setCurrentPath(path);
    QSettings().setValue(QStringLiteral("lastDir"), QFileInfo(path).absolutePath());
    statusBar()->showMessage(tr("Saved %1").arg(QFileInfo(path).fileName()), 3000);
    return true;
}

static void printDocument(QTextDocument *doc, const PageSetup &page, QPrinter *printer)
{
    std::unique_ptr<QTextDocument> copy(doc->clone());
    copy->setPageSize(QSizeF(twipsToPx(page.width), twipsToPx(page.height)));
    copy->print(printer);
}

void MainWindow::exportPdf()
{
    QString base = m_path.isEmpty() ? tr("Untitled") : QFileInfo(m_path).completeBaseName();
    const QString dir = m_path.isEmpty() ? QSettings().value(QStringLiteral("lastDir")).toString()
                                         : QFileInfo(m_path).absolutePath();
    QString path = QFileDialog::getSaveFileName(this, tr("Export PDF"), dir + QLatin1Char('/') + base + QStringLiteral(".pdf"),
                                                tr("PDF (*.pdf)"));
    if (path.isEmpty())
        return;
    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(path);
    printer.setPageLayout(QPageLayout(QPageSize(QSizeF(m_pkg.page.width / 1440.0, m_pkg.page.height / 1440.0),
                                                QPageSize::Inch),
                                      QPageLayout::Portrait, QMarginsF(0, 0, 0, 0)));
    printDocument(m_edit->document(), m_pkg.page, &printer);
    statusBar()->showMessage(tr("Exported %1").arg(QFileInfo(path).fileName()), 3000);
}

void MainWindow::print()
{
    QPrinter printer(QPrinter::HighResolution);
    printer.setPageLayout(QPageLayout(QPageSize(QSizeF(m_pkg.page.width / 1440.0, m_pkg.page.height / 1440.0),
                                                QPageSize::Inch),
                                      QPageLayout::Portrait, QMarginsF(0, 0, 0, 0)));
    QPrintDialog dlg(&printer, this);
    if (dlg.exec() == QDialog::Accepted)
        printDocument(m_edit->document(), m_pkg.page, &printer);
}

bool MainWindow::maybeSave()
{
    if (!m_edit->document()->isModified())
        return true;
    const auto r = QMessageBox::warning(this, tr("Unsaved changes"), tr("Save changes to this document?"),
                                        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (r == QMessageBox::Save)
        return save();
    return r == QMessageBox::Discard;
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (maybeSave())
        e->accept();
    else
        e->ignore();
}

void MainWindow::setCurrentPath(const QString &path)
{
    m_path = path;
    const QString name = path.isEmpty() ? tr("Untitled") : QFileInfo(path).fileName();
    setWindowTitle(QStringLiteral("%1[*] — anyprocessorqt").arg(name));
    setWindowModified(m_edit->document()->isModified());
}

void MainWindow::rebuildStyleCombo()
{
    m_styleCombo->clear();
    for (const auto &[id, label] : m_pkg.styles.uiParagraphStyles())
        m_styleCombo->addItem(label, id);
}

// -------------------------------------------------------------- formatting

void MainWindow::mergeFormat(const QTextCharFormat &fmt)
{
    QTextCursor c = m_edit->textCursor();
    if (!c.hasSelection())
        c.select(QTextCursor::WordUnderCursor);
    c.mergeCharFormat(fmt);
    m_edit->mergeCurrentCharFormat(fmt);
}

// Replace a fragment's text formatting with run props, keeping links/images/char style.
static QTextCharFormat restyled(const QTextCharFormat &old, const RunProps &rp)
{
    QTextCharFormat f = toCharFormat(rp);
    if (old.isAnchor()) {
        f.setAnchor(true);
        f.setAnchorHref(old.anchorHref());
    }
    if (old.hasProperty(RStyle))
        f.setProperty(RStyle, old.property(RStyle));
    return f;
}

void MainWindow::applyParagraphStyle(const QString &styleId)
{
    if (styleId.isEmpty())
        return;
    const ParaProps pp = m_pkg.styles.resolvedPara(styleId);
    const RunProps rp = m_pkg.styles.resolvedParaRun(styleId);
    QTextCursor c = m_edit->textCursor();
    QTextDocument *doc = m_edit->document();
    const QTextBlock first = doc->findBlock(c.selectionStart());
    const QTextBlock last = doc->findBlock(c.selectionEnd());
    c.beginEditBlock();
    for (QTextBlock b = first; b.isValid(); b = b.next()) {
        const QTextBlockFormat old = b.blockFormat();
        const bool inList = b.textList() != nullptr;
        QTextBlockFormat nb = toBlockFormat(pp, inList);
        nb.setProperty(PStyle, styleId);
        if (inList) {
            nb.setObjectIndex(old.objectIndex());
            nb.setLeftMargin(old.leftMargin());
            nb.setIndent(old.indent());
        }
        QTextCursor bc(b);
        compensateLineHeight(nb, toCharFormat(rp));
        bc.setBlockFormat(nb);
        // Merge, not set: inside a table the first block's char format carries the cell format.
        bc.mergeBlockCharFormat(toCharFormat(rp));
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            if (!frag.isValid() || frag.charFormat().isImageFormat())
                continue;
            QTextCursor fc(doc);
            fc.setPosition(frag.position());
            fc.setPosition(frag.position() + frag.length(), QTextCursor::KeepAnchor);
            fc.setCharFormat(restyled(frag.charFormat(), rp));
        }
        if (b == last)
            break;
    }
    c.endEditBlock();
    m_edit->setCurrentCharFormat(toCharFormat(rp));
    updateFormatUi();
}

void MainWindow::clearFormatting()
{
    QTextCursor c = m_edit->textCursor();
    if (!c.hasSelection())
        return;
    QTextDocument *doc = m_edit->document();
    c.beginEditBlock();
    const int start = c.selectionStart(), end = c.selectionEnd();
    for (QTextBlock b = doc->findBlock(start); b.isValid() && b.position() <= end; b = b.next()) {
        QString styleId = b.blockFormat().stringProperty(PStyle);
        if (styleId.isEmpty())
            styleId = m_pkg.styles.defaultParagraphStyle();
        const RunProps rp = m_pkg.styles.resolvedParaRun(styleId);
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            if (!frag.isValid() || frag.charFormat().isImageFormat())
                continue;
            const int fs = qMax(start, frag.position());
            const int fe = qMin(end, frag.position() + frag.length());
            if (fs >= fe)
                continue;
            QTextCursor fc(doc);
            fc.setPosition(fs);
            fc.setPosition(fe, QTextCursor::KeepAnchor);
            QTextCharFormat f = restyled(frag.charFormat(), rp);
            f.clearProperty(RStyle);
            fc.setCharFormat(f);
        }
    }
    c.endEditBlock();
}

void MainWindow::setAlignment(Qt::Alignment a)
{
    QTextBlockFormat f;
    f.setAlignment(a);
    m_edit->textCursor().mergeBlockFormat(f);
}

void MainWindow::toggleList(QTextListFormat::Style style)
{
    QTextCursor c = m_edit->textCursor();
    c.beginEditBlock();
    QTextList *list = c.currentList();
    const bool isBullet = [](QTextListFormat::Style s) {
        return s == QTextListFormat::ListDisc || s == QTextListFormat::ListCircle || s == QTextListFormat::ListSquare;
    }(style);
    if (list) {
        QTextListFormat lf = list->format();
        const bool listIsBullet = lf.style() == QTextListFormat::ListDisc || lf.style() == QTextListFormat::ListCircle
                                  || lf.style() == QTextListFormat::ListSquare;
        if (listIsBullet == isBullet) {
            // Same kind: take the selected paragraphs out of the list.
            QTextDocument *doc = m_edit->document();
            for (QTextBlock b = doc->findBlock(c.selectionStart()); b.isValid(); b = b.next()) {
                if (b.textList())
                    b.textList()->remove(b);
                QTextCursor bc(b);
                QTextBlockFormat bf = b.blockFormat();
                bf.setIndent(0);
                QString styleId = bf.stringProperty(PStyle);
                if (styleId.isEmpty())
                    styleId = m_pkg.styles.defaultParagraphStyle();
                const ParaProps pp = m_pkg.styles.resolvedPara(styleId);
                bf.setLeftMargin(twipsToPx(pp.left.value_or(0)));
                bf.setTextIndent(twipsToPx(pp.firstLine.value_or(0)));
                bc.setBlockFormat(bf);
                if (b.contains(c.selectionEnd()))
                    break;
            }
        } else {
            // Switch bullet <-> number; drop the docx numbering link so a new definition is written.
            lf.setStyle(style);
            lf.clearProperty(NumId);
            lf.clearProperty(Ilvl);
            lf.clearProperty(NumFromStyle);
            if (lf.indent() < 1)
                lf.setIndent(1);
            list->setFormat(lf);
            for (int i = 0; i < list->count(); ++i) {
                QTextCursor bc(list->item(i));
                QTextBlockFormat bf = list->item(i).blockFormat();
                bf.setLeftMargin(0);
                bc.setBlockFormat(bf);
            }
        }
    } else {
        QTextBlockFormat bf = c.blockFormat();
        bf.setLeftMargin(0);
        bf.setTextIndent(0);
        bf.setIndent(0);
        c.setBlockFormat(bf);
        QTextListFormat lf;
        lf.setStyle(style);
        lf.setIndent(1);
        if (!isBullet)
            lf.setNumberSuffix(QStringLiteral("."));
        c.createList(lf);
    }
    c.endEditBlock();
}

void MainWindow::changeIndent(int delta)
{
    QTextCursor c = m_edit->textCursor();
    QTextDocument *doc = m_edit->document();
    c.beginEditBlock();
    for (QTextBlock b = doc->findBlock(c.selectionStart()); b.isValid(); b = b.next()) {
        QTextBlockFormat bf = b.blockFormat();
        if (b.textList())
            bf.setIndent(qBound(0, bf.indent() + delta, 8));
        else
            bf.setLeftMargin(qMax(0.0, bf.leftMargin() + delta * doc->indentWidth()));
        QTextCursor(b).setBlockFormat(bf);
        if (b.contains(c.selectionEnd()))
            break;
    }
    c.endEditBlock();
}

void MainWindow::setLineSpacing(double multiple)
{
    QTextCursor c = m_edit->textCursor();
    QTextDocument *doc = m_edit->document();
    c.beginEditBlock();
    for (QTextBlock b = doc->findBlock(c.selectionStart()); b.isValid(); b = b.next()) {
        QTextBlockFormat bf = b.blockFormat();
        bf.clearProperty(LineOrig);
        bf.clearProperty(LineShown);
        if (qFuzzyCompare(multiple, 1.0))
            bf.setLineHeight(100, QTextBlockFormat::SingleHeight);
        else
            bf.setLineHeight(multiple * 100.0, QTextBlockFormat::ProportionalHeight);
        compensateLineHeight(bf, dominantCharFormat(b));
        QTextCursor(b).setBlockFormat(bf);
        if (b.contains(c.selectionEnd()))
            break;
    }
    c.endEditBlock();
}

void MainWindow::chooseTextColor()
{
    const QColor current = m_edit->currentCharFormat().foreground().color();
    const QColor c = QColorDialog::getColor(current, this, tr("Text Color"));
    if (!c.isValid())
        return;
    QTextCharFormat f;
    f.setForeground(c);
    mergeFormat(f);
}

void MainWindow::toggleHighlight()
{
    QTextCursor c = m_edit->textCursor();
    if (!c.hasSelection())
        c.select(QTextCursor::WordUnderCursor);
    const bool on = c.charFormat().stringProperty(Highlight).isEmpty();
    QTextCharFormat f;
    if (on) {
        f.setBackground(highlightColor(QStringLiteral("yellow")));
        f.setProperty(Highlight, QStringLiteral("yellow"));
        c.mergeCharFormat(f);
    } else {
        // mergeCharFormat can't clear properties; rewrite each fragment.
        QTextDocument *doc = m_edit->document();
        const int start = c.selectionStart(), end = c.selectionEnd();
        c.beginEditBlock();
        for (QTextBlock b = doc->findBlock(start); b.isValid() && b.position() <= end; b = b.next()) {
            for (auto it = b.begin(); !it.atEnd(); ++it) {
                const QTextFragment frag = it.fragment();
                const int fs = qMax(start, frag.position()), fe = qMin(end, frag.position() + frag.length());
                if (fs >= fe)
                    continue;
                QTextCharFormat nf = frag.charFormat();
                nf.clearBackground();
                nf.clearProperty(Highlight);
                QTextCursor fc(doc);
                fc.setPosition(fs);
                fc.setPosition(fe, QTextCursor::KeepAnchor);
                fc.setCharFormat(nf);
            }
        }
        c.endEditBlock();
    }
}

// ------------------------------------------------------------------ insert

void MainWindow::insertImage()
{
    const QString dir = QSettings().value(QStringLiteral("lastImageDir")).toString();
    const QString path = QFileDialog::getOpenFileName(this, tr("Insert Image"), dir,
                                                      tr("Images (*.png *.jpg *.jpeg *.gif *.bmp *.webp)"));
    if (path.isEmpty())
        return;
    QSettings().setValue(QStringLiteral("lastImageDir"), QFileInfo(path).absolutePath());
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage img = reader.read();
    if (img.isNull()) {
        QMessageBox::warning(this, tr("Insert Image"), tr("Could not read %1:\n%2").arg(path, reader.errorString()));
        return;
    }
    const double maxW = m_pkg.page.contentWidthPx();
    double w = img.width(), h = img.height();
    if (w > maxW) {
        h = h * maxW / w;
        w = maxW;
    }
    const QString name = QStringLiteral("image:") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_edit->document()->addResource(QTextDocument::ImageResource, QUrl(name), img);
    QTextImageFormat f;
    f.setName(name);
    f.setWidth(w);
    f.setHeight(h);
    m_edit->textCursor().insertImage(f);
}

void MainWindow::resizeImage()
{
    QTextCursor c = m_edit->textCursor();
    QTextCursor sel = c;
    if (!sel.hasSelection()) {
        sel.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor);
        if (!sel.charFormat().isImageFormat()) {
            sel = c;
            sel.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor);
        }
    }
    QTextImageFormat f = sel.charFormat().toImageFormat();
    if (!f.isValid() || !sel.charFormat().isImageFormat()) {
        QMessageBox::information(this, tr("Image Size"), tr("Place the cursor next to an image first."));
        return;
    }
    const QVariant res = m_edit->document()->resource(QTextDocument::ImageResource, QUrl(f.name()));
    const QImage img = res.value<QImage>();
    const double natW = img.isNull() ? f.width() : img.width();
    const double natH = img.isNull() ? f.height() : img.height();
    const double curW = f.width() > 0 ? f.width() : natW;
    bool ok = false;
    const int pct = QInputDialog::getInt(this, tr("Image Size"), tr("Width as % of page text width:"),
                                         int(qRound(curW * 100.0 / m_pkg.page.contentWidthPx())), 5, 100, 5, &ok);
    if (!ok)
        return;
    const double w = m_pkg.page.contentWidthPx() * pct / 100.0;
    f.setWidth(w);
    f.setHeight(natW > 0 ? natH * w / natW : f.height());
    sel.setCharFormat(f);
}

void MainWindow::insertTable()
{
    InsertTableDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const int cols = dlg.columns();
    QTextTableFormat tf;
    tf.setBorder(1);
    tf.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
    tf.setBorderBrush(QBrush(QColor(0x80, 0x80, 0x80)));
    tf.setBorderCollapse(true);
    tf.setCellSpacing(0);
    tf.setCellPadding(4);
    tf.setProperty(TableBorders, true);
    QList<QTextLength> widths;
    const double w = m_pkg.page.contentWidthPx() / cols;
    for (int i = 0; i < cols; ++i)
        widths.append(QTextLength(QTextLength::FixedLength, w));
    tf.setColumnWidthConstraints(widths);
    m_edit->textCursor().insertTable(dlg.rows(), cols, tf);
}

void MainWindow::insertPageBreak()
{
    QTextCursor c = m_edit->textCursor();
    c.beginEditBlock();
    QTextBlockFormat bf = c.blockFormat();
    bf.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
    c.insertBlock(bf);
    c.endEditBlock();
    m_edit->setTextCursor(c);
}

void MainWindow::insertHyperlink()
{
    QTextCursor c = m_edit->textCursor();
    const QString existing = c.charFormat().anchorHref();
    bool ok = false;
    const QString url = QInputDialog::getText(this, tr("Hyperlink"), tr("Address:"), QLineEdit::Normal,
                                              existing.isEmpty() ? QStringLiteral("https://") : existing, &ok);
    if (!ok || url.trimmed().isEmpty())
        return;
    QTextCharFormat f;
    f.setAnchor(true);
    f.setAnchorHref(url.trimmed());
    if (const Style *s = m_pkg.styles.style(QStringLiteral("Hyperlink"))) {
        f.setProperty(RStyle, s->id);
        const RunProps hp = m_pkg.styles.charStyleRun(s->id);
        if (hp.color && *hp.color != QLatin1String("auto"))
            f.setForeground(QColor(QLatin1Char('#') + *hp.color));
        f.setFontUnderline(hp.u.value_or(true));
    } else {
        f.setForeground(QColor(0x05, 0x63, 0xC1));
        f.setFontUnderline(true);
    }
    if (c.hasSelection()) {
        c.mergeCharFormat(f);
    } else {
        QTextCharFormat full = c.charFormat();
        full.merge(f);
        c.insertText(url.trimmed(), full);
    }
}

void MainWindow::find()
{
    bool ok = false;
    const QString t = QInputDialog::getText(this, tr("Find"), tr("Find:"), QLineEdit::Normal, m_findText, &ok);
    if (!ok || t.isEmpty())
        return;
    m_findText = t;
    findNext();
}

void MainWindow::findNext()
{
    if (m_findText.isEmpty()) {
        find();
        return;
    }
    if (!m_edit->find(m_findText)) {
        m_edit->moveCursor(QTextCursor::Start);
        if (!m_edit->find(m_findText))
            statusBar()->showMessage(tr("\"%1\" not found").arg(m_findText), 3000);
    }
}

// ---------------------------------------------------------------------- ui

void MainWindow::updateFormatUi()
{
    if (m_updatingUi)
        return;
    m_updatingUi = true;
    const QTextCharFormat cf = m_edit->currentCharFormat();
    m_bold->setChecked(cf.fontWeight() >= QFont::DemiBold);
    m_italic->setChecked(cf.fontItalic());
    m_underline->setChecked(cf.fontUnderline());
    m_strike->setChecked(cf.fontStrikeOut());
    m_super->setChecked(cf.verticalAlignment() == QTextCharFormat::AlignSuperScript);
    m_sub->setChecked(cf.verticalAlignment() == QTextCharFormat::AlignSubScript);

    const QStringList fams = cf.fontFamilies().toStringList();
    const QString family = fams.isEmpty() ? cf.font().family() : fams.first();
    m_fontCombo->setCurrentText(family);
    const double pt = cf.fontPointSize() > 0 ? cf.fontPointSize() : cf.font().pointSizeF();
    m_sizeCombo->setEditText(QString::number(pt, 'g', 4));

    const QTextBlockFormat bf = m_edit->textCursor().blockFormat();
    const Qt::Alignment a = bf.alignment() & Qt::AlignHorizontal_Mask;
    if (a & Qt::AlignHCenter)
        m_alignCenter->setChecked(true);
    else if (a & Qt::AlignRight)
        m_alignRight->setChecked(true);
    else if (a & Qt::AlignJustify)
        m_alignJustify->setChecked(true);
    else
        m_alignLeft->setChecked(true);

    QString styleId = bf.stringProperty(PStyle);
    if (styleId.isEmpty())
        styleId = m_pkg.styles.defaultParagraphStyle();
    int idx = m_styleCombo->findData(styleId);
    if (idx < 0) {
        const Style *s = m_pkg.styles.style(styleId);
        m_styleCombo->addItem(s && !s->name.isEmpty() ? s->name : styleId, styleId);
        idx = m_styleCombo->count() - 1;
    }
    m_styleCombo->setCurrentIndex(idx);

    const bool inTable = m_edit->textCursor().currentTable() != nullptr;
    for (QAction *act : {m_rowAbove, m_rowBelow, m_colLeft, m_colRight, m_delRow, m_delCol, m_delTable, m_merge, m_split})
        act->setEnabled(inTable);
    m_updatingUi = false;
}

void MainWindow::updateWordCount()
{
    const QString text = m_edit->document()->toPlainText();
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    const int words = text.trimmed().isEmpty() ? 0 : int(text.trimmed().split(ws, Qt::SkipEmptyParts).size());
    m_wordCount->setText(tr("%n word(s)", nullptr, words));
}
