#include "MainWindow.h"
#include <QDebug>

namespace view {

MainWindow::MainWindow(const QString& commandInputFile, QWidget* parent)
    : QMainWindow(parent)
{
    qDebug() << "Command input file:" << commandInputFile;

    m_controller = new Controller(this);

    if (!commandInputFile.isEmpty()) {
        m_controller->openFile(commandInputFile);
    }
}

MainWindow::~MainWindow()
{
    m_controller->closeFile();
}

} // namespace view