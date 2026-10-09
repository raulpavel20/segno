#include "ui/Shell.h"

#include "fd/audiofile/AudioFile.h"

#include <nlohmann/json.hpp>

#include <juce_audio_utils/juce_audio_utils.h>

namespace fd::app
{
namespace
{

juce::KeyPress key(int code, int mods = 0)
{
    return juce::KeyPress(code, juce::ModifierKeys(mods), 0);
}

constexpr int kCmd = juce::ModifierKeys::commandModifier;
constexpr int kAlt = juce::ModifierKeys::altModifier;
constexpr int kShift = juce::ModifierKeys::shiftModifier;
} // namespace

Shell::Shell(engine::Session& session, mcp::McpServer* assistants)
    : session_(session), assistants_(assistants), arrangement_(session)
{
    if (assistants_ != nullptr)
    {
        assistantsPopover_ = std::make_unique<AssistantsPopover>(*assistants_);
        assistantsPopover_->onMessage = [this](const juce::String& m) { showWarning(m); };
    }
    registerActions();
    transport_ = std::make_unique<TransportBar>(session_, actions_);
    if (assistants_ != nullptr)
    {
        transport_->assistantsActive = [this] { return assistants_->isActive(); };
        assistants_->onStatusChanged = [this] {
            assistantsPopover_->refresh();
            transport_->repaint();
        };
    }

    // The module panel, the chain's margins and scrollbar (24), and the detail header.
    deviceViewHeight_ = static_cast<float>(ui::Theme::current().devicePanelHeight + 24 + ui::Theme::current().shell.panelHeaderHeight);
    detailPanel_.size = deviceViewHeight_;
    for (juce::Component* c : {static_cast<juce::Component*>(transport_.get()), static_cast<juce::Component*>(&browser_),
                               static_cast<juce::Component*>(&inspector_), static_cast<juce::Component*>(&detail_),
                               static_cast<juce::Component*>(&arrangement_), static_cast<juce::Component*>(&browserSplit_),
                               static_cast<juce::Component*>(&inspectorSplit_), static_cast<juce::Component*>(&detailSplit_)})
        addAndMakeVisible(c);
    detailSplit_.onDrag = [this](float dy) {
        clipViewHeight_ = juce::jlimit(200.0f, std::max(200.0f, static_cast<float>(getHeight()) - 260.0f), clipViewHeight_ - dy);
        detailPanel_.size = clipViewHeight_;
        resized();
    };

    browserSplit_.onDrag = [this](float dx) {
        browserPanel_.size = juce::jlimit(160.0f, 480.0f, browserPanel_.size + dx);
        resized();
    };
    inspectorSplit_.onDrag = [this](float dx) {
        inspectorPanel_.size = juce::jlimit(200.0f, 520.0f, inspectorPanel_.size - dx);
        resized();
    };

    arrangement_.onSelectionChanged = [this] { selectionChanged(); };
    // The mixer view shares the arrangement's selection (C3c, selection.md): a strip selects its
    // channel alone.
    addChildComponent(mixer_);
    mixer_.setSnapshot(session_.snapshot());
    mixer_.onSelect = [this](std::uint64_t channel) {
        lastArea_ = Area::Arrangement;
        arrangement_.selectTrack(channel);
    };
    mixer_.onSelectModule = [this](std::uint64_t channel, std::uint64_t module) {
        lastArea_ = Area::Detail;
        arrangement_.selectTrack(channel); // the chain now shows this channel
        if (!detailPanel_.open)
            toggle(detailPanel_);
        detail_.selectModule(module);
    };
    detail_.onModeChosen = [this](DetailPanel::Mode m) {
        arrangeMode_ = m;
        applyDetailMode();
    };
    arrangement_.onActivated = [this] { lastArea_ = Area::Arrangement; };
    arrangement_.onMessage = [this](const juce::String& m) { showWarning(m); };
    detail_.chain().onActivated = [this] { lastArea_ = Area::Detail; };
    detail_.pianoRoll().onActivated = [this] { lastArea_ = Area::Clip; };
    arrangement_.onOpenClip = [this](std::uint64_t id) {
        // MIDI clips open in the clip view (a clip made just now isn't in the snapshot yet).
        const auto found = model::findClip(*session_.snapshot(), id);
        if (found && found->first->kind != model::ClipInfo::Kind::Midi)
            return;
        arrangeMode_ = DetailPanel::Mode::Clip;
        applyDetailMode();
        if (!detailPanel_.open)
            toggle(detailPanel_);
    };
    session_.onSnapshotChanged = [this] {
        arrangement_.setSnapshot(session_.snapshot());
        mixer_.setSnapshot(session_.snapshot());
        selectionChanged();
    };
    // New/open: drop every panel, handle and meter before the old project goes away.
    session_.onProjectWillChange = [this] {
        subject_.reset();
        arrangement_.selectTrack(std::nullopt);
        arrangement_.releaseChannelControls();
        mixer_.clear();
        inspector_.clear();
        detail_.setSelection(std::nullopt);
        detail_.setClip(std::nullopt, {});
    };
    session_.onProjectChanged = [this] {
        arrangement_.setSnapshot(session_.snapshot());
        mixer_.setSnapshot(session_.snapshot());
        applyViewState(session_.savedViewState());
        selectionChanged();
        updateTitle();
    };
    session_.viewStateProvider = [this] { return viewState(); };

    setWantsKeyboardFocus(true);
    setSize(1600, 960);
}

Shell::~Shell()
{
    if (assistants_ != nullptr)
        assistants_->onStatusChanged = nullptr;
    session_.onSnapshotChanged = nullptr;
    session_.onProjectWillChange = nullptr;
    session_.onProjectChanged = nullptr;
    session_.viewStateProvider = nullptr;
}

// ---- actions -------------------------------------------------------------------------------

void Shell::registerActions()
{
    auto& s = session_;
    actions_.add({.id = "transport.playStop", .name = "Play / stop", .category = "Transport",
                  .keys = {key(juce::KeyPress::spaceKey)},
                  .perform = [&s] { s.isPlaying() ? s.stop() : s.play(); },
                  .isToggled = [&s] { return s.isPlaying(); }});
    actions_.add({.id = "transport.toStart", .name = "Stop and return to start", .category = "Transport",
                  .keys = {key(juce::KeyPress::homeKey)},
                  .perform = [&s] {
                      s.stop();
                      s.setPosition(0.0);
                  }});
    actions_.add({.id = "transport.loop", .name = "Loop", .category = "Transport", .keys = {key('l', kCmd)},
                  .perform = [&s] {
                      // With no brace yet, loop four bars from the playhead's bar.
                      const auto [a, b] = s.loopRange();
                      if (!(b > a))
                      {
                          const auto tempo = s.snapshot()->tempo;
                          const double bar = tempo.secondsForBeats(tempo.beatsPerBar());
                          const double start = std::floor(s.position() / bar) * bar;
                          s.setLoopRange(start, start + 4.0 * bar);
                      }
                      s.setLooping(!s.isLooping());
                  },
                  .isToggled = [&s] { return s.isLooping(); }});
    actions_.add({.id = "transport.metronome", .name = "Metronome", .category = "Transport", .keys = {key('m', kCmd)},
                  .perform = [&s] { s.setMetronome(!s.isMetronomeOn()); },
                  .isToggled = [&s] { return s.isMetronomeOn(); }});
    actions_.add({.id = "view.follow", .name = "Follow playhead", .category = "View", .keys = {key('f', kCmd | kShift)},
                  .perform = [this] { arrangement_.setFollowEnabled(!arrangement_.isFollowEnabled()); },
                  .isToggled = [this] { return arrangement_.isFollowEnabled(); }});
    actions_.add({.id = "edit.undo", .name = "Undo", .category = "Edit", .keys = {key('z', kCmd)},
                  .perform = [&s] { s.undo(); }, .isEnabled = [&s] { return s.canUndo(); }});
    actions_.add({.id = "edit.redo", .name = "Redo", .category = "Edit", .keys = {key('z', kCmd | kShift), key('y', kCmd)},
                  .perform = [&s] { s.redo(); }, .isEnabled = [&s] { return s.canRedo(); }});
    // Delete, Duplicate and Select all act on the selection in the area used last: notes in the
    // piano roll, a device in the chain, else clips. Clips only while the arrangement shows them
    // (selection.md §6).
    auto& roll = detail_.pianoRoll();
    auto clipsShown = [this] { return !mixerShown_; };
    actions_.add({.id = "edit.delete", .name = "Delete", .category = "Edit",
                  .keys = {key(juce::KeyPress::deleteKey), key(juce::KeyPress::backspaceKey)},
                  .perform = [this, &roll, clipsShown] {
                      if (notesActive() && roll.hasSelection())
                          roll.deleteSelection();
                      else if (lastArea_ == Area::Detail && detail_.chain().hasSelection())
                          detail_.chain().deleteSelected();
                      else if (clipsShown())
                          arrangement_.deleteSelection();
                  },
                  .isEnabled = [this, &roll, clipsShown] {
                      return (notesActive() && roll.hasSelection()) ||
                             (lastArea_ == Area::Detail && detail_.chain().hasSelection()) ||
                             (clipsShown() && arrangement_.hasClipSelection());
                  }});
    actions_.add({.id = "edit.duplicate", .name = "Duplicate", .category = "Edit", .keys = {key('d', kCmd)},
                  .perform = [this, &roll, clipsShown] {
                      if (notesActive() && roll.hasSelection())
                          roll.duplicateSelection();
                      else if (clipsShown())
                          arrangement_.duplicateSelection();
                  },
                  .isEnabled = [this, &roll, clipsShown] {
                      return (notesActive() && roll.hasSelection()) || (clipsShown() && arrangement_.hasClipSelection());
                  }});
    actions_.add({.id = "edit.split", .name = "Split at playhead", .category = "Edit", .keys = {key('e', kCmd)},
                  .perform = [this] { arrangement_.splitAtPlayhead(); }, .isEnabled = clipsShown});
    actions_.add({.id = "edit.selectAll", .name = "Select all", .category = "Edit", .keys = {key('a', kCmd)},
                  .perform = [this, &roll, clipsShown] {
                      if (notesActive())
                          roll.selectAll();
                      else if (clipsShown())
                          arrangement_.selectAllClips();
                  },
                  .isEnabled = [this, clipsShown] { return notesActive() || clipsShown(); }});

    // Notes (C2): the piano roll's selection, while it was used last.
    auto notesSelected = [this, &roll] { return notesActive() && roll.hasSelection(); };
    actions_.add({.id = "notes.up", .name = "Transpose up", .category = "Notes", .keys = {key(juce::KeyPress::upKey)},
                  .perform = [&roll] { roll.transpose(1); }, .isEnabled = notesSelected});
    actions_.add({.id = "notes.down", .name = "Transpose down", .category = "Notes", .keys = {key(juce::KeyPress::downKey)},
                  .perform = [&roll] { roll.transpose(-1); }, .isEnabled = notesSelected});
    actions_.add({.id = "notes.octaveUp", .name = "Transpose up an octave", .category = "Notes",
                  .keys = {key(juce::KeyPress::upKey, kShift)}, .perform = [&roll] { roll.transpose(12); },
                  .isEnabled = notesSelected});
    actions_.add({.id = "notes.octaveDown", .name = "Transpose down an octave", .category = "Notes",
                  .keys = {key(juce::KeyPress::downKey, kShift)}, .perform = [&roll] { roll.transpose(-12); },
                  .isEnabled = notesSelected});
    actions_.add({.id = "notes.later", .name = "Nudge later", .category = "Notes", .keys = {key(juce::KeyPress::rightKey)},
                  .perform = [&roll] { roll.nudge(1); }, .isEnabled = notesSelected});
    actions_.add({.id = "notes.earlier", .name = "Nudge earlier", .category = "Notes", .keys = {key(juce::KeyPress::leftKey)},
                  .perform = [&roll] { roll.nudge(-1); }, .isEnabled = notesSelected});
    actions_.add({.id = "notes.quantise", .name = "Quantise", .category = "Notes", .keys = {key('u', kCmd)},
                  .perform = [&roll] { roll.quantise(); },
                  .isEnabled = [this, &roll] { return detail_.showsPianoRoll() && !roll.notes().empty(); }});
    actions_.add({.id = "clip.newMidi", .name = "New MIDI clip", .category = "Clip", .keys = {key('m', kCmd | kShift)},
                  .perform = [this] { arrangement_.createMidiClipAtPlayhead(); },
                  .isEnabled = [this] { return !mixerShown_ && !session_.snapshot()->tracks.empty(); }});
    actions_.add({.id = "midi.computerKeyboard", .name = "Computer keyboard as MIDI", .category = "MIDI",
                  .keys = {key('k', kCmd | kShift)},
                  .perform = [this] {
                      computerKeyboard_ = !computerKeyboard_;
                      for (const auto& [code, note] : heldKeys_)
                          session_.computerKeyboardNote(note, 0.0f);
                      heldKeys_.clear();
                      if (!computerKeyboard_)
                          return;
                      // Playing needs an instrument track selected (live MIDI follows the
                      // selection): pick the first one, or say how to make one.
                      const auto selected = selectedTrackInfo();
                      if (!selected || selected->kind != model::TrackInfo::Kind::Instrument)
                      {
                          const auto& tracks = session_.snapshot()->tracks;
                          const auto it = std::find_if(tracks.begin(), tracks.end(), [](const auto& t) {
                              return t.kind == model::TrackInfo::Kind::Instrument;
                          });
                          if (it == tracks.end())
                          {
                              showWarning("Add an instrument to a track (+ in its module chain) to play it from the keyboard");
                              return;
                          }
                          arrangement_.selectTrack(it->id);
                      }
                      showWarning("Computer keyboard: A W S E D F ... play, Z / X octave, C / V velocity");
                  },
                  .isToggled = [this] { return computerKeyboard_; }});
    actions_.add({.id = "module.fold", .name = "Fold module", .category = "Module", .keys = {key('m', kCmd | kAlt)},
                  .perform = [this] { detail_.chain().foldSelected(); },
                  .isEnabled = [this] { return detail_.chain().selectedPanel() != nullptr; }});
    actions_.add({.id = "module.expand", .name = "Expand module sections", .category = "Module",
                  .keys = {key('e', kCmd | kAlt)},
                  .perform = [this] { detail_.chain().expandSelected(); },
                  .isEnabled = [this] { return detail_.chain().selectedPanel() != nullptr; }});
    actions_.add({.id = "track.add", .name = "Add track", .category = "Track", .keys = {key('t', kCmd)},
                  .perform = [&s] { s.addTrack(); }});
    // The mixer (C3, docs/design/mixer.md §9): the selected channel.
    auto selectedChannel = [this]() -> std::optional<model::TrackInfo> { return selectedTrackInfo(); };
    actions_.add({.id = "track.mute", .name = "Mute selected track", .category = "Track", .keys = {key('m', kAlt)},
                  .perform = [&s, selectedChannel] {
                      if (const auto c = selectedChannel(); c && c->role != model::TrackInfo::Role::Master)
                          s.setMute(c->id, !c->mix.mute);
                  },
                  .isEnabled = [selectedChannel] {
                      const auto c = selectedChannel();
                      return c && c->role != model::TrackInfo::Role::Master;
                  }});
    actions_.add({.id = "track.solo", .name = "Solo selected track", .category = "Track", .keys = {key('s', kAlt)},
                  .perform = [&s, selectedChannel] {
                      if (const auto c = selectedChannel(); c && c->role != model::TrackInfo::Role::Master)
                          s.setSolo(c->id, !c->mix.solo, true);
                  },
                  .isEnabled = [selectedChannel] {
                      const auto c = selectedChannel();
                      return c && c->role != model::TrackInfo::Role::Master;
                  }});
    actions_.add({.id = "track.addReturn", .name = "Add return track", .category = "Track",
                  .keys = {key('t', kCmd | kAlt)},
                  .perform = [this, &s] {
                      if (const auto id = s.addReturnTrack(); id != 0)
                      {
                          arrangement_.setMixRowsVisible(true);
                          s.updateSnapshotNow();
                          arrangement_.selectTrack(id);
                      }
                  }});
    actions_.add({.id = "view.mixer", .name = "Mixer view", .category = "View", .keys = {key('x', kCmd | kAlt)},
                  .perform = [this] { setMixerShown(!mixerShown_); },
                  .isToggled = [this] { return mixerShown_; }});
    auto arrangeShown = [this] { return !mixerShown_; }; // actions on what only the arrangement shows
    actions_.add({.id = "view.returns", .name = "Show returns and master", .category = "View",
                  .keys = {key('r', kCmd | kAlt)},
                  .perform = [this] { arrangement_.setMixRowsVisible(!arrangement_.mixRowsVisible()); },
                  .isEnabled = arrangeShown,
                  .isToggled = [this] { return arrangement_.mixRowsVisible(); }});

    actions_.add({.id = "view.browser", .name = "Show browser", .category = "View", .keys = {key('b', kCmd | kAlt)},
                  .perform = [this] { toggle(browserPanel_); }, .isToggled = [this] { return browserPanel_.open; }});
    // The inspector belongs to the arrangement (ADR 0027): the mixer's strips show every channel control.
    actions_.add({.id = "view.inspector", .name = "Show inspector", .category = "View", .keys = {key('i', kCmd | kAlt)},
                  .perform = [this] { toggle(inspectorPanel_); }, .isEnabled = arrangeShown,
                  .isToggled = [this] { return inspectorPanel_.open && !mixerShown_; }});
    actions_.add({.id = "view.detail", .name = "Show detail panel", .category = "View", .keys = {key('d', kCmd | kAlt)},
                  .perform = [this] { toggle(detailPanel_); }, .isToggled = [this] { return detailPanel_.open; }});
    actions_.add({.id = "view.detailMode", .name = "Switch modules / clip view", .category = "View",
                  .keys = {key(juce::KeyPress::tabKey)},
                  .perform = [this] {
                      arrangeMode_ = arrangeMode_ == DetailPanel::Mode::Devices ? DetailPanel::Mode::Clip
                                                                                : DetailPanel::Mode::Devices;
                      applyDetailMode();
                      if (!detailPanel_.open)
                          toggle(detailPanel_);
                  },
                  .isEnabled = arrangeShown});
    actions_.add({.id = "view.focus", .name = "Focus mode", .category = "View", .keys = {key('f', kCmd | kAlt)},
                  .perform = [this] {
                      focusMode_ = !focusMode_;
                      resized();
                  },
                  .isToggled = [this] { return focusMode_; }});
    actions_.add({.id = "view.zoomIn", .name = "Zoom in", .category = "View", .keys = {key('='), key('+')},
                  .perform = [this] { arrangement_.zoomIn(); }, .isEnabled = arrangeShown});
    actions_.add({.id = "view.zoomOut", .name = "Zoom out", .category = "View", .keys = {key('-')},
                  .perform = [this] { arrangement_.zoomOut(); }, .isEnabled = arrangeShown});
    actions_.add({.id = "view.zoomFit", .name = "Zoom to fit", .category = "View", .keys = {key('0', kCmd)},
                  .perform = [this] { arrangement_.zoomToFit(); }, .isEnabled = arrangeShown});
    actions_.add({.id = "view.snap", .name = "Snap to grid", .category = "View", .keys = {key('4', kCmd)},
                  .perform = [this] { arrangement_.setSnapEnabled(!arrangement_.isSnapEnabled()); },
                  .isToggled = [this] { return arrangement_.isSnapEnabled(); }});
    actions_.add({.id = "app.commandPalette", .name = "Command palette", .category = "App", .keys = {key('k', kCmd)},
                  .perform = [this] { palette_.isOpen() ? palette_.close() : palette_.open(*this); }});
    if (assistantsPopover_ != nullptr)
        actions_.add({.id = "app.assistants",
                      .name = "Assistants",
                      .category = "App",
                      .perform =
                          [this] {
                              if (assistantsPopover_->isOpen())
                                  assistantsPopover_->close();
                              else
                                  assistantsPopover_->open(*this, transport_->assistantsButtonBounds().withBottom(transport_->getBottom()));
                          },
                      .isToggled = [this] { return assistantsPopover_->isOpen(); }});
    actions_.add({.id = "file.new", .name = "New project", .category = "File", .keys = {key('n', kCmd)},
                  .perform = [this] { confirmDiscard([this] { session_.newProject(); }); }});
    actions_.add({.id = "file.open", .name = "Open project", .category = "File", .keys = {key('o', kCmd)},
                  .perform = [this] { confirmDiscard([this] { openProject(); }); }});
    actions_.add({.id = "file.save", .name = "Save", .category = "File", .keys = {key('s', kCmd)},
                  .perform = [this] {
                      if (session_.file())
                      {
                          if (!session_.save())
                              showWarning("Could not save " + session_.file()->getFullPathName());
                          updateTitle();
                      }
                      else
                          saveProjectAs();
                  }});
    actions_.add({.id = "file.saveAs", .name = "Save as", .category = "File", .keys = {key('s', kCmd | kShift)},
                  .perform = [this] { saveProjectAs(); }});
    actions_.add({.id = "app.scanPlugins", .name = "Scan for plugins", .category = "App", .keys = {key('p', kCmd | kAlt)},
                  .perform = [this] {
                      showWarning("Scanning for plugins...");
                      session_.scanPlugins([this](const juce::String& file) { showWarning("Scanning " + juce::File(file).getFileName()); },
                                           [this](int found) { showWarning("Plugin scan finished: " + juce::String(found) + " plugins"); });
                  },
                  .isEnabled = [this] { return !session_.isScanning(); }});
    actions_.add({.id = "app.audioSettings", .name = "Audio settings", .category = "App", .keys = {key(',', kCmd)},
                  .perform = [this] { showAudioSettings(); }});
}

bool Shell::notesActive() const
{
    return lastArea_ == Area::Clip && detail_.showsPianoRoll();
}

bool Shell::keyPressed(const juce::KeyPress& k)
{
    if (dynamic_cast<juce::TextEditor*>(juce::Component::getCurrentlyFocusedComponent()) != nullptr)
        return false; // typing goes to the text field
    if (computerKeyboard_ && handleComputerKeyboard(k))
        return true;
    if (actions_.handleKey(k))
        return true;
    // Tab is the clip view's key; in the mixer view it must not move focus off the strips.
    return mixerShown_ && k.getKeyCode() == juce::KeyPress::tabKey;
}

bool Shell::handleComputerKeyboard(const juce::KeyPress& k)
{
    if (k.getModifiers().isCommandDown() || k.getModifiers().isAltDown())
        return false; // shortcuts keep working
    static const std::map<juce::juce_wchar, int> semitones{{'a', 0},  {'w', 1},  {'s', 2},  {'e', 3},  {'d', 4},
                                                           {'f', 5},  {'t', 6},  {'g', 7},  {'y', 8},  {'h', 9},
                                                           {'u', 10}, {'j', 11}, {'k', 12}, {'o', 13}, {'l', 14},
                                                           {'p', 15}, {';', 16}};
    const auto c = juce::CharacterFunctions::toLowerCase(k.getTextCharacter());
    if (c == 'z' || c == 'x')
    {
        keyboardOctave_ = juce::jlimit(-4, 4, keyboardOctave_ + (c == 'x' ? 1 : -1));
        showWarning("Computer keyboard: C" + juce::String(3 + keyboardOctave_));
        return true;
    }
    if (c == 'c' || c == 'v')
    {
        keyboardVelocity_ = juce::jlimit(0.1f, 1.0f, keyboardVelocity_ + (c == 'v' ? 0.1f : -0.1f));
        showWarning("Computer keyboard velocity: " + juce::String(juce::roundToInt(keyboardVelocity_ * 127.0f)));
        return true;
    }
    const auto it = semitones.find(c);
    if (it == semitones.end())
        return false;
    if (!heldKeys_.contains(k.getKeyCode())) // ignore auto-repeat
    {
        const int note = 60 + 12 * keyboardOctave_ + it->second;
        heldKeys_[k.getKeyCode()] = note;
        session_.computerKeyboardNote(note, keyboardVelocity_);
    }
    return true;
}

bool Shell::keyStateChanged(bool)
{
    // Note-offs: a held key is no longer down.
    for (auto it = heldKeys_.begin(); it != heldKeys_.end();)
        if (!juce::KeyPress::isKeyCurrentlyDown(it->first))
        {
            session_.computerKeyboardNote(it->second, 0.0f);
            it = heldKeys_.erase(it);
        }
        else
            ++it;
    return false;
}

void Shell::toggle(Panel& p)
{
    p.open = !p.open;
    if (focusMode_)
        focusMode_ = false;
    resized();
}

// ---- projects (C8) -------------------------------------------------------------------------

void Shell::confirmDiscard(std::function<void()> then)
{
    if (!session_.hasUnsavedChanges())
    {
        then();
        return;
    }
    const auto name = session_.file() ? session_.file()->getFileNameWithoutExtension() : juce::String("Untitled");
    auto options = juce::MessageBoxOptions()
                       .withIconType(juce::MessageBoxIconType::NoIcon)
                       .withTitle("Unsaved changes")
                       .withMessage("Save the changes to \"" + name + "\"?")
                       .withButton("Save")
                       .withButton("Don't save")
                       .withButton("Cancel")
                       .withAssociatedComponent(this);
    juce::AlertWindow::showAsync(options, [this, next = std::move(then)](int result) {
        if (result == 1) // Save
        {
            if (session_.file() && session_.save())
                next();
            else
                saveProjectAs(next);
        }
        else if (result == 2) // Don't save
            next();
    });
}

void Shell::openProject()
{
    const auto folder = session_.file() ? session_.file()->getParentDirectory()
                                        : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    chooser_ = std::make_unique<juce::FileChooser>("Open project", folder, juce::String("*") + engine::Session::fileExtension);
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this](const juce::FileChooser& fc) {
                              const auto file = fc.getResult();
                              if (file == juce::File())
                                  return;
                              std::string error;
                              if (!session_.open(file, &error))
                                  showWarning(juce::String::fromUTF8(error.c_str()));
                          });
}

