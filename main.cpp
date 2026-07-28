#include "MainWindow.h"

#include <QApplication>
#include <QFont>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QApplication::setFont(QFont("Segoe UI", 11));

    MainWindow window;
    window.showMaximized();

    return app.exec();
}