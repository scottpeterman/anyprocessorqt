#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <memory>

class Hunspell;

namespace spell {

struct Dictionary {
    QString code;     // "en_US"
    QString affPath;
    QString dicPath;
    bool bundled = false;
};

// One word found in a run of text: [start, start+length) in the source string.
struct Word {
    int start = 0;
    int length = 0;
    QString text;     // normalized for lookup (curly apostrophes -> ')
};

// Hunspell wrapper with a personal dictionary and session ignore list.
// Single instance shared by every editor window.
class SpellChecker : public QObject {
    Q_OBJECT
public:
    static SpellChecker &instance();

    QList<Dictionary> availableDictionaries() const;
    bool setLanguage(const QString &code);
    QString language() const { return m_language; }
    bool isLoaded() const { return m_hunspell != nullptr; }

    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool on);

    bool isCorrect(const QString &word);
    QStringList suggestions(const QString &word, int max = 8);
    void addToPersonal(const QString &word);
    void ignore(const QString &word);  // this session only

    // Words worth checking in a run of text. Skips acronyms (BGP), CamelCase
    // and mixed-case names (NetBox), anything with digits or underscores, and
    // whole tokens that look like URLs, paths, emails or file names.
    static QList<Word> words(const QString &text);
    static bool shouldCheck(const QString &word);

    QString personalDictionaryPath() const;

signals:
    void changed();  // language, enabled state or word lists changed: re-check

private:
    SpellChecker();
    ~SpellChecker() override;
    QByteArray encode(const QString &s) const;
    QString decode(const std::string &s) const;
    void loadPersonal();
    static QString extractBundled(const QString &code);

    std::unique_ptr<Hunspell> m_hunspell;
    QString m_language;
    QString m_encoding;
    bool m_enabled = true;
    QSet<QString> m_personal;
    QSet<QString> m_ignored;
    QHash<QString, bool> m_cache;
};

} // namespace spell
