#include "engine/EngineBehaviour.h"

namespace fd::engine
{
namespace
{
class Behaviour final : public tracktion::EngineBehaviour
{
public:
    bool shouldProcessAuxSendWhenTrackIsMuted(tracktion::AuxSendPlugin&) override { return false; }
};
} // namespace

std::unique_ptr<tracktion::EngineBehaviour> makeEngineBehaviour()
{
    return std::make_unique<Behaviour>();
}
} // namespace fd::engine
