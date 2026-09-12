#include "MainWindow.h"

#include "LogManager.h"

namespace view {

MainWindow::MainWindow(const QString& videoFile, QWidget* parent)
    : QMainWindow(parent)
{
    if (!videoFile.isEmpty()) {
        openVideo(videoFile);
    }
}

MainWindow::~MainWindow() = default;

void MainWindow::openVideo(const QString& videoFile)
{
    using namespace controller;

    IController::Params params;
    params.url = videoFile.toStdString();
    params.logCallback = LogManager::instance().controllerLogCallback();
    m_controller = IController::create(params);
}

} // namespace view
