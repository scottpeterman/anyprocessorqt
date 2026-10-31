#pragma once

#include <QTextDocument>
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QTextEdit;
class QTimer;

// Find & Replace strip shown under the editor (Ctrl+F / Ctrl+H).
class FindBar : public QWidget {
    Q_OBJECT
public:
    explicit FindBar(QTextEdit *edit, QWidget *parent = nullptr);

    void showFind();
    void showReplace();
    bool findNext();
    bool findPrevious();

    // Document-level operations, also used by the tests.
    // Replace every match in one undo step, keeping each match's formatting. Returns the count.
    static int replaceAll(QTextDocument *doc, const QString &query, const QString &replacement,
                          QTextDocument::FindFlags flags);
    static QList<QTextCursor> matches(QTextDocument *doc, const QString &query, QTextDocument::FindFlags flags,
                                      int limit = 5000);
    // Replace the cursor's selection, keeping the formatting of its first character.
    static void replaceSelection(QTextCursor &cursor, const QString &replacement);

protected:
    void keyPressEvent(QKeyEvent *e) override;
    bool eventFilter(QObject *obj, QEvent *e) override;

private:
    QTextDocument::FindFlags flags() const;
    bool find(bool backward);
    bool selectionMatches() const;
    void replaceOne();
    void replaceAllClicked();
    void refreshHighlights();
    void scheduleRefresh();
    void closeBar();

    QTextEdit *m_edit;
    QLineEdit *m_find;
    QLineEdit *m_replace;
    QWidget *m_replaceRow;
    QCheckBox *m_case;
    QCheckBox *m_words;
    QLabel *m_count;
    QTimer *m_refresh;
};
