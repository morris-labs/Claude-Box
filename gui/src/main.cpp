#include "MainWindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("claude-box");
    QApplication::setOrganizationName("claude-box");
    QApplication::setApplicationVersion(CLAUDE_BOX_VERSION);

    MainWindow window;
    window.show();

    return app.exec();
}
