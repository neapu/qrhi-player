#include "AudioRenderer.h"

namespace view {
std::unique_ptr<AudioRenderer> AudioRenderer::create(const Params& params)
{
    auto renderer = std::unique_ptr<AudioRenderer>(new AudioRenderer(params));
    if (!renderer->initialize()) {
        return nullptr;
    }
    return renderer;
}

AudioRenderer::AudioRenderer(const Params& params)
    : m_frameCallback(params.frameCallback)
    , m_sampleRate(params.sampleRate)
    , m_channels(params.channels)
    , m_sampleFormat(params.sampleFormat)
    , m_volume(params.volume)
    , m_timeBase(params.timeBase)
{}

AudioRenderer::~AudioRenderer()
{
}

bool AudioRenderer::initialize()
{
    // Initialization logic for the audio renderer goes here.
    // Return true if initialization is successful, false otherwise.
    return true;
}

void AudioRenderer::setVolume(float volume)
{
    m_volume = volume;
}

int64_t AudioRenderer::playbackPosition() const
{
    // Return the current playback position in microseconds.
    return 0;
}

bool AudioRenderer::playing() const
{
    // Return true if the audio renderer is currently playing.
    return false;
}

bool AudioRenderer::start()
{
    // Start the audio renderer.
    return true;
}

void AudioRenderer::stop()
{
    // Stop the audio renderer.
}

} // namespace view