void Shell::saveProjectAs(std::function<void()> then)
{
    const auto initial = session_.file() ? *session_.file()
                                         : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                               .getChildFile(juce::String("Untitled") + engine::Session::fileExtension);
    chooser_ = std::make_unique<juce::FileChooser>("Save project", initial, juce::String("*") + engine::Session::fileExtension);
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, next = std::move(then)](const juce::FileChooser& fc) {
                              auto file = fc.getResult();
                              if (file == juce::File())
                                  return;
                              if (!file.hasFileExtension(engine::Session::fileExtension))
                                  file = file.withFileExtension(engine::Session::fileExtension);
                              if (!session_.saveAs(file))
                              {
                                  showWarning("Could not save " + file.getFullPathName());
                                  return;
                              }
                              updateTitle();
                              if (next)
                                  next();
                          });
}

std::string Shell::viewState() const
{
    const auto& tl = arrangement_.timeline();
    return nlohmann::json{{"start", tl.start()},
                          {"pixelsPerSecond", tl.pixelsPerSecond()},
                          {"snap", arrangement_.isSnapEnabled()},
                          {"follow", arrangement_.isFollowEnabled()},
                          {"browser", browserPanel_.open},
                          {"inspector", inspectorPanel_.open},
                          {"detail", detailPanel_.open},
                          {"browserWidth", browserPanel_.size},
                          {"inspectorWidth", inspectorPanel_.size},
                          {"clipViewHeight", clipViewHeight_},
                          {"view", mixerShown_ ? "mix" : "arrange"},
                          {"returns", arrangement_.mixRowsVisible()},
                          {"mixerModules", mixer_.options().modules},
                          {"mixerSends", mixer_.options().sends},
                          {"mixerNarrow", mixer_.options().narrow},
                          {"mixerScroll", mixer_.scrollX()}}
        .dump();
}

