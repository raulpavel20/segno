namespace fd::host
{
std::optional<Event> toEvent(const juce::MidiMessage& m, int sampleOffset) noexcept
{
    Event e;
    e.sampleOffset = sampleOffset;
    e.channel = static_cast<std::uint8_t>(juce::jlimit(0, 15, m.getChannel() - 1));
    if (m.isNoteOn())
    {
        e.type = EventType::NoteOn;
        e.key = static_cast<std::uint8_t>(m.getNoteNumber());
        e.value = m.getFloatVelocity();
    }
    else if (m.isNoteOff())
    {
        e.type = EventType::NoteOff;
        e.key = static_cast<std::uint8_t>(m.getNoteNumber());
        e.value = m.getFloatVelocity();
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        e.type = EventType::AllNotesOff;
    }
    else if (m.isController())
    {
        e.type = EventType::Controller;
        e.key = static_cast<std::uint8_t>(m.getControllerNumber());
        e.value = static_cast<float>(m.getControllerValue()) / 127.0f;
    }
    else if (m.isPitchWheel())
    {
        e.type = EventType::PitchBend;
        e.value = static_cast<float>(m.getPitchWheelValue() - 8192) / 8192.0f;
    }
    else if (m.isChannelPressure())
    {
        e.type = EventType::ChannelPressure;
        e.value = static_cast<float>(m.getChannelPressureValue()) / 127.0f;
    }
    else if (m.isAftertouch())
    {
        e.type = EventType::PolyPressure;
        e.key = static_cast<std::uint8_t>(m.getNoteNumber());
        e.value = static_cast<float>(m.getAfterTouchValue()) / 127.0f;
    }
    else
    {
        return std::nullopt;
    }
    return e;
}

void appendMidi(const juce::MidiBuffer& midi, EventList& out) noexcept
{
    for (const auto meta : midi)
        if (const auto e = toEvent(meta.getMessage(), meta.samplePosition))
            out.push(*e);
}

juce::MemoryBlock stateToMemory(const Device& device, const std::string& viewState)
{
    std::string json = serializeState(device);
    if (!viewState.empty())
    {
        auto state = nlohmann::json::parse(json);
        auto view = nlohmann::json::parse(viewState, nullptr, false);
        if (!view.is_discarded())
        {
            state["view"] = std::move(view);
            json = state.dump(2); // the same formatting as serializeState
        }
    }
    return juce::MemoryBlock(json.data(), json.size());
}

std::string viewStateFromMemory(const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return {};
    const auto* text = static_cast<const char*>(data);
    const auto json = nlohmann::json::parse(text, text + sizeInBytes, nullptr, false);
    if (!json.is_object())
        return {};
    const auto it = json.find("view");
    return it != json.end() && it->is_object() ? it->dump() : std::string();
}

ApplyResult stateFromMemory(Device& device, const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return ApplyResult::Invalid;
    return deserializeState(device, std::string_view(static_cast<const char*>(data), static_cast<size_t>(sizeInBytes)));
}
} // namespace fd::host
