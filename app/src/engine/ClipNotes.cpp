#include "engine/ClipNotes.h"

#include "model/Chance.h"

#include <cmath>
#include <map>

namespace fd::engine
{
namespace te = tracktion;

namespace
{
/// tracktion's controller type for one of ours, and back.
int tracktionType(int controller)
{
    if (controller == model::controllers::pitchBend)
        return te::MidiControllerEvent::pitchWheelType;
    if (controller == model::controllers::aftertouch)
        return te::MidiControllerEvent::channelPressureType;
    return controller;
}

std::optional<int> controllerOf(int type)
{
    if (type == te::MidiControllerEvent::pitchWheelType)
        return model::controllers::pitchBend;
    if (type == te::MidiControllerEvent::channelPressureType)
        return model::controllers::aftertouch;
    if (type >= 0 && type < 128)
        return type;
    return std::nullopt;
}

/// tracktion keeps control changes and pressure in 14 bits (the MIDI value << 7), pitch bend as is.
int tracktionValue(int controller, int midi)
{
    return controller == model::controllers::pitchBend ? midi : midi << 7;
}

int midiValue(int controller, int value)
{
    return controller == model::controllers::pitchBend ? value : value >> 7;
}

float velocityOf(int midi)
{
    return static_cast<float>(std::clamp(midi, 0, 127)) / 127.0f;
}

int midiOf(float velocity, int lowest)
{
    return std::clamp(static_cast<int>(std::lround(velocity * 127.0f)), lowest, 127);
}
} // namespace

model::Note noteOf(const te::MidiNote& n)
{
    model::Note note{n.getNoteNumber(), n.getStartBeat().inBeats(), n.getLengthBeats().inBeats(),
                     velocityOf(n.getVelocity()), n.isMute()};
    note.chance = std::clamp(static_cast<float>(n.state.getProperty(noteIds::chance, 1.0)), 0.0f, 1.0f);
    note.velocitySpread =
        std::clamp(static_cast<float>(n.state.getProperty(noteIds::spread, 0.0)), 0.0f, 1.0f);
    note.releaseVelocity = velocityOf(n.getNoteOffVelocity());
    return note;
}

void writeNoteExtras(te::MidiNote& note, const model::Note& from, juce::UndoManager* undo)
{
    // Only what differs from always, as written, is kept on the note.
    const auto write = [&](const juce::Identifier& id, float value, float plain)
    {
        if (value == plain)
            note.state.removeProperty(id, undo);
        else if (static_cast<float>(note.state.getProperty(id, plain)) != value)
            note.state.setProperty(id, value, undo);
    };
    write(noteIds::chance, std::clamp(from.chance, 0.0f, 1.0f), 1.0f);
    write(noteIds::spread, std::clamp(from.velocitySpread, 0.0f, 1.0f), 0.0f);
    if (const int release = midiOf(from.releaseVelocity, 0); release != note.getNoteOffVelocity())
        note.setNoteOffVelocity(release, undo);
}

std::vector<model::ControllerLane> controllerLanesOf(const te::MidiClip& clip)
{
    std::map<int, std::vector<model::ControllerEvent>> events;
    for (const auto* e : const_cast<te::MidiClip&>(clip).getSequence().getControllerEvents())
    {
        const auto controller = controllerOf(e->getType());
        if (!controller)
            continue; // program changes, polyphonic aftertouch: not a lane
        const auto& state = e->state;
        const auto kind = state.getProperty(noteIds::ramp, false)    ? model::ControllerEvent::Kind::Ramp
                          : state.getProperty(noteIds::point, false) ? model::ControllerEvent::Kind::Point
                                                                     : model::ControllerEvent::Kind::Played;
        events[*controller].push_back({.beat = e->getBeatPosition().inBeats(),
                                       .value = midiValue(*controller, e->getControllerValue()),
                                       .kind = kind,
                                       .curve = static_cast<float>(state.getProperty(noteIds::curve, 0.0))});
    }
    std::vector<model::ControllerLane> lanes;
    for (const auto& [controller, list] : events)
        lanes.push_back({.controller = controller, .points = model::controllerPoints(controller, list)});
    return lanes;
}

void writeControllerLanes(te::MidiClip& clip, const std::vector<model::ControllerLane>& lanes,
                          juce::UndoManager* undo)
{
    auto& list = clip.getSequence();
    for (const auto& lane : lanes)
    {
        const int type = tracktionType(lane.controller);
        std::vector<te::MidiControllerEvent*> old;
        for (auto* e : list.getControllerEvents())
            if (e->getType() == type)
                old.push_back(e);
        for (auto* e : old)
            list.removeControllerEvent(*e, undo);
        for (const auto& e : model::controllerEvents(lane))
        {
            auto* added = list.addControllerEvent(te::BeatPosition::fromBeats(e.beat), type,
                                                  tracktionValue(lane.controller, e.value), undo);
            if (added == nullptr)
                continue;
            if (e.kind == model::ControllerEvent::Kind::Ramp)
                added->state.setProperty(noteIds::ramp, true, undo);
            else
            {
                added->state.setProperty(noteIds::point, true, undo);
                if (e.curve != 0.0f)
                    added->state.setProperty(noteIds::curve, e.curve, undo);
            }
        }
    }
}

std::uint32_t chanceSeedOf(const te::MidiClip& clip)
{
    return static_cast<std::uint32_t>(static_cast<juce::int64>(clip.state.getProperty(noteIds::seed, 0)));
}

juce::MidiMessageSequence playbackSequence(const te::MidiList& list, te::MidiClip& clip,
                                           te::MidiList::TimeBase timeBase, bool generateMPE)
{
    std::vector<model::Note> notes;
    for (const auto* n : list.getNotes())
        notes.push_back(noteOf(*n));
    if (!model::usesChance(notes))
        return te::MidiList::createDefaultPlaybackMidiSequence(list, clip, timeBase, generateMPE);

    // A copy with each note's fate decided: those that don't play are muted (so left out), the
    // others take their lap's velocity. Laid out lap after lap, a looped clip's notes are told
    // apart by their lap; its loop played by the node is one lap, decided as the first.
    te::MidiList decided(list.state.createCopy(), nullptr);
    const bool laps = clip.isLooping() && &list != &clip.getSequence();
    const double loopLength = clip.getLoopLengthBeats().inBeats(),
                 loopStart = clip.getLoopStartBeats().inBeats();
    const auto seed = chanceSeedOf(clip);
    const auto id = clip.itemID.getRawID();
    for (auto* n : decided.getNotes())
    {
        const auto note = noteOf(*n);
        const double at = note.start;
        const int lap = laps && loopLength > 0.0 ? static_cast<int>(std::floor(at / loopLength + 1e-9)) : 0;
        const double place = laps ? at - lap * loopLength + loopStart : at;
        const auto fate = model::noteFate(note, place, lap, seed, id);
        if (!fate.plays)
            n->setMute(true, nullptr);
        else if (fate.velocity != note.velocity)
            n->setVelocity(midiOf(fate.velocity, 1), nullptr);
    }
    return te::MidiList::createDefaultPlaybackMidiSequence(decided, clip, timeBase, generateMPE);
}
} // namespace fd::engine
