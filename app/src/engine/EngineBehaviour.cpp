#include "engine/EngineBehaviour.h"

namespace fd::engine
{
namespace
{
class Behaviour final : public tracktion::EngineBehaviour
{
public:
    bool shouldProcessAuxSendWhenTrackIsMuted(tracktion::AuxSendPlugin&) override { return false; }

    juce::File getFileForNewAudioRecording(tracktion::Track& track, const juce::String& extension) override
    {
        return files ? files(track, extension) : juce::File();
    }

    RecordingFiles files;
};
} // namespace

std::unique_ptr<tracktion::EngineBehaviour> makeEngineBehaviour()
{
    return std::make_unique<Behaviour>();
}

void setRecordingFiles(tracktion::Engine& engine, RecordingFiles files)
{
    if (auto* behaviour = dynamic_cast<Behaviour*>(&engine.getEngineBehaviour()))
        behaviour->files = std::move(files);
}
} // namespace fd::engine
