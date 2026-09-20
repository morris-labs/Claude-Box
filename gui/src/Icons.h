#pragma once

#include <QIcon>

class QColor;

// Toolbar/menu icons, drawn with QPainter rather than loaded from a theme
// or a resource file. Two reasons: the app ships a fixed dark palette (see
// Theme), so system icon themes -- which may be light, may be missing, or
// may be styled nothing like each other -- would look out of place; and
// drawing them keeps the build free of .qrc plumbing and icon-theme
// runtime dependencies.
//
// Each icon is rendered at several sizes so high-DPI displays pick a crisp
// one rather than upscaling a 16px bitmap.
namespace Icons {

QIcon newBox();
QIcon editBox();
QIcon open();
QIcon closeBox();
QIcon removeBox();
QIcon purge();
QIcon fork();
QIcon refresh();
QIcon terminal();

// Filled circle used as the table's status indicator.
QIcon statusDot(const QColor &color);

} // namespace Icons
