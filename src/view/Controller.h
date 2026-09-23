#pragma once
#include <QObject>
#include <QString>
#include "media/IMediaSource.h"

namespace view {
class Controller : public QObject {
    Q_OBJECT
public:
    explicit Controller(QObject* parent = nullptr);

    void openFile(const QString& filePath);
    void closeFile();

private:
    media::MediaSourcePtr m_mediaSource{nullptr};
};

} // namespace view