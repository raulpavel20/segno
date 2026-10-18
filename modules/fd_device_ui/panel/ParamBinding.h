#pragma once

namespace fd::ui
{
/// Host-agnostic access to a device's parameters for UI controls. Implemented by each host:
/// the plugin adapter (JUCE parameters), the DAW adapter (tracktion AutomatableParameter),
/// and LocalParamBinding (tools). All edits go through here, never into the device directly,
/// so automation recording and undo work (CLAUDE.md, hard rules). Message thread only.
class ParamBinding
{
public:
    virtual ~ParamBinding() = default;

    [[nodiscard]] virtual float getPlain(size_t index) const = 0;
    virtual void setPlain(size_t index, float plain) = 0;
    virtual void beginGesture(size_t index) = 0;
    virtual void endGesture(size_t index) = 0;

    /// Merges `patch` into the device's non-parameter state through the host (ADR 0017), so the
    /// change is saved with the project or preset and is undoable in the DAW.
    virtual void applyExtraState(const nlohmann::json& patch) = 0;

    /// Starts one undo step named `name` for the edits that follow, when a panel changes several
    /// parameters as one action (the EQ's Make room). In the DAW only: plugin hosts group their
    /// own undo, so it does nothing there.
    virtual void beginEdit(const juce::String& /*name*/) {}

    // Automation (docs/design/automation.md §6): in the DAW only; a plugin host automates our
    // plugins its own way, so these default to nothing there.
    /// Whether a parameter is automated, and whether its lane plays.
    [[nodiscard]] virtual AutomationState automationState(size_t /*index*/) const { return AutomationState::None; }
    /// Whether the host can automate it (automatable parameters in the DAW).
    [[nodiscard]] virtual bool canAutomate(size_t /*index*/) const { return false; }
    /// The control's menu: the host adds its automation items (show its lane, turn it back on,
    /// remove it from its lane, assign it to a lane, automate it in a new one).
    virtual void addAutomationItems(size_t /*index*/, juce::PopupMenu& /*menu*/) {}

    // Racks (docs/design/racks.md §4): in the DAW only, so these default to nothing elsewhere.
    /// A parameter set by a rack's macro: the macro's name, and where the mapping takes it at the
    /// macro's 0 and 100 % (normalised; `to` below `from` turns it the other way). `dimmed` while
    /// the pointer shows another macro's (its ring dims, as for modulation sources).
    struct MacroMapping
    {
        juce::String macro;
        float from = 0.0f, to = 1.0f;
        bool dimmed = false;
        bool operator==(const MacroMapping&) const = default;
    };
    [[nodiscard]] virtual std::optional<MacroMapping> macroMapping(size_t /*index*/) const
    {
        return std::nullopt;
    }
    /// The control's menu: the host adds its rack items (Map to macro, Unmap).
    virtual void addMacroItems(size_t /*index*/, juce::PopupMenu& /*menu*/) {}
    /// While a rack's macro is dragged by its name: whether the parameter could take it (its knob
    /// shows a faint ring), and whether the pointer is on it (lit).
    struct MacroDrag
    {
        bool candidate = false, target = false;
    };
    [[nodiscard]] virtual MacroDrag macroDrag(size_t /*index*/) const { return {}; }

    // Modulators (docs/design/modulators.md §7): in the DAW only, so these default to nothing
    // elsewhere.
    /// A parameter moved by its track's modulators: the range their routes move it over
    /// (normalised, `from` <= `to`), where they have it now, and who moves it (for its tooltip);
    /// `dimmed` while the pointer shows another modulator's routes.
    struct Modulated
    {
        float from = 0.0f, to = 0.0f, now = 0.0f;
        juce::String by;
        bool dimmed = false;
        bool operator==(const Modulated&) const = default;
    };
    [[nodiscard]] virtual std::optional<Modulated> modulated(size_t /*index*/) const { return std::nullopt; }
    /// While a modulator's handle is dragged: whether the parameter could take it, and whether
    /// the pointer is on it.
    [[nodiscard]] virtual MacroDrag modulatorDrag(size_t /*index*/) const { return {}; }
    /// A drag of the ring they draw (§7.2): the amount of the route the host picks (the modulator
    /// hovered last), one undo step from begin to end; `delta` is of the control's travel. False
    /// where there's no route to set.
    virtual bool beginModulatedDrag(size_t /*index*/) { return false; }
    virtual void dragModulated(size_t /*index*/, float /*delta*/) {}
    virtual void endModulatedDrag(size_t /*index*/) {}
    /// A control's right-click (§15): the routes that move it in `menu`, the track's modulators in
    /// `with` (the Modulate with ▸ submenu, which a module's own sources share).
    virtual void addModulatorItems(size_t /*index*/, juce::PopupMenu& /*menu*/, juce::PopupMenu& /*with*/) {}
};

/// Merges `patch` into `device`'s extra state directly (hosts call this once they have
/// recorded the change). Not real-time.
void mergeExtraState(Device& device, const nlohmann::json& patch);

/// Binding that writes straight into a ParamStore (tools, tests, the gallery).
class LocalParamBinding final : public ParamBinding
{
public:
    explicit LocalParamBinding(ParamStore& store) : store_(store) {}
    explicit LocalParamBinding(Device& device) : store_(device.params()), device_(&device) {}