void Shell::applyViewState(const std::string& json)
{
    const auto v = nlohmann::json::parse(json, nullptr, false);
    if (!v.is_object())
        return;
    auto get = [&](const char* key, auto fallback) { return v.contains(key) ? v[key].get<decltype(fallback)>() : fallback; };
    arrangement_.setView(get("start", 0.0), get("pixelsPerSecond", arrangement_.timeline().pixelsPerSecond()));
    arrangement_.setSnapEnabled(get("snap", true));
    arrangement_.setFollowEnabled(get("follow", true));
    browserPanel_.open = get("browser", browserPanel_.open);
    inspectorPanel_.open = get("inspector", inspectorPanel_.open);
    detailPanel_.open = get("detail", detailPanel_.open);
    browserPanel_.size = get("browserWidth", browserPanel_.size);
    inspectorPanel_.size = get("inspectorWidth", inspectorPanel_.size);
    clipViewHeight_ = get("clipViewHeight", clipViewHeight_);
    mixerShown_ = v.value("view", std::string("arrange")) == "mix";
    arrangement_.setMixRowsVisible(get("returns", true));
    mixer_.setOptions(get("mixerModules", true), get("mixerSends", true), get("mixerNarrow", false));
    mixer_.setScrollX(get("mixerScroll", 0));
    resized();
}

