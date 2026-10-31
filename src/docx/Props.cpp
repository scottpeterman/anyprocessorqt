#include "docx/Props.h"
#include "docx/Xml.h"

namespace docx {

bool parseOnOff(const XElem *e)
{
    if (!e)
        return false;
    if (!e->hasAttr(u"val"))
        return true;
    const QString v = e->attr(u"val").toLower();
    return !(v == QLatin1String("0") || v == QLatin1String("false") || v == QLatin1String("off")
             || v == QLatin1String("none"));
}

template <typename T>
static void over(std::optional<T> &dst, const std::optional<T> &src)
{
    if (src)
        dst = src;
}

void RunProps::merge(const RunProps &o)
{
    over(font, o.font);
    over(halfPts, o.halfPts);
    over(b, o.b);
    over(i, o.i);
    over(u, o.u);
    over(strike, o.strike);
    over(color, o.color);
    over(highlight, o.highlight);
    over(shd, o.shd);
    over(vertAlign, o.vertAlign);
}

RunProps RunProps::parse(const XElem *rPr)
{
    RunProps r;
    if (!rPr)
        return r;
    for (const auto &k : rPr->kids) {
        const QString &n = k->name;
        if (n == QLatin1String("rFonts")) {
            QString f = k->attr(u"ascii");
            if (f.isEmpty())
                f = k->attr(u"hAnsi");
            if (!f.isEmpty())
                r.font = f;
        } else if (n == QLatin1String("sz")) {
            if (k->hasAttr(u"val"))
                r.halfPts = k->intAttr(u"val", 20);
        } else if (n == QLatin1String("b")) {
            r.b = parseOnOff(k.get());
        } else if (n == QLatin1String("i")) {
            r.i = parseOnOff(k.get());
        } else if (n == QLatin1String("u")) {
            r.u = parseOnOff(k.get());
        } else if (n == QLatin1String("strike")) {
            r.strike = parseOnOff(k.get());
        } else if (n == QLatin1String("color")) {
            const QString v = k->attr(u"val");
            if (!v.isEmpty())
                r.color = v.toUpper() == QLatin1String("AUTO") ? QStringLiteral("auto") : v.toUpper();
        } else if (n == QLatin1String("highlight")) {
            const QString v = k->attr(u"val");
            r.highlight = (v == QLatin1String("none")) ? QString() : v;
        } else if (n == QLatin1String("shd")) {
            const QString fill = k->attr(u"fill");
            if (!fill.isEmpty())
                r.shd = fill.toUpper() == QLatin1String("AUTO") ? QString() : fill.toUpper();
        } else if (n == QLatin1String("vertAlign")) {
            r.vertAlign = k->attr(u"val");
        }
    }
    return r;
}

void ParaProps::merge(const ParaProps &o)
{
    over(jc, o.jc);
    over(before, o.before);
    over(after, o.after);
    over(line, o.line);
    over(lineRule, o.lineRule);
    over(left, o.left);
    over(right, o.right);
    over(firstLine, o.firstLine);
    over(keepNext, o.keepNext);
    over(keepLines, o.keepLines);
    over(pageBreakBefore, o.pageBreakBefore);
    over(contextualSpacing, o.contextualSpacing);
    over(numId, o.numId);
    over(ilvl, o.ilvl);
    for (int s = 0; s < 4; ++s)
        over(bdr[s], o.bdr[s]);
    over(shd, o.shd);
    over(outlineLvl, o.outlineLvl);
    for (const TabStop &t : o.tabs) {
        for (int k = tabs.size() - 1; k >= 0; --k)
            if (tabs[k].pos == t.pos)
                tabs.removeAt(k);
        if (t.val != QLatin1String("clear"))
            tabs.append(t);
    }
}

static QString borderSpec(const XElem *e)
{
    if (!e)
        return QString();
    const QString val = e->attr(u"val");
    if (val.isEmpty() || val == QLatin1String("nil") || val == QLatin1String("none"))
        return QString();
    QString color = e->attr(u"color").toUpper();
    if (color.isEmpty() || color == QLatin1String("AUTO"))
        color = QStringLiteral("000000");
    return QStringLiteral("%1|%2|%3|%4").arg(val).arg(e->intAttr(u"sz", 4)).arg(e->intAttr(u"space", 0)).arg(color);
}

ParaProps ParaProps::parse(const XElem *pPr)
{
    ParaProps p;
    if (!pPr)
        return p;
    for (const auto &k : pPr->kids) {
        const QString &n = k->name;
        if (n == QLatin1String("jc")) {
            p.jc = k->attr(u"val");
        } else if (n == QLatin1String("spacing")) {
            if (k->hasAttr(u"before"))
                p.before = k->intAttr(u"before");
            if (k->hasAttr(u"after"))
                p.after = k->intAttr(u"after");
            if (k->hasAttr(u"line"))
                p.line = k->intAttr(u"line", 240);
            if (k->hasAttr(u"lineRule"))
                p.lineRule = k->attr(u"lineRule");
        } else if (n == QLatin1String("ind")) {
            if (k->hasAttr(u"left"))
                p.left = k->intAttr(u"left");
            else if (k->hasAttr(u"start"))
                p.left = k->intAttr(u"start");
            if (k->hasAttr(u"right"))
                p.right = k->intAttr(u"right");
            else if (k->hasAttr(u"end"))
                p.right = k->intAttr(u"end");
            if (k->hasAttr(u"hanging"))
                p.firstLine = -k->intAttr(u"hanging");
            else if (k->hasAttr(u"firstLine"))
                p.firstLine = k->intAttr(u"firstLine");
        } else if (n == QLatin1String("keepNext")) {
            p.keepNext = parseOnOff(k.get());
        } else if (n == QLatin1String("keepLines")) {
            p.keepLines = parseOnOff(k.get());
        } else if (n == QLatin1String("pageBreakBefore")) {
            p.pageBreakBefore = parseOnOff(k.get());
        } else if (n == QLatin1String("contextualSpacing")) {
            p.contextualSpacing = parseOnOff(k.get());
        } else if (n == QLatin1String("numPr")) {
            if (const XElem *id = k->child(u"numId"))
                p.numId = id->intAttr(u"val");
            if (const XElem *lv = k->child(u"ilvl"))
                p.ilvl = lv->intAttr(u"val");
        } else if (n == QLatin1String("pBdr")) {
            static const char *sides[4] = {"top", "left", "bottom", "right"};
            static const char *alt[4] = {"top", "start", "bottom", "end"};
            for (int s = 0; s < 4; ++s) {
                const XElem *e = k->child(QString::fromLatin1(sides[s]));
                if (!e)
                    e = k->child(QString::fromLatin1(alt[s]));
                if (e)
                    p.bdr[s] = borderSpec(e);
            }
        } else if (n == QLatin1String("shd")) {
            const QString fill = k->attr(u"fill").toUpper();
            p.shd = (fill.isEmpty() || fill == QLatin1String("AUTO")) ? QString() : fill;
        } else if (n == QLatin1String("tabs")) {
            for (const XElem *t : k->children(u"tab")) {
                TabStop ts;
                ts.val = t->attr(u"val");
                if (ts.val == QLatin1String("start"))
                    ts.val = QStringLiteral("left");
                else if (ts.val == QLatin1String("end"))
                    ts.val = QStringLiteral("right");
                ts.pos = t->intAttr(u"pos");
                ts.leader = t->attr(u"leader");
                // "clear" entries are kept here; merge() applies them against inherited stops.
                p.tabs.append(ts);
            }
        } else if (n == QLatin1String("outlineLvl")) {
            p.outlineLvl = k->intAttr(u"val");
        }
    }
    return p;
}

} // namespace docx
