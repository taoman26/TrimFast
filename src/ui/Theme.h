#pragma once

#include <QColor>
#include <QString>

class QApplication;

// The look of the application: a light and a dark palette, applied to the widgets through the
// palette and a style sheet, and read by the custom-painted timeline and preview.
namespace Theme {

enum class Mode { System, Light, Dark };

// Colours used by the custom-painted widgets (those of the scheme in use).
struct Colors {
    QColor window;
    QColor surface;
    QColor surfaceLow;
    QColor border;
    QColor text;
    QColor textDim;
    QColor accent;
    QColor accentFill;
    QColor playhead;
    QColor keyframe;
};

// The stored name of a mode ("system", "light", "dark") and back; anything unknown is System.
QString modeName(Mode mode);
Mode modeFromName(const QString &name);

// Applies the mode to the whole application (System follows the desktop's colour scheme).
void apply(QApplication &app, Mode mode);

const Colors &colors();

} // namespace Theme
