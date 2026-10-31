#include "docx/Xml.h"

#include <QXmlStreamReader>

namespace docx {

const XElem *XElem::child(QStringView local) const
{
    for (const auto &k : kids)
        if (k->name == local)
            return k.get();
    return nullptr;
}

QList<const XElem *> XElem::children(QStringView local) const
{
    QList<const XElem *> out;
    for (const auto &k : kids)
        if (local.isEmpty() || k->name == local)
            out.append(k.get());
    return out;
}

QString XElem::attr(QStringView local) const
{
    for (const QXmlStreamAttribute &a : attrs)
        if (a.name() == local)
            return a.value().toString();
    return QString();
}

bool XElem::hasAttr(QStringView local) const
{
    for (const QXmlStreamAttribute &a : attrs)
        if (a.name() == local)
            return true;
    return false;
}

int XElem::intAttr(QStringView local, int def) const
{
    for (const QXmlStreamAttribute &a : attrs) {
        if (a.name() == local) {
            bool ok = false;
            // Some producers write fractional twips ("720.0"); accept them.
            const double v = a.value().toDouble(&ok);
            return ok ? int(qRound(v)) : def;
        }
    }
    return def;
}

std::unique_ptr<XElem> parseXml(const QByteArray &data, QString *error)
{
    QXmlStreamReader r(data);
    std::unique_ptr<XElem> root;
    std::vector<XElem *> stack;
    while (!r.atEnd()) {
        const auto tok = r.readNext();
        if (tok == QXmlStreamReader::StartElement) {
            auto e = std::make_unique<XElem>();
            e->ns = r.namespaceUri().toString();
            e->name = r.name().toString();
            e->attrs = r.attributes();
            XElem *raw = e.get();
            if (stack.empty()) {
                root = std::move(e);
            } else {
                stack.back()->kids.push_back(std::move(e));
            }
            stack.push_back(raw);
        } else if (tok == QXmlStreamReader::EndElement) {
            if (!stack.empty())
                stack.pop_back();
        } else if (tok == QXmlStreamReader::Characters) {
            if (!stack.empty())
                stack.back()->text += r.text();
        }
    }
    if (r.hasError()) {
        if (error)
            *error = QStringLiteral("XML error at line %1: %2").arg(r.lineNumber()).arg(r.errorString());
        return nullptr;
    }
    return root;
}

QString xmlEsc(const QString &s)
{
    QString out;
    out.reserve(s.size());
    for (const QChar c : s) {
        switch (c.unicode()) {
        case '&': out += QLatin1String("&amp;"); break;
        case '<': out += QLatin1String("&lt;"); break;
        case '>': out += QLatin1String("&gt;"); break;
        default:
            // Drop control characters that are illegal in XML 1.0.
            if (c.unicode() < 0x20 && c != QLatin1Char('\t') && c != QLatin1Char('\n') && c != QLatin1Char('\r'))
                continue;
            out += c;
        }
    }
    return out;
}

QString xmlAttrEsc(const QString &s)
{
    QString out = xmlEsc(s);
    out.replace(QLatin1Char('"'), QLatin1String("&quot;"));
    return out;
}

} // namespace docx
