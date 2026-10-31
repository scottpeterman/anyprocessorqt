#pragma once

#include "docx/Props.h"

#include <QByteArray>
#include <QHash>
#include <QString>

namespace docx {

struct NumLevel {
    QString fmt = QStringLiteral("bullet"); // bullet, decimal, lowerLetter, upperLetter, lowerRoman, upperRoman, none
    QString text;                           // lvlText
    int start = 1;
    ParaProps p;                            // indentation of the level
    RunProps r;                             // marker formatting
};

class Numbering {
public:
    void parse(const QByteArray &numberingXml);
    const NumLevel *level(int numId, int ilvl) const;
    bool isRealList(int numId, int ilvl) const;  // exists and fmt != none
    int maxAbstractId() const { return m_maxAbs; }
    int maxNumId() const { return m_maxNum; }

private:
    QHash<int, QHash<int, NumLevel>> m_abstract;
    QHash<int, int> m_numToAbs;
    QHash<int, QHash<int, int>> m_startOverride;
    int m_maxAbs = 0;
    int m_maxNum = 0;
};

} // namespace docx
