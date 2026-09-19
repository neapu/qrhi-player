#include "Stats.h"

#include <QDebug>

namespace view {
Stats::Stats(QObject* parent)
{
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &Stats::onTimeout);
}

void Stats::setControllerStatisticsCallback(ControllerStatisticsCallback callback)
{
    m_controllerStatisticsCallback = callback;
}

void Stats::start()
{
    if (m_timer) {
        m_timer->start(1000); // Adjust the interval as needed
    }
}

void Stats::stop()
{
    if (m_timer) {
        m_timer->stop();
    }
    m_frameCount = 0;
    m_lastFrameCount = 0;
}

void Stats::onTimeout()
{
    auto framesThisSecond = m_frameCount - m_lastFrameCount;
    m_lastFrameCount = m_frameCount;

    auto stats = m_controllerStatisticsCallback ? m_controllerStatisticsCallback() : controller::StatisticsData{};
    auto decodedFramesThisSecond = stats.video.decodedFrames - m_lastDecodedFrames;
    m_lastDecodedFrames = stats.video.decodedFrames;

    qDebug() << "[Renderer[fps:" << framesThisSecond << "][total:" << m_frameCount << "]]"
             << "[Controller[dropped:" << stats.video.droppedFrames << "][decoded:" << stats.video.decodedFrames << "][decodedThisSecond:" << decodedFramesThisSecond << "]]";
}

void Stats::renderFrame()
{
    ++m_frameCount;
}

} // namespace view