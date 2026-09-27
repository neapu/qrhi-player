#include "PlayOrPauseButton.h"

namespace view {

PlayOrPauseButton::PlayOrPauseButton(QWidget* parent)
    : QPushButton(parent)
{
    m_playIcon = QIcon(":/icons/play.svg");
    m_pauseIcon = QIcon(":/icons/pause.svg");
    setIcon(m_playIcon);
}

void PlayOrPauseButton::setPaused(bool paused)
{
    m_paused = paused;
    setIcon(m_paused ? m_playIcon : m_pauseIcon);
}

} // namespace view