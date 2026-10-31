#pragma once

#include <QList>
#include <QString>

#include <optional>

namespace docx {

struct XElem;

// Run properties (w:rPr) in docx units. Unset = inherit.
struct RunProps {
    std::optional<QString> font;      // w:rFonts/@ascii (or hAnsi)
    std::optional<int> halfPts;       // w:sz
    std::optional<bool> b, i, u, strike;
    std::optional<QString> color;     // "RRGGBB" or "auto"
    std::optional<QString> highlight; // named highlight color
    std::optional<QString> shd;       // fill "RRGGBB"
    std::optional<QString> vertAlign; // superscript / subscript / baseline

    void merge(const RunProps &o);
    static RunProps parse(const XElem *rPr);
};

struct TabStop {
    QString val;  // left/right/center/decimal/clear/num/bar
    int pos = 0;  // twips
    QString leader;
    bool operator==(const TabStop &o) const { return val == o.val && pos == o.pos && leader == o.leader; }
};

// Paragraph properties (w:pPr) in docx units. Unset = inherit.
struct ParaProps {
    std::optional<QString> jc;
    std::optional<int> before, after, line;
    std::optional<QString> lineRule;  // auto / exact / atLeast
    std::optional<int> left, right, firstLine;  // firstLine < 0 means hanging
    std::optional<bool> keepNext, keepLines, pageBreakBefore, contextualSpacing;
    std::optional<int> numId, ilvl;
    std::optional<QString> bdr[4];    // top, left, bottom, right: "val|sz|space|color" ("" = none)
    std::optional<QString> shd;
    QList<TabStop> tabs;              // accumulated (clear entries remove)
    std::optional<int> outlineLvl;

    void merge(const ParaProps &o);
    static ParaProps parse(const XElem *pPr);
};

bool parseOnOff(const XElem *e);  // <w:b/>, <w:b w:val="0"/>, ...

} // namespace docx
