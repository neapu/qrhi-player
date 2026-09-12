#include <QApplication>

#include "LogManager.h"
#include "MainWindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    view::LogManager::instance(); // 尽早初始化日志系统并接管 Qt 消息处理

    QString videoFile = argc > 1 ? QString(argv[1]) : QString();
    view::MainWindow mainWindow(videoFile);
    mainWindow.show();

    return app.exec();
}
