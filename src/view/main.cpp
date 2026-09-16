#include <QApplication>
#include "LogManager.h"
#include "MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    view::LogManager::instance(); // 先初始化
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext& context, const QString& message) {
        view::LogManager::instance().logQtMessage(type, context, message);
    });

    QString commandInputFile = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    view::MainWindow mainWindow(commandInputFile);
    mainWindow.show();

    int ret = app.exec();
    qInstallMessageHandler(nullptr); // 恢复默认的Qt消息处理器
    view::LogManager::shutdown();
    return ret;
}