void Shell::updateTitle()
{
    auto title = juce::String(ui::brand::name) + " - " +
                 (session_.file() ? session_.file()->getFileNameWithoutExtension() : juce::String("Untitled"));
    if (session_.hasUnsavedChanges())
        title << " *";
    if (title != title_)
    {
        title_ = title;
        if (onTitleChanged)
            onTitleChanged(title_);
    }
}

void Shell::showAudioSettings()
{
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent>(session_.deviceManager(), 0, 2, 2, 2, false,
                                                                         false, true, false);
    selector->setSize(500, 380);
    // A popover next to its button, closed by clicking away (ui-principles §2: no modal dialog).
    auto& anchor = transport_->settingsButton();
    juce::CallOutBox::launchAsynchronously(std::move(selector), getLocalArea(&anchor, anchor.getLocalBounds()), this);
}

// ---- selection -----------------------------------------------------------------------------

std::optional<model::TrackInfo> Shell::selectedTrackInfo() const
{
    const auto id = arrangement_.selectedTrack();
    if (!id)
        return std::nullopt;
    if (const auto* channel = session_.snapshot()->channel(*id))
        return *channel; // a track, a return track or the master
    return std::nullopt;
}

void Shell::selectionChanged()
{
    const auto snapshot = session_.snapshot();
    const auto context = model::panelContext(arrangement_.selection(), *snapshot,
                                             mixerShown_ ? model::CentreView::Mix : model::CentreView::Arrange);
    mixer_.setSelection(context.channel ? std::optional(context.channel->id) : std::nullopt);

    // Live MIDI plays the selected track's instrument (auto-arm); other tracks hear nothing.
    // The piano roll previews notes through the same route.
    if (context.midiInputTarget != session_.midiInputTarget())
        session_.setMidiInputTarget(context.midiInputTarget);
    inspector_.setContext(context, snapshot->tempo, snapshot->returns);
    detail_.setSelection(context.channel);
    detail_.setClip(context.midiClip, snapshot->tempo);

    // The detail panel follows the subject: a MIDI clip opens the clip view, anything else the
    // modules; Tab switches until the subject changes.
    if (context.key != subject_)
    {
        subject_ = context.key;
        arrangeMode_ = context.opensClipView ? DetailPanel::Mode::Clip : DetailPanel::Mode::Devices;
    }
    applyDetailMode();
}

