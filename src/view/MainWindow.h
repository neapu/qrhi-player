#pragma once
#include <QMainWindow>
#include "Controller.h"
#include "video_renderer/VideoRenderer.h"

namespace view {
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const QString& commandInputFile, QWidget* parent = nullptr);
    ~MainWindow();

private:
    void createWidgets();

private slots:
    void onVideoRendererInitialized();

private:
    Controller* m_controller{nullptr};
    VideoRenderer* m_videoRenderer{nullptr};
    QString m_commandInputFile;
};

} // namespace view