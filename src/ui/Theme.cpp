#include "ui/Theme.h"

#include <QApplication>
#include <QObject>
#include <QPalette>
#include <QStyleFactory>
#include <QStyleHints>
#include <QWidget>

namespace Theme {

namespace {

const Colors kDark{QColor(30, 31, 36),    QColor(40, 42, 49),    QColor(24, 25, 29),
                   QColor(62, 65, 75),    QColor(230, 232, 238), QColor(142, 147, 160),
                   QColor(79, 140, 255),  QColor(79, 140, 255, 70), QColor(255, 91, 91),
                   QColor(95, 99, 112)};

const Colors kLight{QColor(244, 245, 248), QColor(232, 234, 240), QColor(255, 255, 255),
                    QColor(208, 212, 222), QColor(30, 33, 41),    QColor(106, 112, 128),
                    QColor(36, 99, 224),   QColor(36, 99, 224, 55), QColor(217, 48, 48),
                    QColor(170, 175, 190)};

const Colors *g_colors = &kDark;
Mode g_mode = Mode::System;
bool g_followConnected = false;

bool useDark(Mode mode)
{
    if (mode == Mode::Dark)
        return true;
    if (mode == Mode::Light)
        return false;
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

QString styleSheet(const Colors &c, bool dark)
{
    // Text on the accent-coloured default button is white in both schemes.
    const QString hover = dark ? QStringLiteral("#353945") : QStringLiteral("#dde0ea");
    const QString pressed = dark ? QStringLiteral("#262830") : QStringLiteral("#d0d4e0");
    const QString accentHover = dark ? QStringLiteral("#6a9fff") : QStringLiteral("#4a80ea");
    const QString accentPressed = dark ? QStringLiteral("#3d78e6") : QStringLiteral("#1b52c0");
    const QString disabledBg = dark ? QStringLiteral("#25262c") : QStringLiteral("#eceef3");
    const QString disabledAccent = dark ? QStringLiteral("#2f3a52") : QStringLiteral("#b8c6e4");
    const QString menuBar = dark ? QStringLiteral("#1e1f24") : QStringLiteral("#ffffff");
    const QString menuHover = dark ? QStringLiteral("#2c2f38") : QStringLiteral("#e6e8f0");
    const QString disabledText = dark ? QStringLiteral("#646874") : QStringLiteral("#a3a8b6");

    return QStringLiteral(R"(
QMenuBar { background: %1; padding: 2px 4px; }
QMenuBar::item { padding: 5px 10px; border-radius: 4px; background: transparent; }
QMenuBar::item:selected { background: %2; }
QMenu { background: %3; border: 1px solid %4; padding: 4px; }
QMenu::item { padding: 6px 24px 6px 16px; border-radius: 4px; }
QMenu::item:selected { background: %5; color: white; }
QMenu::item:disabled { color: %6; }
QMenu::separator { height: 1px; background: %4; margin: 4px 8px; }
QStatusBar { background: %1; color: %7; }
QStatusBar::item { border: none; }
QStatusBar QLabel { color: %7; padding: 0 6px; }
QToolTip { background: %3; color: %8; border: 1px solid %4; padding: 4px; }
QPushButton {
    background: %3; border: 1px solid %4; border-radius: 6px;
    padding: 8px 16px; min-height: 18px;
}
QPushButton:hover { background: %9; }
QPushButton:pressed { background: %10; }
QPushButton:focus { border-color: %5; }
QPushButton:disabled { background: %11; color: %6; }
QPushButton:default { background: %5; border-color: %5; color: white; font-weight: bold; }
QPushButton:default:hover { background: %12; }
QPushButton:default:pressed { background: %13; }
QPushButton:default:disabled { background: %14; border-color: %14; color: %6; }
QLineEdit {
    background: %15; border: 1px solid %4; border-radius: 6px;
    padding: 5px 8px; selection-background-color: %5;
}
QLineEdit:focus { border-color: %5; }
QDialog, QMessageBox { background: %16; }
QLabel#dim { color: %7; }
QLabel#accent { color: %5; }
QLabel#position { color: %8; font-size: 15pt; }
)")
        .arg(menuBar, menuHover, c.surface.name(), c.border.name(), c.accent.name(), disabledText,
             c.textDim.name(), c.text.name(), hover, pressed, disabledBg, accentHover)
        .arg(accentPressed, disabledAccent, c.surfaceLow.name(), c.window.name());
}

} // namespace

QString modeName(Mode mode)
{
    switch (mode) {
    case Mode::Light:
        return QStringLiteral("light");
    case Mode::Dark:
        return QStringLiteral("dark");
    case Mode::System:
        break;
    }
    return QStringLiteral("system");
}

Mode modeFromName(const QString &name)
{
    if (name == QLatin1String("light"))
        return Mode::Light;
    if (name == QLatin1String("dark"))
        return Mode::Dark;
    return Mode::System;
}

const Colors &colors()
{
    return *g_colors;
}

void apply(QApplication &app, Mode mode)
{
    g_mode = mode;
    if (!g_followConnected) {
        g_followConnected = true;
        QObject::connect(app.styleHints(), &QStyleHints::colorSchemeChanged, &app, [&app] {
            if (g_mode == Mode::System)
                apply(app, Mode::System);
        });
    }

    static bool styled = false;
    if (!styled) {
        styled = true;
        if (QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion")))
            app.setStyle(fusion);
    }

    const bool dark = useDark(mode);
    const Colors &c = dark ? kDark : kLight;
    g_colors = &c;

    QPalette p;
    p.setColor(QPalette::Window, c.window);
    p.setColor(QPalette::WindowText, c.text);
    p.setColor(QPalette::Base, c.surfaceLow);
    p.setColor(QPalette::AlternateBase, c.surface);
    p.setColor(QPalette::Text, c.text);
    p.setColor(QPalette::Button, c.surface);
    p.setColor(QPalette::ButtonText, c.text);
    p.setColor(QPalette::ToolTipBase, c.surface);
    p.setColor(QPalette::ToolTipText, c.text);
    p.setColor(QPalette::Highlight, c.accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::PlaceholderText, c.textDim);
    p.setColor(QPalette::Link, c.accent);
    p.setColor(QPalette::Disabled, QPalette::WindowText, c.textDim);
    p.setColor(QPalette::Disabled, QPalette::Text, c.textDim);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, c.textDim);
    app.setPalette(p);
    app.setStyleSheet(styleSheet(c, dark));

    // The custom-painted widgets read the colours when they paint.
    for (QWidget *w : app.allWidgets())
        w->update();
}

} // namespace Theme
