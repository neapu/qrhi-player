#pragma once
#include <QLabel>

namespace view {
class PlaybackLabel : public QLabel {
    Q_OBJECT

public:
    explicit PlaybackLabel(QWidget* parent = nullptr);

    void setDuration(qint64 duration);
    void setPosition(qint64 position);

private:
    void updateText();

private:
    qint64 m_position{0};
    QString m_durationText;
};

} // namespace view