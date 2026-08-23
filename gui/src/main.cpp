#include "MainWindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("claude-box");
    QApplication::setOrganizationName("claude-box");

    MainWindow window;
    window.show();

    return app.exec();
}
