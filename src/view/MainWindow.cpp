#include "MainWindow.h"
#include <QMenuBar>
#include <QFileDialog>

namespace view {
MainWindow::MainWindow(const QString &commandInputFile, QWidget* parent)
    : QMainWindow(parent)
{
    if (!commandInputFile.isEmpty()) {
        openVideo(commandInputFile);
    }

    setWindowTitle("QRHI Player");
    resize(800, 600);

    createMenuBar();
}

MainWindow::~MainWindow()
{
}

void MainWindow::openVideo(const QString &videoFile)
{
    // Implement the logic to open the video file here
}

void MainWindow::closeVideo()
{
    // Implement the logic to close the video file here
}

void MainWindow::createMenuBar()
{
    auto* menu = this->menuBar()->addMenu(tr("File(&F)"));
    auto* openAction = menu->addAction(tr("Open(&O)"));
    connect(openAction, &QAction::triggered, this, [this]() {
        QString videoFile = QFileDialog::getOpenFileName(
            this, tr("Open Video"), QString(), tr("Video Files (*.mp4 *.avi *.mkv)"));
        if (!videoFile.isEmpty()) {
            closeVideo();
            openVideo(videoFile);
        }
    });

    auto* exitAction = menu->addAction(tr("Exit(&E)"));
    connect(exitAction, &QAction::triggered, this, [this]() {
        close();
    });
}

} // namespace view