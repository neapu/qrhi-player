#pragma once
#include <QMainWindow>

namespace view {
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QString &commandInputFile, QWidget *parent = nullptr);
    ~MainWindow();

private:
    void openVideo(const QString &videoFile);
    void closeVideo();

    void createMenuBar();
};

} // namespace view