    [[nodiscard]] float getPlain(size_t index) const override { return store_.get(index); }
    void setPlain(size_t index, float plain) override { store_.set(index, plain); }
    void beginGesture(size_t) override {}
    void endGesture(size_t) override {}
    void applyExtraState(const nlohmann::json& patch) override
    {
        if (device_ != nullptr)
            mergeExtraState(*device_, patch);
    }

private:
    ParamStore& store_;
    Device* device_ = nullptr;
};

/// Where a device's sidechain comes from (ADR 0025): host routing, never a parameter. The DAW
/// offers its other tracks; a plugin offers Off and External (its host's auxiliary input).
/// Message thread only.
class SidechainRouting
{
public:
    virtual ~SidechainRouting() = default;
    /// The choices by name; the first is always "no sidechain" (the device hears its input).
    [[nodiscard]] virtual std::vector<std::string> sources() const = 0;
    /// The current choice, an index into sources().
    [[nodiscard]] virtual int selected() const = 0;
    /// Chooses a source: recorded like any edit in the host (undoable in the DAW).
    virtual void select(int index) = 0;
};

/// What a MIDI effect's note display shows (docs/design/midi-effects.md §12): the notes it heard
/// and played around the playhead, and the song it followed. Times are in beats of the song.
class NoteFeed
{
public:
    struct Note
    {
        double start = 0.0, end = 0.0;
        int key = 60;
        float velocity = 0.8f;
        bool played = true; // went out; false: came in
    };
    struct Chord
    {
        double start = 0.0, end = 0.0;
        juce::String name; // spelled in the key ("Am7", "D9/F♯")
    };
    struct View
    {
        std::vector<Note> notes;
        std::vector<Chord> chords; // the harmony it followed, in time order
        double now = 0.0;          // the playhead
        double beatsPerBar = 4.0;
        std::optional<double> sectionEnd; // where the section playing ends, if it's in sight
        juce::String following;           // what it follows: "Song", "Bass", "Played", "Own"
        juce::String fallback;            // why there's no chord here ("The key's I"), else empty
    };
    virtual ~NoteFeed() = default;
    /// Everything from `before` beats before the playhead to `after` beats after it.
    [[nodiscard]] virtual View view(double before, double after) const = 0;
};

/// Everything a device panel needs from its host.
struct DeviceUiContext
{
    const DeviceInfo& info;
    ParamBinding& params;
    /// Live display data from the audio thread; may be null (e.g. snapshots).
    AnalysisBus* analysis = nullptr;
    /// Read-only access for displays of non-parameter data, e.g. a loaded sample (ADR 0017).
    /// Message thread only; may be null. Never write state through it: use `params`.
    const Device* device = nullptr;
    /// The sidechain picker's source list, for a device with a sidechain bus; null where the
    /// host can't route one (snapshots, the gallery).
    SidechainRouting* sidechain = nullptr;
    /// Presets (ADR 0044): the header's preset bar; null where there are none (snapshots, the
    /// gallery).
    PresetHost* presets = nullptr;
    /// Asks the host for `height` px below the module's header while the preset list is open (0:
    /// done), where its window is too small to hold it: a plugin's editor grows. Empty in the DAW.
    std::function<void(int height)> popoverRoom;
    /// The module's own modulation (device-panels.md, "Modulation"), set by a panel that has
    /// one: its knobs show rings and take dragged sources. Null for most modules.
    ModulationModel* modulation = nullptr;
    /// Shows the module's editor (ADR 0053) where the host has room for it: the DAW's detail
    /// panel, under a plugin's panel. The host owns it until its Done, or until the module goes.
    /// Empty where there's no room for one (snapshots, the gallery).
    std::function<void(std::unique_ptr<ModuleEditor> editor)> showEditor;
    /// Holds a note on the module (`velocity` 0 lets it go): an editor's Preview. Empty where the
    /// host can't play one.
    std::function<void(int note, float velocity)> playNote;
    /// The user's Segno folder (Documents/Segno, or where the DAW's Settings put it), where a
    /// module writes files of the user's (Export Wavetable…). Empty: Documents/Segno.
    std::function<juce::File()> segnoFolder;
    /// A MIDI effect's notes and the song it follows, for its note display; null where the host
    /// has none (snapshots, the gallery).
    NoteFeed* notes = nullptr;
};
} // namespace fd::ui
