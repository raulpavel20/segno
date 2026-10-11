#include "ui/Shell.h"

#include "ui/AutomationMenu.h"

#include "model/Takes.h"

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

/// An item that asks for more before it acts ends with an ellipsis (menu-bar.md §5).
juce::String asks(const char* name)
{
    return juce::String(name) + juce::String::fromUTF8("\xE2\x80\xA6");
}

#if JUCE_MAC
constexpr auto kMenuPlatform = model::MenuPlatform::Mac;
#else
constexpr auto kMenuPlatform = model::MenuPlatform::Windowed;
#endif
} // namespace

Shell::Shell(engine::Session& session, mcp::McpServer* assistants)
    : session_(session), assistants_(assistants), arrangement_(session)
{
    // The export panel (export.md §6): stems of the selected tracks; the result in a toast.
    exportPanel_.selectedTracks = [this] {
        std::vector<std::uint64_t> out;
        const auto& sel = arrangement_.selection();
        if (sel.channel)
            out.push_back(*sel.channel);
        for (const auto id : sel.clips)
            if (const auto found = model::findClip(*session_.snapshot(), id))
            {
                const auto track = session_.snapshot()->tracks[static_cast<size_t>(found->second)].id;
                if (std::find(out.begin(), out.end(), track) == out.end())
                    out.push_back(track);
            }
        return out;
    };
    exportPanel_.onMessage = [this](const juce::String& m) { showWarning(m); };
    exportPanel_.onExported = [this](const engine::Session::ExportResult& r) {
        if (r.cancelled)
        {
            showWarning("Export cancelled: no file was left");
            return;
        }
        if (!r.error.empty() || r.files.empty())
            return; // the panel said why
        const auto& f = r.files.front();
        const auto& m = f.measured;
        const auto minus = juce::String::fromUTF8("\xe2\x88\x92"), dot = juce::String::fromUTF8(" \xc2\xb7 ");
        auto text = "Exported " + f.file.getFileName();
        if (r.files.size() > 1)
            text += " and " + juce::String(static_cast<int>(r.files.size()) - 1) + " more";
        else if (std::isfinite(m.integratedLufs) && std::isfinite(m.truePeakDb))
            text += dot + juce::String(m.integratedLufs, 1).replace("-", minus) + " LUFS" + dot +
                    juce::String(m.truePeakDb, 1).replace("-", minus) + " dBTP";
        showToast(text, "Show", [file = f.file] { file.revealToUser(); });
    };
    addChildComponent(toastClick_);
    toastClick_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    toastClick_.onClick = [this] {
        if (auto run = toastRun_)
            run();
        warning_.clear();
        toastClick_.setVisible(false);
        repaint();
    };

    if (assistants_ != nullptr)
    {
        assistantsPopover_ = std::make_unique<AssistantsPopover>(*assistants_);
        assistantsPopover_->onMessage = [this](const juce::String& m) { showWarning(m); };
    }
    registerActions();
    inspector_.setActions(actions_);
    arrangement_.snapToggle().setTooltip(actions_.tooltip("view.snap"));
    arrangement_.cancelTooltip = actions_.tooltip("clip.cancelBounces");
    transport_ = std::make_unique<TransportBar>(session_, actions_);
    // The menu bar (ADR 0040): every action; its lists from the builders the right-click menus use.
    // The brand is in the title bar, except on macOS, which has none of ours.
    menus_ = std::make_unique<ShellMenus>(actions_, kMenuPlatform);
    menus_->fillList = [this](const std::string& list) {
        if (list == "recent")
            return recentMenu();
        if (list == "countIn")
            return transport_->countInMenu();
        if (list == "midiLoop")
            return transport_->midiLoopMenu();
        if (list == "trackColour")
            return trackColourMenu();
        if (list == "clipColour")
            return clipColourMenu();
        return juce::PopupMenu();
    };
    transport_->setBrandShown(kMenuPlatform == model::MenuPlatform::Mac);
    // The capture button says where it will go (recording.md §9).
    transport_->captureTooltip = [this] {
        const auto selected = selectedTrackInfo();
        const auto target = session_.captureTarget(selected ? std::optional(selected->id) : std::nullopt);
        juce::String where = "a new track";
        if (target.tracks.size() == 1)
            if (const auto* t = session_.snapshot()->channel(target.tracks.front()))
                where = juce::String::fromUTF8(t->name.c_str());
        if (target.tracks.size() > 1)
            where = juce::String(static_cast<int>(target.tracks.size())) + " armed tracks";
        return "Capture MIDI to " + where + " " + actions_.tooltip("transport.capture").fromFirstOccurrenceOf("(", true, false);
    };
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
    arrangement_.onLaneActivated = [this] { lastArea_ = Area::Lane; };
    // Rows are made by right-click too (daw-shell.md): every row header and the empty space.
    arrangement_.onAddMenu = [this](juce::PopupMenu& menu) {
        actions_.addMenuItem(menu, "track.add");
        actions_.addMenuItem(menu, "automation.addLane");
        actions_.addMenuItem(menu, "track.addReturn");
    };
    mixer_.onAddMenu = [this](juce::PopupMenu& menu) {
        actions_.addMenuItem(menu, "track.add");
        actions_.addMenuItem(menu, "track.addReturn");
    };
    // A track's or return's own header items (tracks-and-clips.md §10).
    arrangement_.onHeaderMenu = [this](juce::PopupMenu& menu, std::uint64_t) {
        actions_.addMenuItem(menu, "edit.rename");
        actions_.addMenuItem(menu, "track.duplicate");
        actions_.addMenuItem(menu, "track.delete");
        menu.addSubMenu("Colour", trackColourMenu());
    };
    // A clip's menu (bounce.md §10): bounce and mute, then the edits; while bouncing, cancel first.
    arrangement_.onClipMenu = [this](juce::PopupMenu& menu) {
        if (!session_.bouncing().empty())
        {
            actions_.addMenuItem(menu, "clip.cancelBounces");
            menu.addSeparator();
        }
        actions_.addMenuItem(menu, "clip.bounce");
        actions_.addMenuItem(menu, "clip.bounceToTrack");
        actions_.addMenuItem(menu, "clip.mute");
        actions_.addMenuItem(menu, "clip.crossfade");
        menu.addSeparator();
        actions_.addMenuItem(menu, "edit.split");
        actions_.addMenuItem(menu, "edit.duplicate");
        actions_.addMenuItem(menu, "edit.delete");
        menu.addSeparator();
        actions_.addMenuItem(menu, "edit.cut");
        actions_.addMenuItem(menu, "edit.copy");
        actions_.addMenuItem(menu, "edit.paste");
        menu.addSeparator();
        menu.addSubMenu("Colour", clipColourMenu());
    };
    actions_.onRefused = [this](const juce::String& why) { showWarning(why); };
    // "Show automation lane" on any control: the lane, in the arrangement.
    session_.onLaneRevealed = [this](std::uint64_t lane) {
        if (mixerShown_)
            setMixerShown(false);
        arrangement_.setLanesVisible(true);
        lastArea_ = Area::Lane;
        arrangement_.selectLane(lane);
    };
    lanePicker_.onChosen = [this](std::uint64_t lane) {
        arrangement_.setLanesVisible(true);
        lastArea_ = Area::Lane;
        arrangement_.selectLane(lane);
    };
    arrangement_.onAssignLane = [this](std::uint64_t lane) { assignToLane(lane); };
    arrangement_.onRenameTake = [this](std::uint64_t track, std::uint64_t take) {
        // The inspector's name field, for the take.
        lastArea_ = Area::Arrangement;
        arrangement_.selectTake(track, take);
        if (!inspectorPanel_.open)
            toggle(inspectorPanel_);
        inspector_.focusTakeName();
    };
    arrangement_.onRenameLane = [this](std::uint64_t lane) {
        // The inspector's name field, for the lane.
        lastArea_ = Area::Lane;
        arrangement_.selectLane(lane);
        if (!inspectorPanel_.open)
            toggle(inspectorPanel_);
        inspector_.focusLaneName();
    };
    inspector_.assignments().onAssign = [this](std::uint64_t lane) { assignToLane(lane); };
    inspector_.assignments().onShowParameter = [this](std::uint64_t channel, std::uint64_t module) {
        laneDetailChannel_ = channel;
        updateTitle(); // the detail panel shows that channel's chain
        if (module != 0)
        {
            if (!detailPanel_.open)
                toggle(detailPanel_);
            detail_.selectModule(module);
        }
    };
    session_.automationMenu = [this](const engine::Session::ParamTarget& target, juce::PopupMenu& menu) {
        addAutomationItems(session_, target, menu);
    };
    // A pass's new clips are selected, so the inspector shows them (recording.md §5).
    session_.onRecorded = [this](const std::vector<std::uint64_t>& clips) {
        session_.updateSnapshotNow();
        lastArea_ = Area::Arrangement;
        arrangement_.setSelection(model::withClips(*session_.snapshot(), {clips.begin(), clips.end()},
                                                   arrangement_.selection().channel, arrangement_.selection().channel));
    };
    arrangement_.onMessage = [this](const juce::String& m) { showWarning(m); };
    detail_.chain().onActivated = [this] { lastArea_ = Area::Detail; };
    detail_.pianoRoll().onActivated = [this] { lastArea_ = Area::Clip; };
    // The notes' menu (menu-bar.md §8): quantise, then the edits.
    detail_.pianoRoll().onMenu = [this] {
        juce::PopupMenu menu;
        actions_.addMenuItem(menu, "notes.quantise");
        menu.addSeparator();
        actions_.addMenuItem(menu, "edit.cut");
        actions_.addMenuItem(menu, "edit.copy");
        actions_.addMenuItem(menu, "edit.paste");
        menu.addSeparator();
        actions_.addMenuItem(menu, "edit.duplicate");
        actions_.addMenuItem(menu, "edit.delete");
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&detail_.pianoRoll()).withMousePosition());
    };
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
    releaseComputerKeyboard();
    if (assistants_ != nullptr)
        assistants_->onStatusChanged = nullptr;
    session_.onSnapshotChanged = nullptr;
    session_.onProjectWillChange = nullptr;
    session_.onProjectChanged = nullptr;
    session_.viewStateProvider = nullptr;
    session_.onLaneRevealed = nullptr;
    session_.automationMenu = nullptr;
    session_.onRecorded = nullptr;
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
    // Recording (C5, docs/design/recording.md §5, §11).
    actions_.add({.id = "transport.record", .name = "Record", .category = "Transport",
                  .keys = {key('r', kCmd), juce::KeyPress(juce::KeyPress::F9Key)},
                  .perform = [&s] { s.record(); }, .isToggled = [&s] { return s.isRecording(); }});
    actions_.add({.id = "track.arm", .name = "Arm selected track", .category = "Track", .keys = {key('r', kAlt)},
                  .perform = [this, &s] {
                      if (const auto t = selectedTrackInfo(); t && t->holdsClips())
                          s.setArmed(t->id, !t->input.armed, true);
                  },
                  .isEnabled = [this] { return selectedTrackInfo() && selectedTrackInfo()->holdsClips(); }});
    actions_.add({.id = "transport.countIn", .name = "Count-in", .category = "Transport", .keys = {key('c', kCmd | kAlt)},
                  .perform = [this, &s] {
                      // Off, or back to the last length (one bar at first).
                      if (s.countIn() == model::CountIn::Off)
                          s.setCountIn(lastCountIn_);
                      else
                      {
                          lastCountIn_ = s.countIn();
                          s.setCountIn(model::CountIn::Off);
                      }
                  },
                  .isToggled = [&s] { return s.countIn() != model::CountIn::Off; }});
    actions_.add({.id = "transport.capture", .name = "Capture MIDI", .category = "Transport",
                  .keys = {key('r', kCmd | kShift)},
                  .perform = [this, &s] {
                      // What was just played, as a clip, selected (recording.md §8): onto the
                      // armed MIDI tracks, else the selected track, else a new one.
                      const auto selected = selectedTrackInfo();
                      if (const auto clip = s.captureMidi(selected ? std::optional(selected->id) : std::nullopt); clip != 0)
                      {
                          s.updateSnapshotNow();
                          lastArea_ = Area::Arrangement;
                          const auto found = model::findClip(*s.snapshot(), clip);
                          const auto track = found ? std::optional(s.snapshot()->tracks[static_cast<size_t>(found->second)].id)
                                                   : std::nullopt;
                          arrangement_.setSelection(model::withClips(*s.snapshot(), {clip}, track, track));
                      }
                      else
                          showWarning("Nothing played to capture");
                  },
                  .isEnabled = [&s] { return s.canCaptureMidi(); }});
    // The clip whose takes the keys step through: the selected one, or the one under the playhead
    // on the selected track; only a clip with two takes or more (ADR 0032).
    struct TakesClip
    {
        std::uint64_t clip = 0, stack = 0, track = 0;
    };
    auto takesClip = [this, &s]() -> std::optional<TakesClip> {
        const auto snapshot = s.snapshot();
        auto of = [&](const model::TrackInfo& t, const model::ClipInfo& c) -> std::optional<TakesClip> {
            if (const auto* stack = c.stack != 0 ? t.stack(c.stack) : nullptr; stack != nullptr && model::hasTakesToShow(*stack))
                return TakesClip{c.id, stack->id, t.id};
            return std::nullopt;
        };
        for (const auto id : arrangement_.selection().clips)
            if (const auto found = model::findClip(*snapshot, id))
                if (const auto c = of(snapshot->tracks[static_cast<size_t>(found->second)], *found->first))
                    return c;
        if (const auto t = selectedTrackInfo(); t && t->holdsClips())
            for (const auto& c : t->clips)
                if (c.start <= s.position() && c.end() > s.position())
                    return of(*t, c);
        return std::nullopt;
    };
    auto stepTake = [this, &s, takesClip](int direction) {
        return [this, &s, takesClip, direction] {
            const auto target = takesClip();
            if (!target || !s.stepTake(target->clip, direction, s.position())) // the section at the playhead
            {
                showWarning("No other take covers this clip");
                return;
            }
            // The clip stays selected (now playing another take), for the next step.
            s.updateSnapshotNow();
            std::set<std::uint64_t> clips;
            if (const auto* t = s.snapshot()->channel(target->track))
                if (const auto* stack = t->stack(target->stack))
                    clips.insert(stack->parts.begin(), stack->parts.end());
            lastArea_ = Area::Arrangement;
            arrangement_.setSelection(model::withClips(*s.snapshot(), std::move(clips), target->track, target->track));
        };
    };
    auto canStepTake = [takesClip] { return takesClip().has_value(); };
    actions_.add({.id = "view.takes", .name = "Show takes of the selected clip", .category = "View",
                  .keys = {key('k', kCmd | kAlt)},
                  .perform = [&s, takesClip] {
                      if (const auto c = takesClip())
                          if (const auto* t = s.snapshot()->channel(c->track))
                              if (const auto* stack = t->stack(c->stack))
                                  s.setTakesOpen(stack->id, !stack->open);
                  },
                  .isEnabled = canStepTake,
                  .isToggled = [&s, takesClip] {
                      const auto c = takesClip();
                      const auto* t = c ? s.snapshot()->channel(c->track) : nullptr;
                      const auto* stack = t != nullptr ? t->stack(c->stack) : nullptr;
                      return stack != nullptr && stack->open;
                  }});
    actions_.add({.id = "take.spread", .name = "Spread the selected clip's takes to new tracks", .category = "Track",
                  .keys = {key('k', kCmd | kAlt | kShift)},
                  .perform = [this, &s, takesClip] {
                      const auto c = takesClip();
                      const auto* t = c ? s.snapshot()->channel(c->track) : nullptr;
                      const auto* stack = t != nullptr ? t->stack(c->stack) : nullptr;
                      if (stack == nullptr)
                          return;
                      std::vector<std::uint64_t> takes;
                      for (const auto& k : stack->takes)
                          takes.push_back(k.id);
                      if (const auto made = s.takesToTracks(c->track, takes, false); !made.empty())
                      {
                          s.updateSnapshotNow();
                          arrangement_.selectTrack(made.front());
                      }
                  },
                  .isEnabled = canStepTake});
    struct TakeTarget
    {
        std::uint64_t track = 0;
        std::vector<std::uint64_t> takes;
    };
    // The takes they act on: the selected ones only (menu-bar.md §15).
    auto takeTarget = [this]() -> std::optional<TakeTarget> {
        const auto& sel = arrangement_.selection();
        if (!sel.takes.empty() && sel.channel)
            return TakeTarget{*sel.channel, {sel.takes.begin(), sel.takes.end()}};
        return std::nullopt;
    };
    auto oneTake = [takeTarget] {
        const auto t = takeTarget();
        return t && t->takes.size() == 1;
    };
    auto someTakes = [takeTarget] { return takeTarget().has_value(); };
    auto takesToTracks = [this, &s, takeTarget](bool move) {
        return [this, &s, takeTarget, move] {
            if (const auto t = takeTarget())
                if (const auto made = s.takesToTracks(t->track, t->takes, move); !made.empty())
                {
                    s.updateSnapshotNow();
                    arrangement_.selectTrack(made.front());
                }
        };
    };
    actions_.add({.id = "take.useWhole", .name = "Use this take for the whole clip", .category = "Track",
                  .perform = [&s, takeTarget] {
                      if (const auto t = takeTarget())
                          s.useTake(t->track, t->takes.front());
                  },
                  .isEnabled = oneTake});
    actions_.add({.id = "take.rename", .name = asks("Rename take"), .category = "Track",
                  .perform = [this, takeTarget] {
                      if (const auto t = takeTarget(); t && arrangement_.onRenameTake)
                          arrangement_.onRenameTake(t->track, t->takes.front());
                  },
                  .isEnabled = oneTake});
    actions_.add({.id = "take.copyToTrack", .name = "Copy take to new track", .category = "Track",
                  .perform = takesToTracks(false), .isEnabled = someTakes});
    actions_.add({.id = "take.moveToTrack", .name = "Move take to new track", .category = "Track",
                  .perform = takesToTracks(true), .isEnabled = someTakes});
    actions_.add({.id = "take.keepOnly", .name = "Keep only this take", .category = "Track",
                  .perform = [&s, takeTarget] {
                      if (const auto t = takeTarget())
                          s.keepOnlyTake(t->track, t->takes.front());
                  },
                  .isEnabled = oneTake});
    actions_.add({.id = "take.previous", .name = "Previous take", .category = "Track", .keys = {key(juce::KeyPress::upKey, kAlt)},
                  .perform = stepTake(-1), .isEnabled = canStepTake});
    actions_.add({.id = "take.next", .name = "Next take", .category = "Track", .keys = {key(juce::KeyPress::downKey, kAlt)},
                  .perform = stepTake(1), .isEnabled = canStepTake});
    actions_.add({.id = "view.follow", .name = "Follow playhead", .category = "View", .keys = {key('f', kCmd | kShift)},
                  .perform = [this] { arrangement_.setFollowEnabled(!arrangement_.isFollowEnabled()); },
                  .isToggled = [this] { return arrangement_.isFollowEnabled(); }});
    actions_.add({.id = "edit.undo", .name = "Undo", .category = "Edit", .keys = {key('z', kCmd)},
                  .perform = [&s] { s.undo(); }, .isEnabled = [&s] { return s.canUndo(); },
                  .label = [&s] {
                      const auto step = s.canUndo() ? s.undoDescription() : std::string();
                      return step.empty() ? juce::String("Undo") : "Undo " + juce::String::fromUTF8(step.c_str());
                  }});
    actions_.add({.id = "edit.redo", .name = "Redo", .category = "Edit", .keys = {key('z', kCmd | kShift), key('y', kCmd)},
                  .perform = [&s] { s.redo(); }, .isEnabled = [&s] { return s.canRedo(); },
                  .label = [&s] {
                      const auto step = s.canRedo() ? s.redoDescription() : std::string();
                      return step.empty() ? juce::String("Redo") : "Redo " + juce::String::fromUTF8(step.c_str());
                  }});
    // Delete, Duplicate and Select all act on the selection in the area used last: notes in the
    // piano roll, a device in the chain, a lane's points (or the lane), else clips. Clips and
    // lanes only while the arrangement shows them (selection.md §6, automation.md §9).
    auto& roll = detail_.pianoRoll();
    auto clipsShown = [this] { return !mixerShown_; };
    actions_.add({.id = "edit.delete", .name = "Delete", .category = "Edit",
                  .keys = {key(juce::KeyPress::deleteKey), key(juce::KeyPress::backspaceKey)},
                  .perform = [this, &roll, clipsShown] {
                      if (notesActive() && roll.hasSelection())
                          roll.deleteSelection();
                      else if (lastArea_ == Area::Detail && detail_.chain().hasSelection())
                          detail_.chain().deleteSelected();
                      else if (laneActive() && arrangement_.hasPointSelection())
                          arrangement_.deletePoints();
                      else if (laneActive())
                          arrangement_.removeSelectedLane();
                      else if (trackHeaderActive()) // its header (tracks-and-clips.md §5)
                          deleteChannel(*commandChannel());
                      else if (clipsShown())
                          arrangement_.deleteSelection();
                  },
                  .isEnabled = [this, &roll, clipsShown] {
                      return (notesActive() && roll.hasSelection()) ||
                             (lastArea_ == Area::Detail && detail_.chain().hasSelection()) || laneActive() || trackHeaderActive() ||
                             (clipsShown() && (arrangement_.hasClipSelection() || arrangement_.hasTakeSelection()));
                  }});
    actions_.add({.id = "edit.duplicate", .name = "Duplicate", .category = "Edit", .keys = {key('d', kCmd)},
                  .perform = [this, &roll, clipsShown] {
                      if (notesActive() && roll.hasSelection())
                          roll.duplicateSelection();
                      else if (pointsActive())
                          arrangement_.duplicatePoints();
                      else if (trackHeaderActive()) // its header (tracks-and-clips.md §7)
                          duplicateChannel(*commandChannel());
                      else if (clipsShown())
                          arrangement_.duplicateSelection();
                  },
                  .isEnabled = [this, &roll, clipsShown] {
                      return (notesActive() && roll.hasSelection()) || pointsActive() || trackHeaderActive() ||
                             (clipsShown() && arrangement_.hasClipSelection());
                  }});
    // Cut, copy and paste (tracks-and-clips.md §9): clips, notes or points, from the area used
    // last; pasted at the playhead.
    actions_.add({.id = "edit.cut", .name = "Cut", .category = "Edit", .keys = {key('x', kCmd)},
                  .perform = [this, &roll] {
                      const bool notes = notesActive() && roll.hasSelection(), points = pointsActive();
                      if (!copySelection())
                          return;
                      const engine::Session::EditGroup group(session_, "Cut");
                      if (notes)
                          roll.deleteSelection();
                      else if (points)
                          arrangement_.deletePoints();
                      else
                          arrangement_.deleteSelection();
                  },
                  .isEnabled = [this] { return canCopy(); }});
    actions_.add({.id = "edit.copy", .name = "Copy", .category = "Edit", .keys = {key('c', kCmd)},
                  .perform = [this] { copySelection(); }, .isEnabled = [this] { return canCopy(); }});
    actions_.add({.id = "edit.paste", .name = "Paste", .category = "Edit", .keys = {key('v', kCmd)},
                  .perform = [this] { paste(); }, .isEnabled = [this] { return held_ != Held::None; },
                  .whyNot = [] { return juce::String("Nothing to paste yet: copy or cut something first"); }});
    // F2: renames what's selected where its name is (tracks-and-clips.md §4).
    actions_.add({.id = "edit.rename", .name = asks("Rename"), .category = "Edit", .keys = {juce::KeyPress(juce::KeyPress::F2Key)},
                  .perform = [this] {
                      const auto& sel = arrangement_.selection();
                      if (mixerShown_ && sel.channel)
                          mixer_.renameChannel(*sel.channel);
                      else if (!mixerShown_)
                          arrangement_.renameSelection();
                  },
                  .isEnabled = [this] {
                      const auto& sel = arrangement_.selection();
                      if (!sel.clips.empty() || sel.lane)
                          return !mixerShown_;
                      return commandChannel().has_value();
                  }});
    // The selected track's or return's own commands (Delete and Ctrl+D do these from its header).
    auto channelCommand = [this] { return commandChannel().has_value() && !session_.isRecording(); };
    actions_.add({.id = "track.duplicate", .name = "Duplicate track", .category = "Track",
                  .perform = [this] {
                      if (const auto c = commandChannel())
                          duplicateChannel(*c);
                  },
                  .isEnabled = channelCommand});
    actions_.add({.id = "track.delete", .name = "Delete track", .category = "Track",
                  .perform = [this] {
                      if (const auto c = commandChannel())
                          deleteChannel(*c);
                  },
                  .isEnabled = channelCommand,
                  .whyNot = [this] {
                      return session_.isRecording() ? juce::String("Stop recording first: tracks can't be deleted while a pass records")
                                                    : juce::String();
                  }});
    actions_.add({.id = "edit.split", .name = "Split at playhead", .category = "Edit", .keys = {key('e', kCmd)},
                  .perform = [this] { arrangement_.splitAtPlayhead(); }, .isEnabled = clipsShown});
    actions_.add({.id = "edit.selectAll", .name = "Select all", .category = "Edit", .keys = {key('a', kCmd)},
                  .perform = [this, &roll, clipsShown] {
                      if (notesActive())
                          roll.selectAll();
                      else if (laneActive())
                          arrangement_.selectAllPoints();
                      else if (clipsShown())
                          arrangement_.selectAllClips();
                  },
                  .isEnabled = [this, clipsShown] { return notesActive() || clipsShown(); }});

    // The arrows: the piano roll's notes (C2), or a lane's points (C4), whichever was used last.
    auto notesSelected = [this, &roll] { return (notesActive() && roll.hasSelection()) || pointsActive(); };
    auto arrow = [this, &roll](int semitones, int steps, float value) {
        return [this, &roll, semitones, steps, value] {
            if (pointsActive())
                arrangement_.nudgePoints(steps, value);
            else if (semitones != 0)
                roll.transpose(semitones);
            else
                roll.nudge(steps);
        };
    };
    actions_.add({.id = "notes.up", .name = "Transpose up / raise points", .category = "Notes",
                  .keys = {key(juce::KeyPress::upKey)}, .perform = arrow(1, 0, 0.01f), .isEnabled = notesSelected});
    actions_.add({.id = "notes.down", .name = "Transpose down / lower points", .category = "Notes",
                  .keys = {key(juce::KeyPress::downKey)}, .perform = arrow(-1, 0, -0.01f), .isEnabled = notesSelected});
    actions_.add({.id = "notes.octaveUp", .name = "Transpose up an octave / raise points a little", .category = "Notes",
                  .keys = {key(juce::KeyPress::upKey, kShift)}, .perform = arrow(12, 0, 0.001f), .isEnabled = notesSelected});
    actions_.add({.id = "notes.octaveDown", .name = "Transpose down an octave / lower points a little", .category = "Notes",
                  .keys = {key(juce::KeyPress::downKey, kShift)}, .perform = arrow(-12, 0, -0.001f),
                  .isEnabled = notesSelected});
    actions_.add({.id = "notes.later", .name = "Nudge later", .category = "Notes", .keys = {key(juce::KeyPress::rightKey)},
                  .perform = arrow(0, 1, 0.0f), .isEnabled = notesSelected});
    actions_.add({.id = "notes.earlier", .name = "Nudge earlier", .category = "Notes", .keys = {key(juce::KeyPress::leftKey)},
                  .perform = arrow(0, -1, 0.0f), .isEnabled = notesSelected});
    actions_.add({.id = "notes.quantise", .name = "Quantise", .category = "Notes", .keys = {key('u', kCmd)},
                  .perform = [&roll] { roll.quantise(); },
                  .isEnabled = [this, &roll] { return detail_.showsPianoRoll() && !roll.notes().empty(); }});
    actions_.add({.id = "clip.newMidi", .name = "New MIDI clip", .category = "Clip", .keys = {key('m', kCmd | kShift)},
                  .perform = [this] { arrangement_.createMidiClipAtPlayhead(); },
                  .isEnabled = [this] { return !mixerShown_ && !session_.snapshot()->tracks.empty(); }});

    // Bounce (C7, ADR 0035, docs/design/bounce.md): the selected clips, offline; the result lands
    // as one undo step and is selected. Clips that can't be bounced are left out; when none can,
    // the shortcut says why.
    auto bounceable = [this](model::BounceMode mode) {
        std::pair<std::vector<std::uint64_t>, std::string> out;
        const auto& selected = arrangement_.selection().clips;
        for (const auto& t : session_.snapshot()->tracks)
            for (const auto& c : t.clips)
                if (selected.contains(c.id))
                {
                    if (const auto refusal = model::bounceRefusal(t, c, mode); refusal == model::BounceRefusal::None)
                        out.first.push_back(c.id);
                    else if (out.second.empty())
                        out.second = model::refusalText(refusal);
                }
        return out;
    };
    auto bounce = [this, bounceable](model::BounceMode mode) {
        const auto clips = bounceable(mode).first;
        session_.bounce(clips, mode, [this](const engine::Session::BounceResult& r) {
            if (r.cancelled)
                return;
            if (!r.error.empty())
            {
                showWarning(juce::String::fromUTF8(r.error.c_str()));
                return;
            }
            session_.updateSnapshotNow();
            arrangement_.setSelection(model::withClips(*session_.snapshot(), {r.clips.begin(), r.clips.end()}, std::nullopt,
                                                       std::nullopt));
            if (!r.cutTails.empty())
                showWarning(juce::String::fromUTF8((r.cutTails.front().clip + "'s tail stops where " + r.cutTails.front().next +
                                                    " starts. Bounce both to keep it.")
                                                       .c_str()));
        });
    };
    auto canBounce = [this, clipsShown, bounceable](model::BounceMode mode) {
        return clipsShown() && arrangement_.hasClipSelection() && !bounceable(mode).first.empty();
    };
    auto whyNotBounce = [this, clipsShown, bounceable](model::BounceMode mode) {
        return clipsShown() && arrangement_.hasClipSelection() ? juce::String::fromUTF8(bounceable(mode).second.c_str())
                                                                : juce::String();
    };
    actions_.add({.id = "clip.bounce", .name = "Bounce in place", .category = "Clip", .keys = {key('b', kCmd)},
                  .perform = [bounce] { bounce(model::BounceMode::InPlace); },
                  .isEnabled = [canBounce] { return canBounce(model::BounceMode::InPlace); },
                  .whyNot = [whyNotBounce] { return whyNotBounce(model::BounceMode::InPlace); }});
    actions_.add({.id = "clip.bounceToTrack", .name = "Bounce to new track", .category = "Clip",
                  .keys = {key('b', kCmd | kShift)},
                  .perform = [bounce] { bounce(model::BounceMode::NewTrack); },
                  .isEnabled = [canBounce] { return canBounce(model::BounceMode::NewTrack); },
                  .whyNot = [whyNotBounce] { return whyNotBounce(model::BounceMode::NewTrack); }});
    actions_.add({.id = "clip.cancelBounces", .name = "Cancel bounces", .category = "Clip",
                  .keys = {key(juce::KeyPress::escapeKey)}, .perform = [this] { session_.cancelBounces(); },
                  .isEnabled = [this] { return !session_.bouncing().empty(); }});
    // Clip mute (bounce.md §6): every selected clip, on unless all of them are muted already.
    auto allMuted = [this] {
        const auto& selected = arrangement_.selection().clips;
        bool any = false;
        for (const auto& t : session_.snapshot()->tracks)
            for (const auto& c : t.clips)
                if (selected.contains(c.id))
                {
                    if (!c.muted)
                        return false;
                    any = true;
                }
        return any;
    };
    actions_.add({.id = "clip.mute", .name = "Mute clips", .category = "Clip", .keys = {key('0')},
                  .perform = [this, allMuted] {
                      const auto& selected = arrangement_.selection().clips;
                      session_.setClipsMuted({selected.begin(), selected.end()}, !allMuted());
                  },
                  .isEnabled = [this, clipsShown] { return clipsShown() && arrangement_.hasClipSelection(); },
                  .isToggled = allMuted});

    // Crossfades (C6, fades.md §4): every join between the selected clips that meet; a join that
    // can't take one says why.
    actions_.add({.id = "clip.crossfade", .name = "Crossfade", .category = "Clip", .keys = {key('x', kCmd | kShift)},
                  .perform = [this] {
                      const auto& selected = arrangement_.selection().clips;
                      const auto results = session_.crossfade({selected.begin(), selected.end()});
                      if (results.empty())
                          showWarning("Select audio clips that meet to crossfade them.");
                      for (const auto& r : results)
                          if (!r.crossfade)
                          {
                              showWarning(juce::String::fromUTF8(r.why.c_str()));
                              break;
                          }
                  },
                  .isEnabled = [this, clipsShown] { return clipsShown() && arrangement_.selection().clips.size() >= 2; },
                  .whyNot = [this, clipsShown] {
                      return clipsShown() && arrangement_.hasClipSelection() ? juce::String("Select the clips on both sides of a join.")
                                                                             : juce::String();
                  }});
    actions_.add({.id = "project.export", .name = asks("Export"), .category = "Project", .keys = {key('e', kCmd | kShift)},
                  .perform = [this] { exportPanel_.isOpen() ? exportPanel_.close() : exportPanel_.open(*this); }});
    actions_.add({.id = "project.edgeFades", .name = "Edge fades", .category = "Project", .keys = {key('e', kCmd | kAlt | kShift)},
                  .perform = [this] { session_.setEdgeFades(!session_.edgeFades()); },
                  .isToggled = [this] { return session_.edgeFades(); }});
    actions_.add({.id = "midi.computerKeyboard", .name = "Computer keyboard as MIDI", .category = "MIDI",
                  .keys = {key('k', kCmd | kShift)},
                  .perform = [this] {
                      computerKeyboard_ = !computerKeyboard_;
                      releaseComputerKeyboard();
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
    auto keyboardOn = [this] { return computerKeyboard_; };
    actions_.add({.id = "midi.octaveDown", .name = "Octave down", .category = "Computer keyboard", .keys = {key('z')},
                  .perform = [this] { shiftKeyboardOctave(-1); }, .isEnabled = keyboardOn});
    actions_.add({.id = "midi.octaveUp", .name = "Octave up", .category = "Computer keyboard", .keys = {key('x')},
                  .perform = [this] { shiftKeyboardOctave(1); }, .isEnabled = keyboardOn});
    actions_.add({.id = "midi.velocityDown", .name = "Velocity down", .category = "Computer keyboard", .keys = {key('c')},
                  .perform = [this] { shiftKeyboardVelocity(-1); }, .isEnabled = keyboardOn});
    actions_.add({.id = "midi.velocityUp", .name = "Velocity up", .category = "Computer keyboard", .keys = {key('v')},
                  .perform = [this] { shiftKeyboardVelocity(1); }, .isEnabled = keyboardOn});
    actions_.add({.id = "module.fold", .name = "Fold module", .category = "Module", .keys = {key('m', kCmd | kAlt)},
                  .perform = [this] { detail_.chain().foldSelected(); },
                  .isEnabled = [this] { return detail_.chain().selectedPanel() != nullptr; }});
    actions_.add({.id = "module.expand", .name = "Expand module sections", .category = "Module",
                  .keys = {key('e', kCmd | kAlt)},
                  .perform = [this] { detail_.chain().expandSelected(); },
                  .isEnabled = [this] { return detail_.chain().selectedPanel() != nullptr; }});
    // Presets (ADR 0044, presets.md §10): the selected module's, as its header has them.
    auto presets = [this]() -> ui::PresetBar*
    {
        auto* panel = detail_.chain().selectedPanel();
        return panel != nullptr ? panel->presetBar() : nullptr;
    };
    auto onPresets = [presets](std::function<void(ui::PresetBar&)> f)
    {
        return [presets, f]
        {
            if (auto* bar = presets())
            {
                f(*bar);
                bar->refresh();
            }
        };
    };
    auto hasPresets = [presets] { return presets() != nullptr; };
    actions_.add({.id = "module.previousPreset",
                  .name = "Previous preset",
                  .category = "Module",
                  .perform = onPresets([](ui::PresetBar& b) { b.step(-1); }),
                  .isEnabled = hasPresets});
    actions_.add({.id = "module.nextPreset",
                  .name = "Next preset",
                  .category = "Module",
                  .perform = onPresets([](ui::PresetBar& b) { b.step(+1); }),
                  .isEnabled = hasPresets});
    actions_.add({.id = "module.savePreset",
                  .name = "Save preset",
                  .category = "Module",
                  .perform = onPresets(
                      [](ui::PresetBar& b)
                      {
                          if (b.controller().canSave())
                              b.controller().save();
                          else
                              b.openList(true); // a factory preset: one of yours
                      }),
                  .isEnabled = hasPresets});
    actions_.add({.id = "module.savePresetAs",
                  .name = juce::String::fromUTF8("Save preset as\xE2\x80\xA6"),
                  .category = "Module",
                  .perform = onPresets([](ui::PresetBar& b) { b.openList(true); }),
                  .isEnabled = hasPresets});
    actions_.add({.id = "module.revertPreset",
                  .name = "Revert to preset",
                  .category = "Module",
                  .perform = onPresets([](ui::PresetBar& b) { b.controller().revert(); }),
                  .isEnabled = [presets]
                  {
                      auto* bar = presets();
                      return bar != nullptr && bar->controller().modified();
                  }});
    actions_.add({.id = "module.compare",
                  .name = "Compare A/B",
                  .category = "Module",
                  .perform = onPresets([](ui::PresetBar& b) { b.controller().switchCompare(); }),
                  .isEnabled = hasPresets,
                  .isToggled = [presets]
                  {
                      auto* bar = presets();
                      return bar != nullptr && bar->controller().onB();
                  }});
    actions_.add({.id = "module.copyCompare",
                  .name = "Copy A to B",
                  .category = "Module",
                  .perform = onPresets([](ui::PresetBar& b) { b.controller().copyCompare(); }),
                  .isEnabled = hasPresets,
                  .label = [presets]
                  {
                      auto* bar = presets();
                      return juce::String(bar != nullptr && bar->controller().onB() ? "Copy B to A"
                                                                                    : "Copy A to B");
                  }});
    actions_.add({.id = "module.useAsDefault",
                  .name = "Use as the default for a new module",
                  .category = "Module",
                  .perform = onPresets([](ui::PresetBar& b) { b.controller().useAsDefault(); }),
                  .isEnabled = hasPresets,
                  .label = [presets]
                  {
                      auto* bar = presets();
                      return bar != nullptr ? "Use as the default for a new " +
                                                  juce::String(std::string(bar->context().info.name))
                                            : juce::String("Use as the default for a new module");
                  }});
    actions_.add({.id = "module.clearDefault",
                  .name = "Clear the default",
                  .category = "Module",
                  .perform = onPresets([](ui::PresetBar& b) { b.controller().clearDefault(); }),
                  .isEnabled = [presets]
                  {
                      auto* bar = presets();
                      return bar != nullptr && bar->controller().hasDefault();
                  }});
    actions_.add({.id = "module.presetsFolder",
                  .name = "Show presets folder",
                  .category = "Module",
                  .perform = [&s]
                  {
                      const juce::File folder(
                          juce::String::fromUTF8(s.presetLibrary().root().string().c_str()));
                      if (!folder.isDirectory())
                          folder.createDirectory();
                      folder.startAsProcess();
                  }});
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
    // Automation lanes (C4, docs/design/automation.md §3, §7, §8, §10).
    actions_.add({.id = "automation.arm", .name = "Automation arm", .category = "Automation", .keys = {key('a', kCmd | kShift)},
                  .perform = [&s] { s.setAutomationArmed(!s.isAutomationArmed()); },
                  .isToggled = [&s] { return s.isAutomationArmed(); }});
    actions_.add({.id = "automation.reEnable", .name = "Turn automation back on", .category = "Automation",
                  .keys = {key('a', kCmd | kAlt)}, .perform = [&s] { s.setAllLanesOn(); },
                  .isEnabled = [&s] { return s.anyLaneOff(); }, .isToggled = [&s] { return s.anyLaneOff(); }});
    actions_.add({.id = "automation.addLane", .name = "Add automation lane", .category = "Automation",
                  .keys = {key('l', kCmd | kShift)},
                  .perform = [&s] {
                      // Like a new track: empty, at the end of the tracks, its own colour; selected,
                      // so its parameters are assigned next (automation.md §3).
                      if (const auto lane = s.addAutomationLane())
                          s.revealLane(lane);
                  },
                  .isEnabled = arrangeShown});
    actions_.add({.id = "automation.assign", .name = asks("Assign parameter to the selected lane"), .category = "Automation",
                  .keys = {key('l', kCmd | kAlt | kShift)},
                  .perform = [this] {
                      if (const auto lane = arrangement_.selection().lane)
                          assignToLane(*lane);
                  },
                  .isEnabled = [this, arrangeShown] { return arrangeShown() && arrangement_.hasLaneSelection(); }});
    actions_.add({.id = "automation.showLastTouched", .name = "Automate last touched parameter", .category = "Automation",
                  .keys = {key('l', kAlt)},
                  .perform = [&s] {
                      // Its lane, or a new one for it.
                      if (const auto target = s.lastTouchedParameter())
                          if (const auto lane = s.laneOf(*target) ? *s.laneOf(*target) : s.automateInNewLane(*target))
                              s.revealLane(lane);
                  },
                  .isEnabled = [&s, arrangeShown] { return arrangeShown() && s.lastTouchedParameter().has_value(); }});
    actions_.add({.id = "view.lanes", .name = "Show automation lanes", .category = "View", .keys = {key('l', kCmd | kAlt)},
                  .perform = [this] { arrangement_.setLanesVisible(!arrangement_.lanesVisible()); },
                  .isEnabled = arrangeShown, .isToggled = [this] { return arrangement_.lanesVisible(); }});
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
    actions_.add({.id = "view.fullScreen", .name = "Full screen", .category = "View",
                  .keys = {juce::KeyPress(juce::KeyPress::F11Key)},
                  .perform = [this] { window.setFullScreen(!window.isFullScreen()); },
                  .isEnabled = [this] { return window.setFullScreen != nullptr && window.isFullScreen != nullptr; },
                  .isToggled = [this] { return window.isFullScreen && window.isFullScreen(); }});
    actions_.add({.id = "app.commandPalette", .name = asks("Command palette"), .category = "App", .keys = {key('k', kCmd)},
                  .perform = [this] { palette_.isOpen() ? palette_.close() : palette_.open(*this); }});
    if (assistantsPopover_ != nullptr)
        actions_.add({.id = "app.assistants",
                      .name = asks("Assistants"),
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
    actions_.add({.id = "file.open", .name = asks("Open project"), .category = "File", .keys = {key('o', kCmd)},
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
    actions_.add({.id = "file.saveAs", .name = asks("Save as"), .category = "File", .keys = {key('s', kCmd | kShift)},
                  .perform = [this] { saveProjectAs(); }});
    actions_.add({.id = "app.scanPlugins", .name = "Scan for plugins", .category = "App", .keys = {key('p', kCmd | kAlt)},
                  .perform = [this] {
                      showWarning("Scanning for plugins...");
                      session_.scanPlugins([this](const juce::String& file) { showWarning("Scanning " + juce::File(file).getFileName()); },
                                           [this](int found) { showWarning("Plugin scan finished: " + juce::String(found) + " plugins"); });
                  },
                  .isEnabled = [this] { return !session_.isScanning(); }});
    actions_.add({.id = "app.audioSettings", .name = asks("Audio settings"), .category = "App", .keys = {key(',', kCmd)},
                  .perform = [this] { showAudioSettings(); }});
    actions_.add({.id = "app.systemTitleBar", .name = "Use the system title bar", .category = "App",
                  .perform = [this] {
                      settings_.systemTitleBar = !settings_.systemTitleBar;
                      saveSettings();
                      window.setSystemTitleBar(settings_.systemTitleBar);
                  },
                  .isEnabled = [this] { return window.setSystemTitleBar != nullptr; },
                  .isToggled = [this] { return settings_.systemTitleBar; }});
    actions_.add({.id = "app.about", .name = "About Segno", .category = "Help",
                  .perform = [this] { about_.isOpen() ? about_.close() : about_.open(*this); },
                  .isToggled = [this] { return about_.isOpen(); }});
    actions_.add({.id = "app.quit", .name = "Quit", .category = "File", .keys = {key('q', kCmd)},
                  .perform = [this] { window.quit(); }, .isEnabled = [this] { return window.quit != nullptr; }});
}

bool Shell::notesActive() const
{
    return lastArea_ == Area::Clip && detail_.showsPianoRoll();
}

bool Shell::laneActive() const
{
    return lastArea_ == Area::Lane && !mixerShown_ && arrangement_.hasLaneSelection();
}

bool Shell::pointsActive() const
{
    return laneActive() && arrangement_.hasPointSelection();
}

bool Shell::keyPressed(const juce::KeyPress& k)
{
    if (dynamic_cast<juce::TextEditor*>(juce::Component::getCurrentlyFocusedComponent()) != nullptr)
        return false; // typing goes to the text field
    if (computerKeyboard_ && handleComputerKeyboard(k))
        return true;
    // Escape cancels bounces and an export in progress (bounce.md §8, export.md §7); popovers
    // take it first, being focused.
    if (k == juce::KeyPress::escapeKey)
    {
        if (arrangement_.cancelRowDrag())
            return true;
        const bool bounces = session_.cancelBounces();
        if (exportPanel_.cancel() || bounces)
            return true;
    }
    // The menu bar from the keyboard (menu-bar.md §5): F10 opens File, Alt+letter its menu.
    if (onOpenMenu)
    {
        if (k == juce::KeyPress(juce::KeyPress::F10Key))
        {
            onOpenMenu(0);
            return true;
        }
        const auto mods = k.getModifiers();
        if (mods.isAltDown() && !mods.isCommandDown() && !mods.isShiftDown() && k.getKeyCode() > 0 && k.getKeyCode() < 128)
            if (const int menu = menus_->menuForMnemonic(juce::CharacterFunctions::toLowerCase(static_cast<juce::juce_wchar>(k.getKeyCode())));
                menu >= 0)
            {
                onOpenMenu(menu);
                return true;
            }
    }
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
        shiftKeyboardOctave(c == 'x' ? 1 : -1);
        return true;
    }
    if (c == 'c' || c == 'v')
    {
        shiftKeyboardVelocity(c == 'v' ? 1 : -1);
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

void Shell::shiftKeyboardOctave(int octaves)
{
    keyboardOctave_ = juce::jlimit(-4, 4, keyboardOctave_ + octaves);
    showWarning("Computer keyboard: C" + juce::String(3 + keyboardOctave_));
}

void Shell::shiftKeyboardVelocity(int steps)
{
    keyboardVelocity_ = juce::jlimit(0.1f, 1.0f, keyboardVelocity_ + 0.1f * static_cast<float>(steps));
    showWarning("Computer keyboard velocity: " + juce::String(juce::roundToInt(keyboardVelocity_ * 127.0f)));
}

void Shell::releaseComputerKeyboard()
{
    for (const auto& [code, note] : heldKeys_)
        session_.computerKeyboardNote(note, 0.0f);
    heldKeys_.clear();
}

void Shell::focusLost(FocusChangeType)
{
    if (!hasKeyboardFocus(true))
        releaseComputerKeyboard();
}

void Shell::focusOfChildComponentChanged(FocusChangeType)
{
    if (!hasKeyboardFocus(true)) // another window took the keys, or the app is in the background
        releaseComputerKeyboard();
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
    // Beside the project's folder (Save As makes a folder of its own), or in Documents.
    auto initial = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                       .getChildFile(juce::String("Untitled") + engine::Session::fileExtension);
    if (const auto current = session_.file())
        initial = current->getParentDirectory().getFileName() == current->getFileNameWithoutExtension()
                      ? current->getParentDirectory().getSiblingFile(current->getFileName())
                      : *current;
    chooser_ = std::make_unique<juce::FileChooser>("Save project", initial, juce::String("*") + engine::Session::fileExtension);
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, next = std::move(then)](const juce::FileChooser& fc) {
                              const auto chosen = fc.getResult();
                              if (chosen == juce::File())
                                  return;
                              // A project is a folder: Name/Name.segno, with its recordings (ADR 0030).
                              const juce::File file(model::projectFileFor(chosen.getFullPathName().toStdString()).string());
                              auto save = [this, file, next] {
                                  if (!session_.saveAs(file))
                                  {
                                      showWarning("Could not save " + file.getFullPathName());
                                      return;
                                  }
                                  updateTitle();
                                  if (next)
                                      next();
                              };
                              if (file.existsAsFile() && file != session_.file())
                              {
                                  // The chooser asked about the name chosen; this is the project inside its folder.
                                  juce::AlertWindow::showAsync(
                                      juce::MessageBoxOptions()
                                          .withIconType(juce::MessageBoxIconType::QuestionIcon)
                                          .withTitle("Replace " + file.getFileNameWithoutExtension() + "?")
                                          .withMessage("A project with this name is already in " +
                                                       file.getParentDirectory().getFullPathName() + ".")
                                          .withButton("Replace")
                                          .withButton("Cancel"),
                                      [save](int button) {
                                          if (button == 1)
                                              save();
                                      });
                                  return;
                              }
                              save();
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
                          {"lanes", arrangement_.lanesVisible()},
                          {"mixerModules", mixer_.options().modules},
                          {"mixerSends", mixer_.options().sends},
                          {"mixerNarrow", mixer_.options().narrow},
                          {"mixerScroll", mixer_.scrollX()},
                          {"rowHeights", arrangement_.rowHeights()}}
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
    arrangement_.setLanesVisible(get("lanes", true));
    mixer_.setOptions(get("mixerModules", true), get("mixerSends", true), get("mixerNarrow", false));
    mixer_.setScrollX(get("mixerScroll", 0));
    arrangement_.setRowHeights(get("rowHeights", std::map<std::string, double>{}));
    resized();
}

juce::String Shell::projectName() const
{
    return session_.file() ? session_.file()->getFileNameWithoutExtension() : juce::String("Untitled");
}

bool Shell::hasUnsavedChanges() const
{
    return session_.hasUnsavedChanges();
}

void Shell::updateTitle()
{
    // A project opened or saved under a new name goes to the front of Open recent.
    if (const auto file = session_.file(); file != titleFile_)
    {
        titleFile_ = file;
        if (file)
        {
            settings_.recent.add(file->getFullPathName().toStdString());
            saveSettings();
        }
    }
    // The document first, the convention on Windows and GNOME (menu-bar.md §4).
    auto title = projectName();
    if (hasUnsavedChanges())
        title << " *";
    title << juce::String::fromUTF8(" \xE2\x80\x93 ") << ui::brand::name;
    if (title != title_)
    {
        title_ = title;
        if (onTitleChanged)
            onTitleChanged(title_);
    }
}

void Shell::setSettingsFile(const juce::File& file)
{
    settingsFile_ = file;
    settings_ = model::appSettingsFromJson(file.loadFileAsString().toStdString());
    if (titleFile_) // the project already open
        settings_.recent.add(titleFile_->getFullPathName().toStdString());
}

void Shell::saveSettings() const
{
    if (!settingsFile_)
        return;
    settingsFile_->getParentDirectory().createDirectory();
    settingsFile_->replaceWithText(juce::String::fromUTF8(model::toJson(settings_).c_str()));
}

void Shell::openFile(const juce::File& file)
{
    confirmDiscard([this, file] {
        std::string error;
        if (!session_.open(file, &error))
        {
            showWarning(juce::String::fromUTF8(error.c_str()));
            settings_.recent.remove(file.getFullPathName().toStdString());
            saveSettings();
        }
    });
}

juce::PopupMenu Shell::recentMenu()
{
    juce::PopupMenu menu;
    const auto paths = settings_.recent.existing([](const std::string& p) { return juce::File(juce::String::fromUTF8(p.c_str())).existsAsFile(); });
    if (paths.empty())
        menu.addItem(juce::PopupMenu::Item("No recent projects").setEnabled(false));
    const auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory).getFullPathName();
    for (const auto& path : paths)
    {
        const juce::File file(juce::String::fromUTF8(path.c_str()));
        // The name, and where it is (home as ~) on the right where shortcuts go.
        juce::PopupMenu::Item item(file.getFileNameWithoutExtension());
        auto folder = file.getParentDirectory().getFullPathName();
        if (folder.startsWith(home))
            folder = "~" + folder.substring(home.length());
        item.shortcutKeyDescription = folder;
        item.setAction([this, file] { openFile(file); });
        menu.addItem(std::move(item));
    }
    menu.addSeparator();
    menu.addItem(juce::PopupMenu::Item("Clear the list").setEnabled(!settings_.recent.paths.empty()).setAction([this] {
        settings_.recent.clear();
        saveSettings();
    }));
    return menu;
}

std::optional<std::uint64_t> Shell::commandChannel() const
{
    const auto& sel = arrangement_.selection();
    if (!sel.channel || sel.lane)
        return std::nullopt;
    const auto* c = session_.snapshot()->channel(*sel.channel);
    return c != nullptr && c->role != model::TrackInfo::Role::Master ? std::optional(c->id) : std::nullopt;
}

bool Shell::trackHeaderActive() const
{
    return lastArea_ == Area::Arrangement && !mixerShown_ && arrangement_.headerActive() && commandChannel().has_value();
}

void Shell::deleteChannel(std::uint64_t channel)
{
    const auto snapshot = session_.snapshot();
    const auto* c = snapshot->channel(channel);
    if (c == nullptr)
        return;
    const auto name = juce::String::fromUTF8(c->name.c_str());
    // Then the one above it is selected (the one below, for the first).
    const auto& kind = c->role == model::TrackInfo::Role::Return ? snapshot->returns : snapshot->tracks;
    std::optional<std::uint64_t> next;
    for (size_t i = 0; i < kind.size(); ++i)
        if (kind[i].id == channel)
            next = i > 0 ? std::optional(kind[i - 1].id) : i + 1 < kind.size() ? std::optional(kind[i + 1].id) : std::nullopt;
    std::string why;
    if (!session_.deleteTrack(channel, &why))
    {
        if (!why.empty())
            showWarning(juce::String::fromUTF8(why.c_str()));
        return;
    }
    session_.updateSnapshotNow();
    arrangement_.selectTrack(next);
    // Nothing asked first: what went, with Undo (tracks-and-clips.md §5).
    showToast("Deleted " + name, "Undo", [this] { session_.undo(); });
}

void Shell::duplicateChannel(std::uint64_t channel)
{
    const auto copy = session_.duplicateTrack(channel);
    if (copy == 0)
    {
        showWarning(session_.isRecording() ? "Stop recording first" : "There's no room for another return track (32 at most)");
        return;
    }
    session_.updateSnapshotNow();
    arrangement_.selectTrack(copy);
}

juce::PopupMenu Shell::trackColourMenu()
{
    juce::PopupMenu menu;
    const auto snapshot = session_.snapshot();
    if (const auto lane = arrangement_.selection().lane)
    {
        for (const auto& l : snapshot->automationLanes)
            if (l.id == *lane)
                ui::addColourGrid(menu, l.colour,
                                  [this, id = l.id](int c) { session_.setLaneColour(id, c); });
        return menu;
    }
    if (const auto c = commandChannel())
        ui::addColourGrid(menu, snapshot->channel(*c)->colour,
                          [this, id = *c](int colour) { session_.setTrackColour(id, colour); });
    else
        menu.addItem(juce::PopupMenu::Item("Select a track first").setEnabled(false));
    return menu;
}

juce::PopupMenu Shell::clipColourMenu()
{
    juce::PopupMenu menu;
    const auto snapshot = session_.snapshot();
    // The selected clips, with every part of a comp.
    std::vector<std::uint64_t> ids;
    const model::ClipInfo* first = nullptr;
    if (!mixerShown_)
        for (const auto& t : snapshot->tracks)
            for (const auto& c : t.clips)
                if (arrangement_.selection().clips.contains(c.id) ||
                    (c.stack != 0 && std::any_of(t.clips.begin(), t.clips.end(), [&](const auto& o) {
                         return o.stack == c.stack && arrangement_.selection().clips.contains(o.id);
                     })))
                {
                    ids.push_back(c.id);
                    if (first == nullptr)
                        first = &c;
                }
    if (first == nullptr)
    {
        menu.addItem(juce::PopupMenu::Item("Select clips first").setEnabled(false));
        return menu;
    }
    ui::addColourGrid(menu, first->colour, [this, ids](int c) { session_.setClipColour(ids, c); });
    return menu;
}

bool Shell::canCopy() const
{
    return (notesActive() && detail_.pianoRoll().hasSelection()) || pointsActive() ||
           (!mixerShown_ && lastArea_ != Area::Clip && arrangement_.hasClipSelection());
}

bool Shell::copySelection()
{
    auto& roll = detail_.pianoRoll();
    if (notesActive() && roll.hasSelection())
    {
        heldNotes_ = roll.copySelection();
        held_ = Held::Notes;
        return true;
    }
    if (pointsActive())
    {
        heldPoints_ = arrangement_.copyPoints();
        held_ = Held::Points;
        return true;
    }
    if (!mixerShown_ && arrangement_.hasClipSelection())
    {
        const auto& sel = arrangement_.selection().clips;
        heldClips_ = session_.copyClips({sel.begin(), sel.end()});
        held_ = heldClips_.empty() ? held_ : Held::Clips;
        return !heldClips_.empty();
    }
    return false;
}

void Shell::paste()
{
    const double at = session_.position();
    switch (held_)
    {
    case Held::None:
        return;
    case Held::Notes:
        if (!detail_.showsPianoRoll())
        {
            showWarning("Notes paste into the clip view: open a MIDI clip there first");
            return;
        }
        {
            const engine::Session::EditGroup group(session_, "Paste");
            detail_.pianoRoll().pasteNotes(heldNotes_, at);
        }
        lastArea_ = Area::Clip;
        return;
    case Held::Points:
        if (mixerShown_ || !arrangement_.pastePoints(heldPoints_, at))
            showWarning("Points paste onto an automation lane: select one first");
        else
            lastArea_ = Area::Lane;
        return;
    case Held::Clips:
    {
        // On the selected track (else the first), rows kept (tracks-and-clips.md §9).
        const auto snapshot = session_.snapshot();
        std::uint64_t target = 0;
        if (const auto c = arrangement_.selection().channel)
            if (const auto* info = snapshot->channel(*c); info != nullptr && info->holdsClips())
                target = info->id;
        if (target == 0 && !snapshot->tracks.empty())
            target = snapshot->tracks.front().id;
        if (target == 0 || mixerShown_)
        {
            showWarning("Clips paste onto a track in the arrangement");
            return;
        }
        const auto result = session_.pasteClips(heldClips_, target, at);
        session_.updateSnapshotNow();
        lastArea_ = Area::Arrangement;
        arrangement_.setSelection(model::withClips(*session_.snapshot(), {result.clips.begin(), result.clips.end()}, target, target));
        if (result.rowsLeftOut > 0)
            showWarning(juce::String(result.rowsLeftOut) + (result.rowsLeftOut == 1 ? " row" : " rows") +
                        " had no track below: left out");
        return;
    }
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
    if (context.channel)
        recentChannel_ = context.channel->id;

    // Live MIDI plays the selected track's instrument (auto-arm); other tracks hear nothing.
    // The piano roll previews notes through the same route.
    if (context.midiInputTarget != session_.midiInputTarget())
        session_.setMidiInputTarget(context.midiInputTarget);
    inspector_.setContext(context, snapshot->tempo, snapshot->returns);
    // A lane's context is its first assignment's channel, or another of its assignments' when
    // the inspector showed that one's module (automation.md §9).
    auto detailChannel = context.channel;
    if (context.key != subject_)
        laneDetailChannel_.reset();
    if (context.lane && laneDetailChannel_ && context.lane->assignments.size() > 1)
        for (const auto& a : context.lane->assignments)
            if (a.channel == *laneDetailChannel_)
                if (const auto* c = snapshot->channel(a.channel))
                    detailChannel = *c;
    detail_.setSelection(detailChannel);
    detail_.setClip(context.midiClip, snapshot->tempo);

    // The detail panel follows the subject: a MIDI clip opens the clip view, anything else the
    // modules; Tab switches until the subject changes.
    if (context.key != subject_)
    {
        subject_ = context.key;
        arrangeMode_ = context.opensClipView ? DetailPanel::Mode::Clip : DetailPanel::Mode::Devices;
        // A lane shows its first assignment's module below (automation.md §9).
        if (context.lane && !context.lane->assignments.empty() && context.lane->assignments.front().device != 0)
            detail_.chain().selectDevice(context.lane->assignments.front().device);
    }
    applyDetailMode();
}

void Shell::assignToLane(std::uint64_t lane)
{
    const auto snapshot = session_.snapshot();
    const auto* l = snapshot->automationLane(lane);
    if (l == nullptr)
        return;
    // Its first parameter's channel; for a lane that drives nothing, the channel used last.
    const auto channel = !l->assignments.empty()                                          ? l->assignments.front().channel
                         : recentChannel_ && snapshot->channel(*recentChannel_) != nullptr ? *recentChannel_
                         : !snapshot->tracks.empty()                                       ? snapshot->tracks.front().id
                                                                                           : snapshot->master.id;
    lanePicker_.open(*this, lane, channel);
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
    toastAction_.clear();
    toastRun_ = nullptr;
    toastClick_.setVisible(false);
    repaint();
}

void Shell::showToast(const juce::String& message, const juce::String& action, std::function<void()> run)
{
    showWarning(message);
    warningUntil_ = juce::Time::getMillisecondCounterHiRes() * 0.001 + 8.0; // time to reach it
    toastAction_ = action;
    toastRun_ = std::move(run);
    toastClick_.setBounds(ui::toastArea(arrangement_.getBounds()));
    toastClick_.setVisible(true);
    toastClick_.toFront(false);
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
    // Alt held (and no mouse button: Alt-drags bypass snapping): the menus' letters underlined.
    const auto mods = juce::ModifierKeys::currentModifiers;
    if (const bool alt = mods.isAltDown() && !mods.isAnyMouseButtonDown(); alt != mnemonicsShown_)
    {
        mnemonicsShown_ = alt;
        if (onMnemonicsShown)
            onMnemonicsShown(alt);
    }

    const double position = session_.position();
    arrangement_.frame(dt, position);
    detail_.pianoRoll().setSnapEnabled(arrangement_.isSnapEnabled());
    if (detail_.isVisible())
        detail_.frame(position);
    transport_->frame(position, session_.snapshot()->tempo);

    if (warning_.isNotEmpty() && timestamp > 0.0 && juce::Time::getMillisecondCounterHiRes() * 0.001 > warningUntil_)
    {
        warning_.clear();
        toastClick_.setVisible(false);
        repaint();
    }
    // An export running with its panel closed: its progress along the top bar (export.md §7).
    if (exportPanel_.progress() || exportProgressShown_)
    {
        exportProgressShown_ = exportPanel_.progress().has_value() && !exportPanel_.isOpen();
        repaint(transport_->getBounds());
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
    if (const auto progress = exportPanel_.progress(); progress && !exportPanel_.isOpen())
    {
        const auto bar = transport_->getBounds().toFloat();
        ui::paintProgress(g, bar.withTop(bar.getBottom() - static_cast<float>(t.shell.progressHeight)), *progress);
    }
    if (warning_.isEmpty())
        return;
    ui::paintToast(g, ui::toastArea(arrangement_.getBounds()), warning_, toastAction_);
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