void Shell::applyDetailMode()
{
    detail_.setClipViewAvailable(!mixerShown_);
    detail_.setMode(mixerShown_ ? DetailPanel::Mode::Devices : arrangeMode_);
}

void Shell::setMixerShown(bool shown)
{
    mixerShown_ = shown;
    selectionChanged(); // the panels show what this view needs
    resized();
    if (mixerShown_ && mixer_.isShowing())
        mixer_.grabKeyboardFocus();
}

void Shell::showWarning(const juce::String& message)
{
    warning_ = message;
    warningUntil_ = juce::Time::getMillisecondCounterHiRes() * 0.001 + 5.0;
    repaint();
}

// ---- frames and layout ---------------------------------------------------------------------

void Shell::onFrame(double timestamp)
{
    const double dt = lastFrame_ > 0.0 ? std::clamp(timestamp - lastFrame_, 0.0, 0.1) : 1.0 / 60.0;
    lastFrame_ = timestamp;

    bool relayout = false;
    for (auto* p : {&browserPanel_, &inspectorPanel_, &detailPanel_})
    {
        const float target = p->open ? 1.0f : 0.0f;
        if (p->shown != target)
        {
            const auto k = static_cast<float>(1.0 - std::exp(-dt / ui::Theme::current().motion.panel));
            p->shown += (target - p->shown) * k;
            if (std::abs(p->shown - target) < 0.01f)
                p->shown = target;
            relayout = true;
        }
    }
    // The detail panel's height follows its mode: the device view's fixed height, or the clip
    // view's (resizable at its top edge).
    const float detailTarget = detail_.mode() == DetailPanel::Mode::Clip ? clipViewHeight_ : deviceViewHeight_;
    if (detailPanel_.size != detailTarget)
    {
        const auto k = static_cast<float>(1.0 - std::exp(-dt / ui::Theme::current().motion.panel));
        detailPanel_.size += (detailTarget - detailPanel_.size) * k;
        if (std::abs(detailPanel_.size - detailTarget) < 0.5f)
            detailPanel_.size = detailTarget;
        relayout = true;
    }
    if (relayout)
        resized();

    if (benchmark_)
    {
        // Zoom in for 2 s, out for 2 s, repeatedly (about 6 octaves each way at 120 Hz).
        const bool in = (benchmark_->frame++ / 240) % 2 == 0;
        arrangement_.zoomAt(arrangement_.timeline().width() * 0.45, in ? 1.035 : 1.0 / 1.035);
        if (juce::Time::getMillisecondCounterHiRes() * 0.001 > benchmark_->until)
        {
            auto done = std::move(benchmark_->done);
            benchmark_.reset();
            if (done)
                done(statsReport());
        }
    }
    if (statsVisible_ || benchmark_)
    {
        stats_.frameMs.push_back(dt * 1000.0);
        stats_.paintMs.push_back(arrangement_.lastPaintMs());
        if (stats_.frameMs.size() > 4000)
            stats_.clear();
        if (statsVisible_)
            repaint(getLocalBounds().removeFromBottom(28).removeFromLeft(360));
    }

    updateTitle(); // cheap: notifies only when it changes

    const double position = session_.position();
    arrangement_.frame(dt, position);
    detail_.pianoRoll().setSnapEnabled(arrangement_.isSnapEnabled());
    if (detail_.isVisible())
        detail_.frame(position);
    transport_->frame(position, session_.snapshot()->tempo);

    if (warning_.isNotEmpty() && timestamp > 0.0 && juce::Time::getMillisecondCounterHiRes() * 0.001 > warningUntil_)
    {
        warning_.clear();
        repaint();
    }
}

