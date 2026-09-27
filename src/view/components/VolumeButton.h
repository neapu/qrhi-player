#pragma once
#include <QPushButton>

namespace view {
class VolumeButton : public QPushButton {
    Q_OBJECT
public:
    explicit VolumeButton(double volume, QWidget* parent = nullptr);

    void setVolume(double volume);
    void clearPreviousVolume();

signals:
    void volumeChanged(double volume);

private:
    void setIconWithVolume(double volume);

private slots:
    void onClicked();

private:
    double m_volume{1.0};
    double m_previousVolume{1.0};   // 静音时，记录之前的音量

    QIcon m_muteIcon{};
    QIcon m_downIcon{};
    QIcon m_upIcon{};
};

} // namespace view