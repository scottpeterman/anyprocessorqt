#include "docx/Styles.h"
#include "docx/Xml.h"

#include <QSet>

namespace docx {

void Styles::parse(const QByteArray &stylesXml)
{
    m_styles.clear();
    m_order.clear();
    m_defRun = RunProps();
    m_defPara = ParaProps();
    m_defaultPara = QStringLiteral("Normal");

    auto root = parseXml(stylesXml);
    if (!root)
        return;

    if (const XElem *dd = root->child(u"docDefaults")) {
        if (const XElem *rd = dd->child(u"rPrDefault"))
            m_defRun = RunProps::parse(rd->child(u"rPr"));
        if (const XElem *pd = dd->child(u"pPrDefault"))
            m_defPara = ParaProps::parse(pd->child(u"pPr"));
    }

    for (const XElem *s : root->children(u"style")) {
        Style st;
        st.id = s->attr(u"styleId");
        st.type = s->attr(u"type");
        st.isDefault = s->attr(u"default") == QLatin1String("1") || s->attr(u"default") == QLatin1String("true");
        if (const XElem *e = s->child(u"name"))
            st.name = e->attr(u"val");
        if (const XElem *e = s->child(u"basedOn"))
            st.basedOn = e->attr(u"val");
        if (const XElem *e = s->child(u"next"))
            st.next = e->attr(u"val");
        st.qFormat = s->child(u"qFormat") != nullptr;
        st.p = ParaProps::parse(s->child(u"pPr"));
        st.r = RunProps::parse(s->child(u"rPr"));
        if (st.id.isEmpty())
            continue;
        if (st.type == QLatin1String("paragraph") && st.isDefault)
            m_defaultPara = st.id;
        m_order.append(st.id);
        m_styles.insert(st.id, st);
    }
}

const Style *Styles::style(const QString &id) const
{
    auto it = m_styles.constFind(id);
    return it == m_styles.constEnd() ? nullptr : &it.value();
}

QString Styles::idForName(const QString &name) const
{
    for (const QString &id : m_order) {
        const Style &s = m_styles[id];
        if (s.name.compare(name, Qt::CaseInsensitive) == 0)
            return id;
    }
    return QString();
}

QList<const Style *> Styles::chain(const QString &id) const
{
    QList<const Style *> out;
    QSet<QString> seen;
    QString cur = id;
    while (!cur.isEmpty() && !seen.contains(cur) && out.size() < 32) {
        seen.insert(cur);
        const Style *s = style(cur);
        if (!s)
            break;
        out.prepend(s);
        cur = s->basedOn;
    }
    return out;
}

ParaProps Styles::resolvedPara(const QString &styleId) const
{
    ParaProps p;
    p.merge(m_defPara);
    for (const Style *s : chain(styleId.isEmpty() ? m_defaultPara : styleId))
        p.merge(s->p);
    return p;
}

RunProps Styles::resolvedParaRun(const QString &styleId) const
{
    RunProps r;
    r.merge(m_defRun);
    for (const Style *s : chain(styleId.isEmpty() ? m_defaultPara : styleId))
        r.merge(s->r);
    return r;
}

RunProps Styles::charStyleRun(const QString &styleId) const
{
    RunProps r;
    for (const Style *s : chain(styleId))
        r.merge(s->r);
    return r;
}

QList<QPair<QString, QString>> Styles::uiParagraphStyles() const
{
    QList<QPair<QString, QString>> out;
    QSet<QString> added;
    auto add = [&](const QString &id) {
        const Style *s = style(id);
        if (!s || s->type != QLatin1String("paragraph") || added.contains(id))
            return;
        added.insert(id);
        QString label = s->name.isEmpty() ? id : s->name;
        if (!label.isEmpty())
            label[0] = label[0].toUpper();
        out.append({id, label});
    };
    add(m_defaultPara);
    for (const char *pref : {"Title", "Subtitle", "Heading1", "Heading2", "Heading3", "Heading4", "Quote",
                             "ListParagraph"})
        add(QString::fromLatin1(pref));
    for (const QString &id : m_order) {
        const Style &s = m_styles[id];
        if (s.type == QLatin1String("paragraph") && s.qFormat)
            add(id);
    }
    return out;
}

} // namespace docx