void Shell::finishAnimations()
{
    for (auto* p : {&browserPanel_, &inspectorPanel_, &detailPanel_})
        p->shown = p->open ? 1.0f : 0.0f;
    detailPanel_.size = detail_.mode() == DetailPanel::Mode::Clip ? clipViewHeight_ : deviceViewHeight_;
    resized();
}

void Shell::setStatsVisible(bool visible)
{
    statsVisible_ = visible;
    stats_.clear();
    repaint();
}

juce::String Shell::statsReport() const
{
    auto summary = [](std::vector<double> v) {
        if (v.empty())
            return std::pair{0.0, 0.0};
        std::sort(v.begin(), v.end());
        double sum = 0.0;
        for (double x : v)
            sum += x;
        return std::pair{sum / static_cast<double>(v.size()), v[v.size() * 95 / 100]};
    };
    const auto [paintAvg, paintP95] = summary(stats_.paintMs);
    const auto [frameAvg, frameP95] = summary(stats_.frameMs);
    return "arrangement paint avg " + juce::String(paintAvg, 2) + " ms, p95 " + juce::String(paintP95, 2) +
           " ms; frames avg " + juce::String(frameAvg > 0.0 ? 1000.0 / frameAvg : 0.0, 1) + " fps, p95 interval " +
           juce::String(frameP95, 2) + " ms (" + juce::String(static_cast<int>(stats_.frameMs.size())) + " frames)";
}

