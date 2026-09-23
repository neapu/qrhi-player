#pragma once
#include <QMainWindow>
#include "Controller.h"

namespace view {
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const QString& commandInputFile, QWidget* parent = nullptr);
    ~MainWindow();


private:
    Controller* m_controller{nullptr};
};

} // namespace view