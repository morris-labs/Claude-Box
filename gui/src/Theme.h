#pragma once

#include <QColor>

class QApplication;

// The app's dark palette, applied globally in main(). Deliberately a
// hand-built palette + stylesheet rather than whatever the desktop theme
// provides: the embedded terminals are unavoidably dark, and a light
// system theme around them looked like two different programs stapled
// together.
//
// Widgets that paint themselves (TerminalWidget, the status dots in
// BoxTableModel) read these accessors instead of hardcoding colors, so
// there's one place to retune the scheme.
namespace Theme {

QColor windowBg();
QColor panelBg();
QColor baseBg();
QColor border();
QColor text();
QColor dimText();
QColor accent();

// Status colors, shared by the table's dots and the details panel.
QColor running();
QColor stopped();
QColor known();

QColor terminalBg();
QColor terminalFg();

void apply(QApplication &app);

} // namespace Theme
