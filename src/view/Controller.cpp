#include "Controller.h"

namespace view {
Controller::Controller(QObject* parent)
    : QObject(parent)
{
}

bool Controller::openFile(const QString& filePath)
{
    // Implement the logic to open the file here
    return true; // Return true if the file was successfully opened, false otherwise
}

void Controller::closeFile()
{
    // Implement the logic to close the file here
}

fh::FramePtr Controller::nextVideoFrame()
{
    // Implement the logic to retrieve the next video frame here
    return {}; // Return the next video frame, or an empty pointer if no more frames are available
}

fh::FramePtr Controller::nextAudioFrame()
{
    // Implement the logic to retrieve the next audio frame here
    return {}; // Return the next audio frame, or an empty pointer if no more frames are available
}

void Controller::pauseOrResume()
{
    // Implement the logic to pause or resume playback here
}

Controller::State Controller::state() const
{
    // Implement the logic to retrieve the current state of the controller here
    return State::Stopped; // Return the current state
}

void Controller::setVolume(float volume)
{
    // Implement the logic to set the volume here
    m_volume = volume;
}

void Controller::seek(int64_t positionUs)
{
    // Implement the logic to seek to the specified position here
}

} // namespace view