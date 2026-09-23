#include <QApplication>
#include "MainWindow.h"
#include "LogManager.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("qrhi_player"));
    view::LogManager::instance().initialize();

    QString commandInputFile = argc > 1 ? QString::fromUtf8(argv[1]) : QString();

    view::MainWindow mainWindow(commandInputFile);
    mainWindow.show();
    
    auto ret = app.exec();
    view::LogManager::instance().shutdown();
    return ret;
}