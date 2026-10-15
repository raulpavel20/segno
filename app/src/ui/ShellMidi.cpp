// MIDI learn in the shell (M3 C8, docs/design/midi-learn.md): Options > MIDI map mode, its banner
// over the centre, the overlay that outlines what can be mapped, the transport's buttons pressed
// on a controller, and the app's mappings kept in its settings.

#include "ui/Shell.h"

namespace fd::app
{
std::optional<model::MidiTarget> Shell::midiTargetOf(const juce::Component& c) const
{
    if (auto t = midiTargetMark(c))
        return t;
    if (auto p = detail_.chain().targetOf(c))
        return model::MidiTarget{p->channel, p->key};
    return std::nullopt;
}

void Shell::registerMidiActions()
{
    const auto resolve = [this](const juce::Component& c) { return midiTargetOf(c); };
    midiOverlay_ = std::make_unique<MidiMapOverlay>(session_, *this, resolve);
    addAndMakeVisible(*midiOverlay_);
    tooltips_ = std::make_unique<Tooltips>(this, session_, resolve);
    midiBanner_ = std::make_unique<MidiMapBanner>();
    midiBanner_->done.onClick = [this] { setMidiMapMode(false); };
    addChildComponent(*midiBanner_);

    // Options > MIDI map mode (ML2): no default shortcut, Ctrl+M being the metronome's.
    actions_.add({.id = "midi.mapMode",
                  .name = "MIDI map mode",
                  .category = "MIDI",
                  .perform = [this] { setMidiMapMode(!midiMapMode_); },
                  .isToggled = [this] { return midiMapMode_; }});

    // A transport button pressed on a controller (§7): what its button on the top bar does.
    session_.onMidiTransport = [this](const std::string& key)
    {
        static const std::map<std::string, std::string> actions{
            {"transport.play", "transport.playStop"},       {"transport.stop", "transport.toStart"},
            {"transport.record", "transport.record"},       {"transport.loop", "transport.loop"},
            {"transport.metronome", "transport.metronome"}, {"transport.capture", "transport.capture"}};
        if (const auto it = actions.find(key); it != actions.end())
            actions_.perform(it->second);
    };
    // The transport's mappings are the app's (ML7): its settings keep them.
    session_.onAppMidiMappingsChanged = [this](const std::vector<model::MidiMapping>& mappings)
    { store_->change([&mappings](model::AppSettings& s) { s.controllers.transport = mappings; }); };
    session_.onMidiLearned = [this](const model::MidiMapping&) { midiOverlay_->update(); };
    // A mapping's Show (§6): its track, and its module in the chain.
    inspector_.midiMappings().onShow = [this](std::uint64_t channel, std::uint64_t module)
    {
        arrangement_.selectTrack(channel);
        if (module != 0)
        {
            openPanel(detailPanel_);
            detail_.chain().selectDevice(module);
        }
    };
}

void Shell::setMidiMapMode(bool on)
{
    if (midiMapMode_ == on)
        return;
    midiMapMode_ = on;
    midiOverlay_->setMapMode(on);
    midiBanner_->setVisible(on);
    inspector_.setMidiMappingsShown(on); // the project's mappings, as each one lands (§6)
    resized();
    repaint();
}
} // namespace fd::app
