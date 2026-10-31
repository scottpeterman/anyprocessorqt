#include "docx/Convert.h"
#include "docx/Units.h"

#include <QAbstractTextDocumentLayout>
#include <QHash>
#include <QRawFont>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>
#include <QTextOption>
#include <QtEndian>

namespace docx {

static const QHash<QString, QString> &highlightMap()
{
    static const QHash<QString, QString> m = {
        {QStringLiteral("yellow"), QStringLiteral("FFFF00")},   {QStringLiteral("green"), QStringLiteral("00FF00")},
        {QStringLiteral("cyan"), QStringLiteral("00FFFF")},     {QStringLiteral("magenta"), QStringLiteral("FF00FF")},
        {QStringLiteral("blue"), QStringLiteral("0000FF")},     {QStringLiteral("red"), QStringLiteral("FF0000")},
        {QStringLiteral("darkBlue"), QStringLiteral("000080")}, {QStringLiteral("darkCyan"), QStringLiteral("008080")},
        {QStringLiteral("darkGreen"), QStringLiteral("008000")}, {QStringLiteral("darkMagenta"), QStringLiteral("800080")},
        {QStringLiteral("darkRed"), QStringLiteral("800000")},  {QStringLiteral("darkYellow"), QStringLiteral("808000")},
        {QStringLiteral("darkGray"), QStringLiteral("808080")}, {QStringLiteral("lightGray"), QStringLiteral("C0C0C0")},
        {QStringLiteral("black"), QStringLiteral("000000")},    {QStringLiteral("white"), QStringLiteral("FFFFFF")},
    };
    return m;
}

QColor highlightColor(const QString &name)
{
    const QString hex = highlightMap().value(name);
    return hex.isEmpty() ? QColor() : QColor(QLatin1Char('#') + hex);
}

static QString hexOf(const QColor &c)
{
    return c.name(QColor::HexRgb).mid(1).toUpper();
}

namespace {

struct LineMetrics {
    double word = 0;  // Word's single-line height in px (OS/2 win metrics)
    double qt = 0;    // Qt's laid-out single-line pitch in px
};

LineMetrics lineMetrics(const QFont &font)
{
    static QHash<QString, LineMetrics> cache;
    const QString key = font.key();
    auto it = cache.constFind(key);
    if (it != cache.constEnd())
        return it.value();
    LineMetrics m;
    const QRawFont raw = QRawFont::fromFont(font);
    const QByteArray os2 = raw.fontTable("OS/2");
    const QByteArray head = raw.fontTable("head");
    if (os2.size() >= 78 && head.size() >= 20) {
        const int upm = qFromBigEndian<quint16>(head.constData() + 18);
        const int winAscent = qFromBigEndian<quint16>(os2.constData() + 74);
        const int winDescent = qFromBigEndian<quint16>(os2.constData() + 76);
        const double px = font.pointSizeF() > 0 ? font.pointSizeF() * 96.0 / 72.0 : font.pixelSize();
        if (upm > 0)
            m.word = (winAscent + winDescent) * px / upm;
    }
    // Measure Qt's real pitch: two lines in a throwaway document.
    QTextDocument d;
    d.setDocumentMargin(0);
    d.setTextWidth(10);
    QTextCursor c(&d);
    QTextCharFormat cf;
    cf.setFont(font);
    c.insertText(QStringLiteral("x x x"), cf);
    d.documentLayout()->documentSize();
    const QTextLayout *l = d.begin().layout();
    if (l && l->lineCount() >= 2)
        m.qt = l->lineAt(1).y() - l->lineAt(0).y();
    cache.insert(key, m);
    return m;
}

QString lineKey(const QTextBlockFormat &bf)
{
    return QStringLiteral("%1|%2").arg(bf.lineHeightType()).arg(bf.lineHeight(), 0, 'f', 4);
}

} // namespace

QTextCharFormat dominantCharFormat(const QTextBlock &block)
{
    QTextCharFormat best = block.charFormat();
    double bestPt = best.fontPointSize() > 0 ? best.fontPointSize() : best.font().pointSizeF();
    bool any = false;
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        const QTextCharFormat cf = it.fragment().charFormat();
        if (cf.isImageFormat() || it.fragment().text().trimmed().isEmpty())
            continue;
        const double pt = cf.fontPointSize() > 0 ? cf.fontPointSize() : cf.font().pointSizeF();
        if (!any || pt > bestPt) {
            best = cf;
            bestPt = pt;
            any = true;
        }
    }
    return best;
}