void Shell::runBenchmark(double seconds, std::function<void(juce::String)> done)
{
    // 32 tracks with 8 clips each of a busy 16 s stereo file.
    {
        constexpr int rate = 48000;
        std::vector<std::vector<float>> ch(2, std::vector<float>(16 * rate));
        for (size_t i = 0; i < ch[0].size(); ++i)
            for (size_t c = 0; c < 2; ++c)
                ch[c][i] = 0.6f * std::sin(0.011f * static_cast<float>(i) * (1.0f + 0.3f * static_cast<float>(c))) *
                           std::exp(-static_cast<float>(i % 12000) / 5000.0f);
        fd::audiofile::writeWav(benchmarkFile_.getFile().getFullPathName().toStdString(), ch, rate);
    }
    while (session_.snapshot()->tracks.size() < 32)
    {
        session_.addTrack();
        session_.updateSnapshotNow();
    }
    for (const auto& t : session_.snapshot()->tracks)
        for (int k = 0; k < 8; ++k)
            session_.insertAudioFile(t.id, benchmarkFile_.getFile(), 16.0 * k);
    session_.updateSnapshotNow();
    arrangement_.waveforms().waitUntilIdle();
    // All 32 tracks on screen: focus mode, tracks shrunk to fit.
    focusMode_ = true;
    resized();
    arrangement_.setAllTrackHeights((arrangement_.getHeight() - ArrangementView::rulerHeight()) / 32.0);
    arrangement_.zoomToFit();

    // Let the zoom-to-fit settle, then measure.
    juce::Timer::callAfterDelay(1000, [this, seconds, onDone = std::move(done)]() mutable {
        stats_.clear();
        benchmark_ = Benchmark{juce::Time::getMillisecondCounterHiRes() * 0.001 + seconds, 0, std::move(onDone)};
    });
}

