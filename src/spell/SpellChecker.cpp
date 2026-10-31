#include "spell/SpellChecker.h"

#include <hunspell.hxx>

#include "docx/Units.h"

#include <QCoreApplication>
#include <QFontInfo>
#include <QTextBlock>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringDecoder>
#include <QStringEncoder>

static void initDictionaryResources()
{
    // The dictionaries live in a static library's resources; reference them so
    // the linker keeps them.
    Q_INIT_RESOURCE(dictionaries);
}

namespace spell {

SpellChecker &SpellChecker::instance()
{
    static SpellChecker s;
    return s;
}

SpellChecker::SpellChecker()
{
    initDictionaryResources();
    loadPersonal();
}

SpellChecker::~SpellChecker() = default;

// Bundled dictionaries are Qt resources; Hunspell wants file paths, so copy
// them to the app data directory once (and again if the bundled one changes).
QString SpellChecker::extractBundled(const QString &code)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                        + QStringLiteral("/dictionaries/bundled");
    QDir().mkpath(dir);
    for (const char *ext : {".aff", ".dic"}) {
        const QString name = code + QLatin1String(ext);
        QFile src(QStringLiteral(":/dictionaries/") + name);
        const QString dst = dir + QLatin1Char('/') + name;
        if (QFileInfo(dst).size() == src.size())
            continue;
        if (!src.open(QIODevice::ReadOnly))
            return QString();
        QSaveFile out(dst);
        if (!out.open(QIODevice::WriteOnly))
            return QString();
        out.write(src.readAll());
        if (!out.commit())
            return QString();
    }
    return dir;
}

QList<Dictionary> SpellChecker::availableDictionaries() const
{
    QList<Dictionary> out;
    QSet<QString> seen;
    auto add = [&](const Dictionary &d) {
        if (seen.contains(d.code))
            return;
        seen.insert(d.code);
        out.append(d);
    };
    for (const QString &code : QDir(QStringLiteral(":/dictionaries")).entryList({QStringLiteral("*.dic")})) {
        Dictionary d;
        d.code = QFileInfo(code).completeBaseName();
        d.bundled = true;
        add(d);
    }
    QStringList dirs = {QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/dictionaries")};
#if defined(Q_OS_LINUX)
    dirs << QDir::homePath() + QStringLiteral("/.local/share/hunspell") << QStringLiteral("/usr/share/hunspell")
         << QStringLiteral("/usr/share/myspell") << QStringLiteral("/usr/share/myspell/dicts");
#elif defined(Q_OS_MACOS)
    dirs << QDir::homePath() + QStringLiteral("/Library/Spelling");
#endif
    for (const QString &dir : dirs) {
        for (const QFileInfo &fi : QDir(dir).entryInfoList({QStringLiteral("*.dic")}, QDir::Files)) {
            const QString aff = fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + QStringLiteral(".aff");
            if (!QFileInfo::exists(aff))
                continue;
            Dictionary d;
            d.code = fi.completeBaseName();
            d.affPath = aff;
            d.dicPath = fi.absoluteFilePath();
            add(d);
        }
    }
    std::sort(out.begin(), out.end(), [](const Dictionary &a, const Dictionary &b) { return a.code < b.code; });
    return out;
}

bool SpellChecker::setLanguage(const QString &code)
{
    for (Dictionary d : availableDictionaries()) {
        if (d.code != code)
            continue;
        if (d.bundled) {
            const QString dir = extractBundled(code);
            if (dir.isEmpty())
                return false;
            d.affPath = dir + QLatin1Char('/') + code + QStringLiteral(".aff");
            d.dicPath = dir + QLatin1Char('/') + code + QStringLiteral(".dic");
        }
        auto h = std::make_unique<Hunspell>(QFile::encodeName(d.affPath).constData(),
                                            QFile::encodeName(d.dicPath).constData());
        m_encoding = QString::fromLatin1(h->get_dict_encoding().c_str()).toUpper();
        m_hunspell = std::move(h);
        m_language = code;
        m_cache.clear();
        // Built-in technical supplement, then the user's personal words.
        QFile tech(QStringLiteral(":/wordlists/tech.txt"));
        if (tech.open(QIODevice::ReadOnly | QIODevice::Text)) {
            while (!tech.atEnd()) {
                const QString w = QString::fromUtf8(tech.readLine()).trimmed();
                if (!w.isEmpty() && !w.startsWith(QLatin1Char('#')))
                    m_hunspell->add(encode(w).toStdString());
            }
        }
        for (const QString &w : std::as_const(m_personal))
            m_hunspell->add(encode(w).toStdString());
        emit changed();
        return true;
    }
    return false;
}

void SpellChecker::setEnabled(bool on)
{
    if (m_enabled == on)
        return;
    m_enabled = on;
    emit changed();
}

QByteArray SpellChecker::encode(const QString &s) const
{
    if (m_encoding.isEmpty() || m_encoding == QLatin1String("UTF-8") || m_encoding == QLatin1String("UTF8"))
        return s.toUtf8();
    if (m_encoding == QLatin1String("ISO8859-1") || m_encoding == QLatin1String("ISO-8859-1"))
        return s.toLatin1();
    QStringEncoder enc(m_encoding.toLatin1().constData());
    if (enc.isValid())
        return enc.encode(s);
    return s.toLatin1();
}

QString SpellChecker::decode(const std::string &s) const
{
    const QByteArray b = QByteArray::fromStdString(s);
    if (m_encoding.isEmpty() || m_encoding == QLatin1String("UTF-8") || m_encoding == QLatin1String("UTF8"))
        return QString::fromUtf8(b);
    if (m_encoding == QLatin1String("ISO8859-1") || m_encoding == QLatin1String("ISO-8859-1"))
        return QString::fromLatin1(b);
    QStringDecoder dec(m_encoding.toLatin1().constData());
    if (dec.isValid())
        return dec.decode(b);
    return QString::fromLatin1(b);
}

bool SpellChecker::isCorrect(const QString &word)
{
    if (!m_hunspell)
        return true;
    if (m_personal.contains(word) || m_ignored.contains(word))
        return true;
    auto it = m_cache.constFind(word);
    if (it != m_cache.constEnd())
        return it.value();
    bool ok = m_hunspell->spell(encode(word).toStdString());
    // Sentence-initial capital: accept if the lower-case form is known.
    if (!ok && word.size() > 1 && word.at(0).isUpper())
        ok = m_hunspell->spell(encode(word.at(0).toLower() + word.mid(1)).toStdString());
    if (m_cache.size() > 50000)
        m_cache.clear();
    m_cache.insert(word, ok);
    return ok;
}

QStringList SpellChecker::suggestions(const QString &word, int max)
{
    QStringList out;
    if (!m_hunspell)
        return out;
    for (const std::string &s : m_hunspell->suggest(encode(word).toStdString())) {
        out << decode(s);
        if (out.size() >= max)
            break;
    }
    return out;
}

QString SpellChecker::personalDictionaryPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/personal-dictionary.txt");
}

