#include "MainWindow.h"
#include "Theme.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("claude-box");
    QApplication::setOrganizationName("claude-box");
    QApplication::setApplicationVersion(CLAUDE_BOX_VERSION);
    // Ties the window back to claude-box-gui.desktop. On Wayland there is
    // no WM_CLASS for the shell to match on, so without this GNOME shows a
    // generic icon in the dash and alt-tab even with the entry installed.
    QGuiApplication::setDesktopFileName("claude-box-gui");
    // GNOME/Wayland takes the window's icon from the desktop entry above,
    // but X11 sessions and other WMs want it set on the window itself.
    // Looked up by name out of the XDG icon theme (desktop-install drops
    // it into ~/.local/share/icons/hicolor) rather than embedded, so the
    // build stays free of .qrc plumbing; if it isn't installed the icon
    // is simply null and the desktop entry still carries the artwork.
    QIcon appIcon = QIcon::fromTheme(QStringLiteral("claude-box"));
    if (appIcon.isNull()) {
        // fromTheme() only resolves once a platform theme is in play, and
        // there isn't always one (minimal sessions, offscreen, plain WMs).
        // The install path is known, so fall back to reading the file.
        const QString iconFile = QDir::homePath()
            + QStringLiteral("/.local/share/icons/hicolor/scalable/apps/claude-box.svg");
        if (QFileInfo::exists(iconFile))
            appIcon = QIcon(iconFile);
    }
    if (!appIcon.isNull())
        QApplication::setWindowIcon(appIcon);

    // Must happen before any widget is constructed: Theme swaps the style
    // and the whole palette, and widgets built beforehand would keep the
    // defaults they were created with.
    Theme::apply(app);

    MainWindow window;
    window.show();

    return app.exec();
}