void Shell::paint(juce::Graphics& g)
{
    g.fillAll(ui::Theme::current().surface0); // shows through the 1 px gaps between panels
}

void Shell::paintOverChildren(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    if (statsVisible_)
    {
        auto box = getLocalBounds().removeFromBottom(28).removeFromLeft(360).reduced(6, 4);
        g.setColour(t.roles.overlay);
        g.fillRect(box);
        g.setColour(t.textPrimary);
        g.setFont(ui::font(ui::TextStyle::Value));
        const double paint = stats_.paintMs.empty() ? 0.0 : stats_.paintMs.back();
        const double frame = stats_.frameMs.empty() ? 0.0 : stats_.frameMs.back();
        g.drawText("paint " + juce::String(paint, 2) + " ms   frame " + juce::String(frame, 2) + " ms",
                   box.reduced(6, 0), juce::Justification::centredLeft, false);
    }
    if (warning_.isEmpty())
        return;
    ui::paintToast(g, ui::toastArea(arrangement_.getBounds()), warning_);
}

void Shell::resized()
{
    // Adaptive: crossing the compact threshold resets the side panels; toggles win otherwise.
    const bool compact = getWidth() < compactWidth;
    if (compact_ != compact)
    {
        compact_ = compact;
        browserPanel_.open = inspectorPanel_.open = !compact;
        browserPanel_.shown = inspectorPanel_.shown = compact ? 0.0f : 1.0f;
    }

    auto r = getLocalBounds();
    transport_->setBounds(r.removeFromTop(TransportBar::height));
    r.removeFromTop(1);

    const bool showSides = !focusMode_;
    const int detailH = showSides ? std::min(juce::roundToInt(detailPanel_.size * detailPanel_.shown), std::max(0, r.getHeight() - 160)) : 0;
    detail_.setVisible(detailH > 0);
    detail_.setBounds(r.removeFromBottom(detailH));
    if (detailH > 0)
        r.removeFromBottom(1);
    detailSplit_.setVisible(detailH > 0 && detailPanel_.open && detail_.mode() == DetailPanel::Mode::Clip);
    detailSplit_.setBounds(detail_.getBounds().withHeight(5).translated(0, -3));

    const auto rail = static_cast<float>(ui::Theme::current().shell.railWidth);
    const float browserW = showSides ? rail + (browserPanel_.size - rail) * browserPanel_.shown : 0.0f;
    browser_.setVisible(browserW > 0.0f);
    browser_.setRail(browserPanel_.shown < 0.5f);
    browser_.setBounds(r.removeFromLeft(juce::roundToInt(browserW)));
    browserSplit_.setVisible(showSides && browserPanel_.open);
    const int splitter = ui::Theme::current().shell.splitter;
    browserSplit_.setBounds(r.removeFromLeft(browserW > 0.0f ? 1 : 0).withWidth(splitter).translated(-splitter / 2, 0));

    // The inspector belongs to the arrangement (ADR 0027).
    const bool inspectorShown = showSides && !mixerShown_;
    const int inspectorW = inspectorShown ? juce::roundToInt(inspectorPanel_.size * inspectorPanel_.shown) : 0;
    inspector_.setVisible(inspectorW > 0);
    inspector_.setBounds(r.removeFromRight(inspectorW));
    inspectorSplit_.setVisible(inspectorShown && inspectorPanel_.open);
    inspectorSplit_.setBounds(r.removeFromRight(inspectorW > 0 ? 1 : 0).withWidth(splitter).translated(-splitter / 2, 0));

    // The centre: the arrangement or the mixer view (C3c), whichever is current.
    arrangement_.setVisible(!mixerShown_);
    mixer_.setVisible(mixerShown_);
    arrangement_.setBounds(r);
    mixer_.setBounds(r);
    browserSplit_.toFront(false);
    inspectorSplit_.toFront(false);
    detailSplit_.toFront(false);
}
} // namespace fd::app
