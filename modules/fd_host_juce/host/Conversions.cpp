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
    else if (m.isAllSoundOff())
    {
        e.type = EventType::AllSoundOff; // CC 120: stop now (a Drum Kit's choke, ADR 0064)
    }
    else if (m.isAllNotesOff())
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

juce::MidiMessage toMidiMessage(const Event& e) noexcept
{
    const int channel = (e.channel & 15) + 1;
    const auto seven = [](float v) { return juce::jlimit(0, 127, juce::roundToInt(v * 127.0f)); };
    switch (e.type)
    {
    case EventType::NoteOn:
        // A note-on at velocity 0 would be read as a note-off: the quietest note-on is 1.
        return juce::MidiMessage::noteOn(channel, e.key,
                                         static_cast<juce::uint8>(std::max(1, seven(e.value))));
    case EventType::NoteOff:
        return juce::MidiMessage::noteOff(channel, e.key, static_cast<juce::uint8>(seven(e.value)));
    case EventType::Controller:
        return juce::MidiMessage::controllerEvent(channel, e.key, seven(e.value));
    case EventType::PitchBend:
        return juce::MidiMessage::pitchWheel(
            channel, juce::jlimit(0, 16383, 8192 + juce::roundToInt(e.value * 8192.0f)));
    case EventType::ChannelPressure:
        return juce::MidiMessage::channelPressureChange(channel, seven(e.value));
    case EventType::PolyPressure:
        return juce::MidiMessage::aftertouchChange(channel, e.key, seven(e.value));
    case EventType::AllNotesOff:
        return juce::MidiMessage::allNotesOff(channel);
    case EventType::AllSoundOff:
        return juce::MidiMessage::allSoundOff(channel);
    }
    return juce::MidiMessage::allNotesOff(channel);
}

void appendMidi(const juce::MidiBuffer& midi, EventList& out) noexcept
{
    for (const auto meta : midi)
        if (const auto e = toEvent(meta.getMessage(), meta.samplePosition))
            out.push(*e);
}

juce::MemoryBlock stateToMemory(const Device& device, const PluginExtras& extras)
{
    std::string json = serializeState(device);
    const auto view = extras.view.empty() ? nlohmann::json() : nlohmann::json::parse(extras.view, nullptr, false);
    const bool hasView = view.is_object();
    const auto preset =
        extras.preset.empty() ? nlohmann::json() : nlohmann::json::parse(extras.preset, nullptr, false);
    const bool hasPreset = preset.is_object() && !preset.empty();
    if (hasView || extras.sidechainExternal || hasPreset)
    {
        auto state = nlohmann::json::parse(json);
        if (hasView)
            state["view"] = view;
        if (extras.sidechainExternal)
            state["sidechain"] = "external";
        if (hasPreset)
            state["preset"] = preset;
        json = state.dump(2); // the same formatting as serializeState
    }
    return juce::MemoryBlock(json.data(), json.size());
}

PluginExtras extrasFromMemory(const void* data, int sizeInBytes)
{
    PluginExtras extras;
    if (data == nullptr || sizeInBytes <= 0)
        return extras;
    const auto* text = static_cast<const char*>(data);
    const auto json = nlohmann::json::parse(text, text + sizeInBytes, nullptr, false);
    if (!json.is_object())
        return extras;
    if (const auto it = json.find("view"); it != json.end() && it->is_object())
        extras.view = it->dump();
    if (const auto it = json.find("sidechain"); it != json.end() && it->is_string())
        extras.sidechainExternal = it->get<std::string>() == "external";
    if (const auto it = json.find("preset"); it != json.end() && it->is_object())
        extras.preset = it->dump();
    return extras;
}

ApplyResult stateFromMemory(Device& device, const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return ApplyResult::Invalid;
    return deserializeState(device, std::string_view(static_cast<const char*>(data), static_cast<size_t>(sizeInBytes)));
}
} // namespace fd::host
