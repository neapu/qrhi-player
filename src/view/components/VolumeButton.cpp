#include "VolumeButton.h"

namespace view {
VolumeButton::VolumeButton(double volume, QWidget* parent)
    : QPushButton(parent), m_volume(volume), m_previousVolume(volume)
{
    connect(this, &QPushButton::clicked, this, &VolumeButton::onClicked);
    m_muteIcon = QIcon(":/icons/volume-mute.svg");
    m_downIcon = QIcon(":/icons/volume-down.svg");
    m_upIcon = QIcon(":/icons/volume-up.svg");
    setIconWithVolume(volume);
}

void VolumeButton::setVolume(double volume)
{
    m_volume = volume;
    setIconWithVolume(volume);
    // 不发射信号
}

void VolumeButton::clearPreviousVolume()
{
    m_previousVolume = 0.0;
}

void VolumeButton::setIconWithVolume(double volume)
{
    if (volume == 0.0) {
        setIcon(m_muteIcon);
    } else if (volume < 0.9) {
        setIcon(m_downIcon);
    } else {
        setIcon(m_upIcon);
    }
}

void VolumeButton::onClicked()
{
    if (m_volume == 0.0 && m_previousVolume == 0.0) {
        emit volumeChanged(1.0);
    } else if (m_volume == 0.0) {
        emit volumeChanged(m_previousVolume);
    } else {
        m_previousVolume = m_volume;
        emit volumeChanged(0.0);
    }
}

} // namespace view