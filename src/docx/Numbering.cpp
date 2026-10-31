#include "docx/Numbering.h"
#include "docx/Xml.h"

namespace docx {

void Numbering::parse(const QByteArray &numberingXml)
{
    m_abstract.clear();
    m_numToAbs.clear();
    m_startOverride.clear();
    m_maxAbs = 0;
    m_maxNum = 0;
    auto root = parseXml(numberingXml);
    if (!root)
        return;
    for (const XElem *a : root->children(u"abstractNum")) {
        const int aid = a->intAttr(u"abstractNumId", -1);
        if (aid < 0)
            continue;
        m_maxAbs = qMax(m_maxAbs, aid);
        QHash<int, NumLevel> levels;
        for (const XElem *l : a->children(u"lvl")) {
            NumLevel lv;
            const int il = l->intAttr(u"ilvl", 0);
            if (const XElem *e = l->child(u"numFmt"))
                lv.fmt = e->attr(u"val");
            if (const XElem *e = l->child(u"lvlText"))
                lv.text = e->attr(u"val");
            if (const XElem *e = l->child(u"start"))
                lv.start = e->intAttr(u"val", 1);
            lv.p = ParaProps::parse(l->child(u"pPr"));
            lv.r = RunProps::parse(l->child(u"rPr"));
            levels.insert(il, lv);
        }
        m_abstract.insert(aid, levels);
    }
    for (const XElem *n : root->children(u"num")) {
        const int nid = n->intAttr(u"numId", -1);
        if (nid < 0)
            continue;
        m_maxNum = qMax(m_maxNum, nid);
        if (const XElem *e = n->child(u"abstractNumId"))
            m_numToAbs.insert(nid, e->intAttr(u"val", -1));
        for (const XElem *o : n->children(u"lvlOverride")) {
            if (const XElem *s = o->child(u"startOverride"))
                m_startOverride[nid].insert(o->intAttr(u"ilvl", 0), s->intAttr(u"val", 1));
        }
    }
}

const NumLevel *Numbering::level(int numId, int ilvl) const
{
    auto a = m_numToAbs.constFind(numId);
    if (a == m_numToAbs.constEnd())
        return nullptr;
    auto levels = m_abstract.constFind(a.value());
    if (levels == m_abstract.constEnd())
        return nullptr;
    auto l = levels->constFind(ilvl);
    return l == levels->constEnd() ? nullptr : &l.value();
}

bool Numbering::isRealList(int numId, int ilvl) const
{
    if (numId <= 0)
        return false;
    const NumLevel *l = level(numId, ilvl);
    return l && l->fmt != QLatin1String("none");
}

} // namespace docx
