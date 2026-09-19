#pragma once
#include <QObject>
#include <QTimer>
#include "controller/Statistics.h"

namespace view {
using ControllerStatisticsCallback = std::function<controller::StatisticsData()>;
class Stats : public QObject {
    Q_OBJECT
public:
    Stats(QObject* parent = nullptr);

    void setControllerStatisticsCallback(ControllerStatisticsCallback callback);

    void start();
    void stop();

    void renderFrame();

private slots:
    void onTimeout();

private:
    QTimer* m_timer{nullptr};

    int64_t m_frameCount{0};
    int64_t m_lastFrameCount{0};
    ControllerStatisticsCallback m_controllerStatisticsCallback{nullptr};

    uint64_t m_lastDecodedFrames{0};
};
} // namespace view