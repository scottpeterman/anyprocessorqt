#pragma once

#include "docx/HeaderFooter.h"
#include "docx/Package.h"

#include <QMainWindow>
#include <QTextCharFormat>
#include <QTextListFormat>

class PageEdit;
class QAction;
class QComboBox;
class QFontComboBox;
class QLabel;
class QMenu;
class QTimer;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    bool openFile(const QString &path);
    PageEdit *editor() const { return m_edit; }

protected:
    void closeEvent(QCloseEvent *e) override;
    void changeEvent(QEvent *e) override;

private:
    void buildActions();
    void buildMenus();
    void buildToolbars();

    void newDocument();
    void open();
    bool save();
    bool saveAs();
    bool saveTo(const QString &path);
    void exportPdf();
    void print();
    bool maybeSave();
    void setCurrentPath(const QString &path);
    void rebuildStyleCombo();

    void mergeFormat(const QTextCharFormat &fmt);
    void applyParagraphStyle(const QString &styleId);
    void clearFormatting();
    void setAlignment(Qt::Alignment a);
    void toggleList(QTextListFormat::Style style);
    void changeIndent(int delta);
    void setLineSpacing(double multiple);
    void chooseTextColor();
    void toggleHighlight();
    void insertImage();
    void resizeImage();
    void insertTable();
    void tableProperties();
    void insertPageBreak();
    void insertHyperlink();
    void toggleCodeBlock();
    void toggleInlineCode();
    void fixContextualSpacingAroundCursor();

    void setDraftView(bool draft);
    void setTheme(int theme);   // theme::Theme
    void setPaper(int paper);   // theme::Paper
    void refreshColors();
    void buildSpellingMenu(QMenu *menu);
    void buildRecentMenu();
    void openRecent(const QString &path);

    void updateFormatUi();
    void updateWordCount();
    void updatePageUi();

    PageEdit *m_edit = nullptr;
    docx::Package m_pkg;
    docx::HeaderFooters m_hf;
    void editHeaderFooter(bool footer, int page);
    QString m_path;
    class FindBar *m_findBar = nullptr;
    class ZoomView *m_zoomView = nullptr;
    class QToolButton *m_zoomButton = nullptr;
    void updateZoomUi(double factor, bool fit);
    int m_lastBlockCount = 0;

    QComboBox *m_styleCombo = nullptr;
    QFontComboBox *m_fontCombo = nullptr;
    QComboBox *m_sizeCombo = nullptr;
    QLabel *m_wordCount = nullptr;
    QLabel *m_pageLabel = nullptr;
    QTimer *m_countTimer = nullptr;
    bool m_updatingUi = false;

    QAction *m_new, *m_open, *m_save, *m_saveAs, *m_exportPdf, *m_print, *m_quit;
    QAction *m_undo, *m_redo, *m_cut, *m_copy, *m_paste, *m_selectAll, *m_find, *m_replace, *m_findNext, *m_findPrev;
    QAction *m_codeBlock, *m_inlineCode;
    QAction *m_bold, *m_italic, *m_underline, *m_strike, *m_super, *m_sub, *m_color, *m_highlight, *m_clearFmt;
    QAction *m_alignLeft, *m_alignCenter, *m_alignRight, *m_alignJustify;
    QAction *m_bullets, *m_numbers, *m_indentMore, *m_indentLess;
    QAction *m_insImage, *m_resizeImage, *m_insTable, *m_insBreak, *m_insLink, *m_insHeaderFooter;
    QAction *m_rowAbove, *m_rowBelow, *m_colLeft, *m_colRight, *m_delRow, *m_delCol, *m_delTable, *m_merge, *m_split, *m_tableProps;
    QAction *m_about;
    QAction *m_pageView, *m_draftView, *m_spellAsYouType, *m_nextMisspelling;
    QAction *m_zoomIn, *m_zoomOut, *m_zoomFit, *m_zoom100;
    QAction *m_themeSystem, *m_themeLight, *m_themeDark, *m_paperWhite, *m_paperDark;
    bool m_refreshingColors = false;
    QMenu *m_languageMenu = nullptr;
    QMenu *m_recentMenu = nullptr;
};
