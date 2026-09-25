#include "MainWindow.h"
#include <QDebug>
#include <QVBoxLayout>

namespace view {

MainWindow::MainWindow(const QString& commandInputFile, QWidget* parent)
    : QMainWindow(parent)
{
    m_commandInputFile = commandInputFile;
    resize(800, 600);

    m_controller = new Controller(this);
    createWidgets();

    connect(m_videoRenderer, &VideoRenderer::initialized, this, &MainWindow::onVideoRendererInitialized);
}

MainWindow::~MainWindow()
{
    m_controller->closeFile();
}

void MainWindow::createWidgets()
{
    auto* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    auto* layout = new QVBoxLayout(centralWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto frameCallback = [this]() {
        return m_controller->nextVideoFrame();
    };
    m_videoRenderer = new VideoRenderer(frameCallback, centralWidget);
    
    layout->addWidget(m_videoRenderer);
}

void MainWindow::onVideoRendererInitialized()
{
    if (!m_commandInputFile.isEmpty()) {
        m_controller->openFile(m_commandInputFile);
        m_commandInputFile = {};
    }
}

} // namespace view