void compensateLineHeight(QTextBlockFormat &bf, const QTextCharFormat &dominant)
{
    // Recover the docx value first: an earlier compensation may still be in place.
    int line = 240;
    if (bf.hasProperty(LineOrig) && bf.stringProperty(LineShown) == lineKey(bf)) {
        line = bf.stringProperty(LineOrig).section(QLatin1Char('|'), 0, 0).toInt();
    } else if (bf.lineHeightType() == QTextBlockFormat::ProportionalHeight) {
        line = int(qRound(bf.lineHeight() * 240.0 / 100.0));
    } else if (bf.lineHeightType() != QTextBlockFormat::SingleHeight) {
        bf.clearProperty(LineOrig);  // exact / at-least: already in px, nothing to fix
        bf.clearProperty(LineShown);
        return;
    }
    const LineMetrics m = lineMetrics(dominant.font());
    if (m.word <= 0 || m.qt <= 0)
        return;
    const double pct = 100.0 * (m.word * line / 240.0) / m.qt;
    bf.setLineHeight(pct, QTextBlockFormat::ProportionalHeight);
    bf.setProperty(LineOrig, QStringLiteral("%1|auto").arg(line));
    bf.setProperty(LineShown, lineKey(bf));
}

QString tabsKey(const QList<QTextOption::Tab> &tabs)
{
    QStringList parts;
    for (const QTextOption::Tab &t : tabs)
        parts << QStringLiteral("%1@%2").arg(int(t.type)).arg(t.position, 0, 'f', 3);
    return parts.join(QLatin1Char(';'));
}

QString encodeTabStops(const QList<TabStop> &tabs)
{
    QStringList parts;
    for (const TabStop &t : tabs)
        parts << QStringLiteral("%1@%2").arg(t.val).arg(t.pos);
    return parts.join(QLatin1Char(';'));
}

QList<TabStop> decodeTabStops(const QString &s)
{
    QList<TabStop> out;
    for (const QString &part : s.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const int at = part.indexOf(QLatin1Char('@'));
        if (at > 0)
            out.append(TabStop{part.left(at), part.mid(at + 1).toInt(), QString()});
    }
    return out;
}

QString normJc(const std::optional<QString> &jc)
{
    if (!jc)
        return QStringLiteral("left");
    const QString &v = *jc;
    if (v == QLatin1String("center"))
        return QStringLiteral("center");
    if (v == QLatin1String("right") || v == QLatin1String("end"))
        return QStringLiteral("right");
    if (v == QLatin1String("both") || v == QLatin1String("distribute") || v == QLatin1String("justify"))
        return QStringLiteral("both");
    return QStringLiteral("left");
}

