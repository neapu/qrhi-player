#include "PlaybackLabel.h"

namespace view {

PlaybackLabel::PlaybackLabel(QWidget* parent)
    : QLabel(parent)
{
    setText("00:00 / 00:00");
}

void PlaybackLabel::setDuration(qint64 duration)
{
    // 转换为秒
    auto secondsDuration = duration / 1000000;
    // 转换为 分:秒 格式
    m_durationText = QString("%1:%2")
                        .arg(secondsDuration / 60)
                        .arg(secondsDuration % 60, 2, 10, QChar('0'));
    updateText();
}

void PlaybackLabel::setPosition(qint64 position)
{
    m_position = position;
    updateText();
}

void PlaybackLabel::updateText()
{
    // 转换为秒
    auto secondsPosition = m_position / 1000000;
    // 转换为 分:秒 格式
    auto minutesPosition = secondsPosition / 60;
    auto secondsOnlyPosition = secondsPosition % 60;

    setText(QString("%1:%2 / %3").arg(minutesPosition, 2, 10, QChar('0')).arg(secondsOnlyPosition, 2, 10, QChar('0')).arg(m_durationText));
}

} // namespace view