void SpellChecker::loadPersonal()
{
    QFile f(personalDictionaryPath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    while (!f.atEnd()) {
        const QString w = QString::fromUtf8(f.readLine()).trimmed();
        if (!w.isEmpty())
            m_personal.insert(w);
    }
}

void SpellChecker::addToPersonal(const QString &word)
{
    if (word.isEmpty() || m_personal.contains(word))
        return;
    m_personal.insert(word);
    if (m_hunspell)
        m_hunspell->add(encode(word).toStdString());
    m_cache.remove(word);
    const QString path = personalDictionaryPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::Append | QIODevice::Text))
        f.write(word.toUtf8() + '\n');
    emit changed();
}

void SpellChecker::ignore(const QString &word)
{
    m_ignored.insert(word);
    emit changed();
}

static bool isCodeFormat(const QTextCharFormat &cf)
{
    if (cf.stringProperty(docx::RStyle) == QLatin1String("CodeChar"))
        return true;
    static QHash<QString, bool> fixedCache;
    const QStringList fams = cf.fontFamilies().toStringList();
    const QString key = fams.isEmpty() ? cf.font().family() : fams.first();
    auto it = fixedCache.constFind(key);
    if (it != fixedCache.constEnd())
        return it.value();
    const bool fixed = QFontInfo(cf.font()).fixedPitch();
    fixedCache.insert(key, fixed);
    return fixed;
}

QList<Word> SpellChecker::checkableWords(const QTextBlock &block)
{
    const QString style = block.blockFormat().stringProperty(docx::PStyle);
    if (style == QLatin1String("Code") || style == QLatin1String("SourceCode") || style == QLatin1String("HTMLPreformatted"))
        return {};
    QList<QPair<int, int>> code;  // [start, end) ranges inside the block
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment f = it.fragment();
        if (f.isValid() && isCodeFormat(f.charFormat()))
            code.append({f.position() - block.position(), f.position() - block.position() + f.length()});
    }
    QList<Word> all = words(block.text());
    if (code.isEmpty())
        return all;
    QList<Word> out;
    for (const Word &w : all) {
        bool inCode = false;
        for (const auto &r : code)
            if (w.start < r.second && w.start + w.length > r.first)
                inCode = true;
        if (!inCode)
            out.append(w);
    }
    return out;
}

bool SpellChecker::shouldCheck(const QString &word)
{
    if (word.size() < 2)
        return false;
    int upper = 0;
    for (int i = 0; i < word.size(); ++i) {
        const QChar c = word.at(i);
        if (c.isDigit() || c == QLatin1Char('_'))
            return false;
        if (c.isUpper()) {
            ++upper;
            if (i > 0)
                return false;  // CamelCase / ALLCAPS: names, acronyms, identifiers
        }
    }
    return true;
}

QList<Word> SpellChecker::words(const QString &text)
{
    QList<Word> out;
    // Whitespace-separated chunks first, so URLs/paths/emails can be skipped whole.
    static const QRegularExpression chunkRe(QStringLiteral("\\S+"));
    static const QRegularExpression wordRe(QStringLiteral("[\\p{L}\\p{M}\\p{N}_]+(?:['\\x{2019}][\\p{L}\\p{M}]+)*"));
    auto it = chunkRe.globalMatch(text);
    while (it.hasNext()) {
        const auto chunk = it.next();
        const QString c = chunk.captured(0);
        if (c.contains(QLatin1String("://")) || c.contains(QLatin1Char('@')) || c.contains(QLatin1Char('/'))
            || c.contains(QLatin1Char('\\')) || c.startsWith(QLatin1String("www.")))
            continue;
        // file.ext, a.b.c, dotted identifiers
        static const QRegularExpression dotted(QStringLiteral("[\\p{L}\\p{N}_]\\.[\\p{L}\\p{N}_]"));
        if (dotted.match(c).hasMatch())
            continue;
        auto wit = wordRe.globalMatch(c);
        while (wit.hasNext()) {
            const auto m = wit.next();
            QString w = m.captured(0);
            w.replace(QChar(0x2019), QLatin1Char('\''));
            if (!shouldCheck(w))
                continue;
            out.append(Word{int(chunk.capturedStart(0) + m.capturedStart(0)), int(m.capturedLength(0)), w});
        }
    }
    return out;
}

} // namespace spell
