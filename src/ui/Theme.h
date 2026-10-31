#pragma once

#include <QColor>
#include <QString>

// App chrome theme and paper colour.
//
// Qt 6.4 (the ChromeOS/Ubuntu 24.04 floor) has no QStyleHints::colorScheme, so
// Light and Dark are explicit Fusion palettes; System keeps whatever style and
// palette the platform gave the app at startup.
namespace theme {

enum class Theme { System, Light, Dark };
enum class Paper { White, Dark };

// Remember the platform style and palette. Call once, after QApplication exists
// and before apply().
void captureSystem();
void apply(Theme t);

Theme themeFromString(const QString &s);  // unknown -> System
QString toString(Theme t);
Paper paperFromString(const QString &s);  // unknown -> White
QString toString(Paper p);

bool appIsDark();      // current application palette is dark
QColor deskColor();    // background around the page, follows the app theme
QColor paperColor(Paper p);
QColor inkColor(Paper p);

} // namespace theme
