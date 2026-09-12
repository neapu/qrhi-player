#pragma once
#include <QMainWindow>
#include "controller/Controller.h"

namespace view {
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QString& videoFile, QWidget* parent = nullptr);
    ~MainWindow();

private:
    void openVideo(const QString& videoFile);

private:
    controller::ControllerPtr m_controller{nullptr};
};
} // namespace view