QTextBlockFormat toBlockFormat(const ParaProps &p, bool isListItem)
{
    QTextBlockFormat bf;
    const QString jc = normJc(p.jc);
    if (jc == QLatin1String("center"))
        bf.setAlignment(Qt::AlignHCenter);
    else if (jc == QLatin1String("right"))
        bf.setAlignment(Qt::AlignRight);
    else if (jc == QLatin1String("both"))
        bf.setAlignment(Qt::AlignJustify);
    else
        bf.setAlignment(Qt::AlignLeft);

    bf.setTopMargin(twipsToPx(p.before.value_or(0)));
    bf.setBottomMargin(twipsToPx(p.after.value_or(0)));

    const QString rule = p.lineRule.value_or(QStringLiteral("auto"));
    const int line = p.line.value_or(240);
    if (rule == QLatin1String("exact"))
        bf.setLineHeight(twipsToPx(line), QTextBlockFormat::FixedHeight);
    else if (rule == QLatin1String("atLeast"))
        bf.setLineHeight(twipsToPx(line), QTextBlockFormat::MinimumHeight);
    else if (line != 240)
        bf.setLineHeight(line * 100.0 / 240.0, QTextBlockFormat::ProportionalHeight);
    else
        bf.setLineHeight(100, QTextBlockFormat::SingleHeight);

    bf.setLeftMargin(twipsToPx(p.left.value_or(0)));
    bf.setRightMargin(twipsToPx(p.right.value_or(0)));
    bf.setTextIndent(isListItem ? 0.0 : twipsToPx(p.firstLine.value_or(0)));

    bf.setPageBreakPolicy(p.pageBreakBefore.value_or(false) ? QTextFormat::PageBreak_AlwaysBefore
                                                            : QTextFormat::PageBreak_Auto);
    if (p.keepNext.value_or(false))
        bf.setProperty(KeepNext, true);
    if (p.contextualSpacing.value_or(false))
        bf.setProperty(ContextualSpacing, true);
    if (p.keepLines.value_or(false)) {
        bf.setProperty(KeepLines, true);
        bf.setNonBreakableLines(true);
    }

    static const int sideProps[4] = {BorderTop, BorderLeft, BorderBottom, BorderRight};
    for (int s = 0; s < 4; ++s)
        if (p.bdr[s] && !p.bdr[s]->isEmpty())
            bf.setProperty(sideProps[s], *p.bdr[s]);

    if (p.shd && !p.shd->isEmpty())
        bf.setBackground(QColor(QLatin1Char('#') + *p.shd));

    QList<QTextOption::Tab> tabs;
    for (const TabStop &t : p.tabs) {
        QTextOption::TabType type = QTextOption::LeftTab;
        QChar delim;
        if (t.val == QLatin1String("right"))
            type = QTextOption::RightTab;
        else if (t.val == QLatin1String("center"))
            type = QTextOption::CenterTab;
        else if (t.val == QLatin1String("decimal")) {
            type = QTextOption::DelimiterTab;
            delim = QLatin1Char('.');
        } else if (t.val != QLatin1String("left"))
            continue;  // num, bar, clear
        tabs.append(QTextOption::Tab(twipsToPx(t.pos), type, delim));
    }
    if (!tabs.isEmpty())
        bf.setTabPositions(tabs);
    return bf;
}

QTextCharFormat toCharFormat(const RunProps &r)
{
    QTextCharFormat cf;
    cf.setFontFamilies(QStringList{r.font.value_or(QString::fromLatin1(kDefaultFont))});
    cf.setFontPointSize(r.halfPts.value_or(kDefaultHalfPts) / 2.0);
    cf.setFontWeight(r.b.value_or(false) ? QFont::Bold : QFont::Normal);
    cf.setFontItalic(r.i.value_or(false));
    cf.setFontUnderline(r.u.value_or(false));
    cf.setFontStrikeOut(r.strike.value_or(false));
    const QString color = r.color.value_or(QStringLiteral("auto"));
    if (color != QLatin1String("auto"))
        cf.setForeground(QColor(QLatin1Char('#') + color));
    if (r.highlight && !r.highlight->isEmpty()) {
        const QColor hc = highlightColor(*r.highlight);
        if (hc.isValid()) {
            cf.setBackground(hc);
            cf.setProperty(Highlight, *r.highlight);
        }
    } else if (r.shd && !r.shd->isEmpty()) {
        cf.setBackground(QColor(QLatin1Char('#') + *r.shd));
    }
    const QString va = r.vertAlign.value_or(QStringLiteral("baseline"));
    if (va == QLatin1String("superscript"))
        cf.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
    else if (va == QLatin1String("subscript"))
        cf.setVerticalAlignment(QTextCharFormat::AlignSubScript);
    return cf;
}

