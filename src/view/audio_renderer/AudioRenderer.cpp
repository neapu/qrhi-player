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

bool AudioRenderer::initialize()
{
    // Initialization logic for the audio renderer goes here.
    // Return true if initialization is successful, false otherwise.
    return true;
}

} // namespace view