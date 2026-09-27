#pragma once
#include <QPushButton>

namespace view {
class PlayOrPauseButton : public QPushButton {
    Q_OBJECT
public:
    explicit PlayOrPauseButton(QWidget* parent = nullptr);

    void setPaused(bool paused);

private:
    bool m_paused{false};
    QIcon m_playIcon{};
    QIcon m_pauseIcon{};
};

} // namespace view