ParaProps fromBlockFormat(const QTextBlockFormat &bf, qreal indentWidth)
{
    ParaProps p;
    const Qt::Alignment a = bf.alignment() & Qt::AlignHorizontal_Mask;
    if (a & Qt::AlignHCenter)
        p.jc = QStringLiteral("center");
    else if (a & Qt::AlignRight)
        p.jc = QStringLiteral("right");
    else if (a & Qt::AlignJustify)
        p.jc = QStringLiteral("both");
    else
        p.jc = QStringLiteral("left");

    p.before = pxToTwips(bf.topMargin());
    p.after = pxToTwips(bf.bottomMargin());
    switch (bf.lineHeightType()) {
    case QTextBlockFormat::ProportionalHeight:
        p.line = int(qRound(bf.lineHeight() * 240.0 / 100.0));
        p.lineRule = QStringLiteral("auto");
        break;
    case QTextBlockFormat::FixedHeight:
        p.line = pxToTwips(bf.lineHeight());
        p.lineRule = QStringLiteral("exact");
        break;
    case QTextBlockFormat::MinimumHeight:
        p.line = pxToTwips(bf.lineHeight());
        p.lineRule = QStringLiteral("atLeast");
        break;
    default:
        p.line = 240;
        p.lineRule = QStringLiteral("auto");
        break;
    }
    if (bf.hasProperty(LineOrig) && bf.stringProperty(LineShown) == lineKey(bf)) {
        const QString o = bf.stringProperty(LineOrig);
        p.line = o.section(QLatin1Char('|'), 0, 0).toInt();
        p.lineRule = o.section(QLatin1Char('|'), 1, 1);
    }
    p.left = pxToTwips(bf.leftMargin() + bf.indent() * indentWidth);
    p.right = pxToTwips(bf.rightMargin());
    p.firstLine = pxToTwips(bf.textIndent());
    p.pageBreakBefore = bool(bf.pageBreakPolicy() & QTextFormat::PageBreak_AlwaysBefore);
    p.keepNext = bf.boolProperty(KeepNext);
    p.keepLines = bf.boolProperty(KeepLines) || bf.nonBreakableLines();
    p.contextualSpacing = bf.boolProperty(ContextualSpacing);
    if (bf.hasProperty(SpacingOrig)
        && bf.stringProperty(SpacingShown)
               == QStringLiteral("%1,%2").arg(bf.topMargin(), 0, 'f', 3).arg(bf.bottomMargin(), 0, 'f', 3)) {
        const QStringList o = bf.stringProperty(SpacingOrig).split(QLatin1Char(','));
        if (o.size() == 2) {
            p.before = o[0].toInt();
            p.after = o[1].toInt();
        }
    }

    static const int sideProps[4] = {BorderTop, BorderLeft, BorderBottom, BorderRight};
    for (int s = 0; s < 4; ++s)
        p.bdr[s] = bf.stringProperty(sideProps[s]);

    p.shd = bf.background().style() == Qt::NoBrush ? QString() : hexOf(bf.background().color());

    if (bf.hasProperty(TabsOrig) && bf.stringProperty(TabsShown) == tabsKey(bf.tabPositions())) {
        p.tabs = decodeTabStops(bf.stringProperty(TabsOrig));
        return p;
    }
    for (const QTextOption::Tab &t : bf.tabPositions()) {
        TabStop ts;
        ts.pos = pxToTwips(t.position);
        switch (t.type) {
        case QTextOption::RightTab: ts.val = QStringLiteral("right"); break;
        case QTextOption::CenterTab: ts.val = QStringLiteral("center"); break;
        case QTextOption::DelimiterTab: ts.val = QStringLiteral("decimal"); break;
        default: ts.val = QStringLiteral("left"); break;
        }
        p.tabs.append(ts);
    }
    return p;
}

RunProps fromCharFormat(const QTextCharFormat &cf)
{
    RunProps r;
    const QStringList fams = cf.fontFamilies().toStringList();
    r.font = fams.isEmpty() ? cf.font().family() : fams.first();
    const double pt = cf.fontPointSize() > 0 ? cf.fontPointSize() : cf.font().pointSizeF();
    r.halfPts = pt > 0 ? int(qRound(pt * 2.0)) : kDefaultHalfPts;
    r.b = cf.fontWeight() >= QFont::DemiBold;
    r.i = cf.fontItalic();
    r.u = cf.fontUnderline();
    r.strike = cf.fontStrikeOut();
    r.color = cf.foreground().style() == Qt::NoBrush ? QStringLiteral("auto") : hexOf(cf.foreground().color());
    r.highlight = QString();
    r.shd = QString();
    if (cf.background().style() != Qt::NoBrush) {
        const QString hl = cf.stringProperty(Highlight);
        const QColor bg = cf.background().color();
        if (!hl.isEmpty() && highlightColor(hl) == bg)
            r.highlight = hl;
        else
            r.shd = hexOf(bg);
    }
    switch (cf.verticalAlignment()) {
    case QTextCharFormat::AlignSuperScript: r.vertAlign = QStringLiteral("superscript"); break;
    case QTextCharFormat::AlignSubScript: r.vertAlign = QStringLiteral("subscript"); break;
    default: r.vertAlign = QStringLiteral("baseline"); break;
    }
    return r;
}

} // namespace docx
