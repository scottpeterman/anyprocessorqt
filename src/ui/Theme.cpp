#include "ui/Theme.h"

#include <QApplication>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>

namespace theme {

namespace {
QString g_systemStyle;
QPalette g_systemPalette;
bool g_captured = false;

QPalette darkPalette()
{
    QPalette p;
    const QColor window(0x35, 0x35, 0x35), base(0x26, 0x26, 0x27), text(0xE8, 0xE8, 0xE8), dim(0x80, 0x80, 0x80);
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, QColor(0x2E, 0x2E, 0x2F));
    p.setColor(QPalette::ToolTipBase, window);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, dim);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, window);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, QColor(0xFF, 0x6B, 0x6B));
    p.setColor(QPalette::Light, QColor(0x4A, 0x4A, 0x4A));
    p.setColor(QPalette::Midlight, QColor(0x40, 0x40, 0x40));
    p.setColor(QPalette::Mid, QColor(0x2A, 0x2A, 0x2A));
    p.setColor(QPalette::Dark, QColor(0x20, 0x20, 0x20));
    p.setColor(QPalette::Shadow, QColor(0x10, 0x10, 0x10));
    p.setColor(QPalette::Highlight, QColor(0x2F, 0x65, 0xCA));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, QColor(0x6C, 0xB4, 0xFF));
    p.setColor(QPalette::LinkVisited, QColor(0xB0, 0x8C, 0xFF));
    for (auto role : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, dim);
    p.setColor(QPalette::Disabled, QPalette::Highlight, QColor(0x50, 0x50, 0x50));
    return p;
}
} // namespace

void captureSystem()
{
    if (g_captured)
        return;
    g_systemStyle = QApplication::style()->name();
    g_systemPalette = QApplication::palette();
    g_captured = true;
}

void apply(Theme t)
{
    captureSystem();
    if (t == Theme::System) {
        if (QApplication::style()->name().compare(g_systemStyle, Qt::CaseInsensitive) != 0)
            if (QStyle *s = QStyleFactory::create(g_systemStyle))
                QApplication::setStyle(s);
        QApplication::setPalette(g_systemPalette);
        return;
    }
    if (QApplication::style()->name().compare(QLatin1String("fusion"), Qt::CaseInsensitive) != 0)
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QApplication::setPalette(t == Theme::Dark ? darkPalette() : QApplication::style()->standardPalette());
}

Theme themeFromString(const QString &s)
{
    if (s == QLatin1String("light"))
        return Theme::Light;
    if (s == QLatin1String("dark"))
        return Theme::Dark;
    return Theme::System;
}

QString toString(Theme t)
{
    switch (t) {
    case Theme::Light: return QStringLiteral("light");
    case Theme::Dark: return QStringLiteral("dark");
    default: return QStringLiteral("system");
    }
}

Paper paperFromString(const QString &s)
{
    return s == QLatin1String("dark") ? Paper::Dark : Paper::White;
}

QString toString(Paper p)
{
    return p == Paper::Dark ? QStringLiteral("dark") : QStringLiteral("white");
}

bool appIsDark()
{
    const QPalette p = QApplication::palette();
    return p.color(QPalette::Window).lightness() < p.color(QPalette::WindowText).lightness();
}

QColor deskColor()
{
    return appIsDark() ? QColor(0x3C, 0x3C, 0x3E) : QColor(0xE6, 0xE6, 0xE6);
}

QColor paperColor(Paper p)
{
    return p == Paper::Dark ? QColor(0x1E, 0x1E, 0x20) : QColor(Qt::white);
}

QColor inkColor(Paper p)
{
    return p == Paper::Dark ? QColor(0xDA, 0xDA, 0xDA) : QColor(Qt::black);
}

} // namespace theme
