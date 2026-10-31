#pragma once

#include "docx/Props.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QPair>
#include <QString>

namespace docx {

struct Style {
    QString id;
    QString name;
    QString type;     // paragraph / character / table / numbering
    QString basedOn;
    QString next;
    bool isDefault = false;
    bool qFormat = false;
    ParaProps p;
    RunProps r;
};

class Styles {
public:
    void parse(const QByteArray &stylesXml);

    // Paragraph properties of a paragraph style, fully resolved (defaults + basedOn chain).
    ParaProps resolvedPara(const QString &styleId) const;
    // Run properties a paragraph style gives its text (defaults + basedOn chain).
    RunProps resolvedParaRun(const QString &styleId) const;
    // Run properties contributed by a character style chain (no defaults).
    RunProps charStyleRun(const QString &styleId) const;

    QString defaultParagraphStyle() const { return m_defaultPara; }
    const Style *style(const QString &id) const;
    QString idForName(const QString &name) const;

    // Paragraph styles worth offering in the UI: quick-format ones, in a sensible order.
    QList<QPair<QString, QString>> uiParagraphStyles() const;

private:
    QList<const Style *> chain(const QString &id) const;  // root first

    QHash<QString, Style> m_styles;
    QList<QString> m_order;
    RunProps m_defRun;
    ParaProps m_defPara;
    QString m_defaultPara = QStringLiteral("Normal");
};

} // namespace docx
