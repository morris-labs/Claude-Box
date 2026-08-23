#include "MainWindow.h"
#include "Theme.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("claude-box");
    QApplication::setOrganizationName("claude-box");
    QApplication::setApplicationVersion(CLAUDE_BOX_VERSION);

    // Must happen before any widget is constructed: Theme swaps the style
    // and the whole palette, and widgets built beforehand would keep the
    // defaults they were created with.
    Theme::apply(app);

    MainWindow window;
    window.show();

    return app.exec();
}
