#include "model/Recording.h"

namespace fd::model
{
RecordKind recordKind(const InputInfo& input, bool hasInstrument) noexcept
{
    if (input.records)
        return *input.records;
    if (!input.midi.empty() && hasInstrument)
        return RecordKind::Midi;
    if (!input.audio.empty())
        return RecordKind::Audio;
    return RecordKind::Midi;
}

bool hearsInput(const InputInfo& input) noexcept
{
    switch (input.monitor)
    {
    case Monitor::In: return true;
    case Monitor::Auto: return input.isArmed();
    case Monitor::Off: return false;
    }
    return false;
}

std::filesystem::path projectFileFor(const std::filesystem::path& chosen)
{
    const auto name = chosen.stem();
    const auto parent = chosen.parent_path();
    const auto file = std::filesystem::path(name).replace_extension(".segno");
    if (parent.filename() == name)
        return parent / file;
    return parent / name / file;
}
} // namespace fd::model
