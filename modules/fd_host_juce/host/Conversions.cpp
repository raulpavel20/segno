namespace fd::host
{
void appendMidi(const juce::MidiBuffer& midi, EventList& out) noexcept
{
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        Event e;
        e.sampleOffset = meta.samplePosition;
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
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            e.type = EventType::AllNotesOff;
        }
        else
        {
            continue;
        }
        out.push(e);
    }
}

juce::MemoryBlock stateToMemory(const Device& device)
{
    const std::string json = serializeState(device);
    return juce::MemoryBlock(json.data(), json.size());
}

ApplyResult stateFromMemory(Device& device, const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return ApplyResult::Invalid;
    return deserializeState(device, std::string_view(static_cast<const char*>(data), static_cast<size_t>(sizeInBytes)));
}
} // namespace fd::host
