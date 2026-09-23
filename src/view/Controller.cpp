#include "Controller.h"
#include "LogManager.h"
#include <QDebug>

namespace view {
Controller::Controller(QObject* parent)
    : QObject(parent)
{
}

void Controller::openFile(const QString& filePath)
{
    media::IMediaSource::Params params{};
    params.source = filePath.toStdString();
    params.logDir = view::LogManager::instance().logDir().toStdString();
    params.requiredPixelFormats = {AV_PIX_FMT_YUV420P};
    params.requiredSampleFormats = {AV_SAMPLE_FMT_FLTP};
    m_mediaSource = media::IMediaSource::create(params);
    if (!m_mediaSource) {
        qCritical() << "Failed to open media source:" << filePath;
    }
}

void Controller::closeFile()
{
    m_mediaSource.reset();
}

} // namespace view