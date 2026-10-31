#include "docx/HeaderFooter.h"
#include "docx/DocxReader.h"
#include "docx/Units.h"

#include <QAbstractTextDocumentLayout>
#include <QPainter>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

namespace docx {

HeaderFooters::HeaderFooters() = default;
HeaderFooters::~HeaderFooters() = default;

void HeaderFooters::clear()
{
    m_entries.clear();
    m_titlePg = m_evenAndOdd = false;
}

void HeaderFooters::load(const Package &pkg)
{
    clear();
    m_titlePg = pkg.titlePg;
    m_evenAndOdd = pkg.evenAndOdd;
    for (const HdrFtrRef &r : pkg.hdrFtr) {
        Entry e;
        e.footer = r.footer;
        e.type = r.type;
        e.doc = std::make_unique<QTextDocument>();
        QString err;
        if (!DocxReader::buildPart(pkg, r.part, e.doc.get(), &err))
            continue;
        e.editable = isTextOnly(pkg.parts.value(r.part));
        m_entries.push_back(std::move(e));
    }
}

QTextDocument *HeaderFooters::doc(bool footer, const QString &type) const
{
    for (const Entry &e : m_entries)
        if (e.footer == footer && e.type == type)
            return e.doc.get();
    return nullptr;
}

bool HeaderFooters::editable(bool footer, const QString &type) const
{
    for (const Entry &e : m_entries)
        if (e.footer == footer && e.type == type)
            return e.editable;
    return true;  // nothing there yet: a new part is text-only
}

QString HeaderFooters::typeForPage(int page) const
{
    if (page == 0 && m_titlePg)
        return QStringLiteral("first");
    if (m_evenAndOdd && (page + 1) % 2 == 0)
        return QStringLiteral("even");
    return QStringLiteral("default");
}

bool HeaderFooters::isTextOnly(const QByteArray &xml)
{
    static const QStringList blockers = {
        QStringLiteral("<w:drawing"), QStringLiteral("<w:pict"),   QStringLiteral("<w:tbl"),
        QStringLiteral("<w:object"),  QStringLiteral("txbxContent"), QStringLiteral("<w:hyperlink"),
        QStringLiteral("AlternateContent"), QStringLiteral("<w:numPr"), QStringLiteral("<w:framePr"),
    };
    const QString s = QString::fromUtf8(xml);
    for (const QString &b : blockers)
        if (s.contains(b))
            return false;
    // Only PAGE / NUMPAGES fields survive a rewrite.
    static const QRegularExpression simple(QStringLiteral("w:instr=\"\\s*([A-Za-z]+)"));
    static const QRegularExpression complex(QStringLiteral("<w:instrText[^>]*>\\s*([A-Za-z]+)"));
    for (const QRegularExpression *re : {&simple, &complex}) {
        auto it = re->globalMatch(s);
        while (it.hasNext()) {
            const QString w = it.next().captured(1).toUpper();
            if (w != QLatin1String("PAGE") && w != QLatin1String("NUMPAGES") && w != QLatin1String("SECTIONPAGES"))
                return false;
        }
    }
    return true;
}

void HeaderFooters::fillFields(QTextDocument *doc, int pageNumber, int pageCount)
{
    struct Hit {
        int pos, len;
        QString text;
        QTextCharFormat fmt;
    };
    QList<Hit> hits;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            const QString field = f.charFormat().stringProperty(FieldInstr);
            if (field.isEmpty())
                continue;
            const int v = field == QLatin1String("NUMPAGES") ? pageCount : pageNumber;
            hits.append({f.position(), f.length(), QString::number(v), f.charFormat()});
        }
    }
    for (int i = int(hits.size()) - 1; i >= 0; --i) {
        QTextCursor c(doc);
        c.setPosition(hits[i].pos);
        c.setPosition(hits[i].pos + hits[i].len, QTextCursor::KeepAnchor);
        c.insertText(hits[i].text, hits[i].fmt);
    }
}

void HeaderFooters::paint(QPainter &p, int page, int pageCount, const PageSetup &setup, const QColor &defaultInk,
                          const std::function<QColor(const QColor &)> &ink) const
{
    const QString type = typeForPage(page);
    const qreal left = twipsToPx(setup.left);
    for (bool footer : {false, true}) {
        const QTextDocument *src = doc(footer, type);
        if (!src)
            continue;
        std::unique_ptr<QTextDocument> d(src->clone());
        d->setTextWidth(twipsToPx(setup.width - setup.left - setup.right));
        fillFields(d.get(), page + 1, pageCount);
        if (ink) {
            QList<QPair<QPair<int, int>, QColor>> recolor;
            for (QTextBlock b = d->begin(); b.isValid(); b = b.next())
                for (auto it = b.begin(); !it.atEnd(); ++it) {
                    const QTextFragment f = it.fragment();
                    if (f.charFormat().foreground().style() != Qt::NoBrush)
                        recolor.append({{f.position(), f.length()}, ink(f.charFormat().foreground().color())});
                }
            for (const auto &r : recolor) {
                QTextCharFormat cf;
                cf.setForeground(r.second);
                QTextCursor c(d.get());
                c.setPosition(r.first.first);
                c.setPosition(r.first.first + r.first.second, QTextCursor::KeepAnchor);
                c.mergeCharFormat(cf);
            }
        }
        const qreal h = d->documentLayout()->documentSize().height();
        const qreal top = footer ? twipsToPx(setup.height - setup.footer) - h : twipsToPx(setup.header);
        p.save();
        p.translate(left, top);
        QAbstractTextDocumentLayout::PaintContext ctx;
        ctx.palette.setColor(QPalette::Text, defaultInk);
        d->documentLayout()->draw(&p, ctx);
        p.restore();
    }
}

} // namespace docx
