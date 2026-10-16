#include "GranularPanel.h"

#include "GranularUi.h"

namespace fd::devices
{
namespace
{
using namespace granular_ui;
using Mode = G::Mode;

float plain(const ui::DeviceUiContext& ctx, size_t index)
{
    return ctx.params.getPlain(index);
}

Mode modeOf(const ui::DeviceUiContext& ctx)
{
    return static_cast<Mode>(static_cast<int>(plain(ctx, G::GrainMode)));
}

/// Notices a parameter's change on the panel's refresh, and says so after it (never during:
/// what it sets off may remove controls the refresh is walking).
class ParamWatcher final : public juce::Component, public ui::ParamControl, private juce::AsyncUpdater
{
public:
    ParamWatcher(ui::DeviceUiContext& context, size_t param, std::function<void()> changed)
        : ctx_(context), param_(param), changed_(std::move(changed)), last_(context.params.getPlain(param))
    {
    }
    void refresh() override
    {
        if (const float now = ctx_.params.getPlain(param_); now != last_)
        {
            last_ = now;
            triggerAsyncUpdate();
        }
    }

private:
    void handleAsyncUpdate() override { changed_(); }
    ui::DeviceUiContext& ctx_;
    size_t param_;
    std::function<void()> changed_;
    float last_;
};

/// The live input's level: a meter as the meter strip's, as wide, centred in its cell.
class InputMeter final : public juce::Component, public juce::SettableTooltipClient
{
public:
    InputMeter() { addAndMakeVisible(meter_); }
    [[nodiscard]] ui::Meter& meter() noexcept { return meter_; }
    void resized() override
    {
        const int width = ui::Theme::current().deviceGrid.meterWidth;
        meter_.setBounds(getLocalBounds().withSizeKeepingCentre(width, getHeight()));
    }

private:
    ui::Meter meter_{1};
};

/// Sections per devices/granular/SPEC.md ("UI layout"), on three pages (ADR 0051). SOURCE shows
/// the selected source, FILTER the selected filter, ENVELOPES and LFO the selected one, as the
/// Wavetable's do; GRAIN shows what the mode uses, placed again when the mode changes.
class GranularPanel final : public ui::DevicePanel
{
public:
    explicit GranularPanel(ui::DeviceUiContext& ctx) : DevicePanel(ctx), modulation_(ctx)
    {
        ctx.modulation = &modulation_;

        // ---- Grains ----
        addPage("grains", "Grains");
        source_ = &addSection("source", "Source 1", 7, G::source(0, G::SrcOn));
        sourceSelector_ = &own(std::make_unique<ui::Selector>(juce::StringArray{"1", "2"}, 2));
        sourceSelector_->onSelect = [this](int) { rebuildSource(); };
        display_ = &own(std::make_unique<SourceDisplay>(ctx));
        picker_ = &own(std::make_unique<SourcePicker>(ctx));
        display_->onBrowse = [this] { picker_->browse(); };
        // On Live, the source's sample stays loaded for when it's back on Sample, but isn't what plays.
        dimWhen({picker_}, [this, &ctx]
                { return plain(ctx, G::source(sourceSelector_->selected(), G::SrcInput)) >= 0.5f; });

        auto& position = addSection("position", "Position", 5);
        position.knob(G::Pos, 0, 0);
        position.knob(G::PosSpray, 1, 0);
        position.knob(G::ScanRate, 0, 3);
        position.knob(G::PosKey, 1, 3);
        for (const auto& [param, row] :
             {std::pair{G::PosDist, 0}, std::pair{G::ScanSync, 1}, std::pair{G::ScanEnd, 2},
              std::pair{G::ScanFrom, 3}, std::pair{G::PosSnap, 4}})
            position.combo(param, 2, row, 3).setInlineName(true);

        grain_ = &addSection("grain", "Grain", 6);
        window_ = &own(std::make_unique<WindowDisplay>(ctx));
        // GRAIN shows what the mode uses: placed again when the mode changes.
        addChildComponent(own(std::make_unique<ParamWatcher>(ctx, G::GrainMode, [this] { rebuildGrain(); })));
        dimWhen({window_}, [&ctx] { return modeOf(ctx) == Mode::Spectral; }); // frames, not windowed grains

        auto& pitch = addSection("pitch", "Pitch", 4);
        pitch.knob(G::PitchTranspose, 0, 0);
        pitch.knob(G::PitchFine, 1, 0);
        pitch.knob(G::PitchKey, 2, 0);
        pitch.knob(G::PitchSpray, 3, 0);
        pitch.knob(G::PitchGlide, 0, 3);
        pitch.segmented(G::PitchDir, 1, 3, 3, 1);
        pitch.combo(G::PitchQuant, 1, 4, 3).setInlineName(true);
        auto& chord = pitch.combo(G::PitchChord, 1, 5, 3);
        chord.setInlineName(true);
        dimWhen({&chord}, [&ctx] { return static_cast<int>(plain(ctx, G::PitchQuant)) != 5; });

        auto& spread = addSection("spread", "Spread", 4);
        spread.knob(G::PanSpray, 0, 0);
        spread.knob(G::GrainSizeSpray, 1, 0);
        spread.knob(G::GrainLevelSpray, 2, 0);
        spread.knob(G::GrainEven, 3, 0);
        spread.combo(G::PanMode, 0, 3, 3).setInlineName(true);
        spread.toggle(G::GrainComp, 0, 4, 2);

        // ---- Sound ----
        addPage("sound", "Sound");
        auto& grainFilter = addSection("gfilter", "Grain Filter", 4, G::GFilterOn);
        grainFilter.combo(G::GFilterType, 0, 0, 3).setInlineName(true);
        grainFilter.toggle(G::GFilterSnap, 3, 0);
        grainResponse_ = &own(std::make_unique<GrainFilterResponse>(ctx));
        grainFilter.place(*grainResponse_, 0, 1, 4, 2);
        grainFilter.holdsParam(G::GFilterCutoff);
        grainFilter.holdsParam(G::GFilterRes);
        grainFilter.knob(G::GFilterCutoff, 0, 3);
        grainFilter.knob(G::GFilterRes, 1, 3);
        grainFilter.knob(G::GFilterSpray, 2, 3);
        grainFilter.knob(G::GFilterKey, 3, 3);

        auto& color = addSection("gcolor", "Grain Color", 3);
        color.knob(G::GColorCrush, 0, 0);
        color.knob(G::GColorCrushSpray, 1, 0);
        color.knob(G::GColorRing, 0, 3);
        color.knob(G::GColorRatio, 1, 3);
        color.knob(G::GColorRingSpray, 2, 3);

        filter_ = &addSection("filter", "Filter 1", 6, G::filter(0, G::FilterOn));
        filterSelector_ = &own(std::make_unique<ui::Selector>(juce::StringArray{"1", "2"}, 2));
        filterSelector_->onSelect = [this](int) { rebuildFilter(); };
        response_ = &own(std::make_unique<FilterResponse>(ctx));

        envelopes_ = &addSection("env", "Envelopes", 7);
        envSelector_ = &own(std::make_unique<ui::Selector>(juce::StringArray{"Amp", "Env 2", "Env 3"}, 2));
        envSelector_->onSelect = [this](int) { rebuildEnvelope(); };
        envelope_ = &own(std::make_unique<EnvelopeEditor>(ctx, modulation_));

        auto& voice = addSection("voice", "Voice", 5);
        voice.knob(G::VoiceGlide, 0, 0);
        voice.knob(G::VoiceBend, 1, 0);
        voice.knob(G::OutWidth, 2, 0);
        voice.knob(G::OutVolume, 3, 0);
        voice.knob(G::GrainMax, 4, 0);
        voice.combo(G::VoiceMode, 0, 3, 3).setInlineName(true);
        voice.toggle(G::VoiceLatch, 3, 3, 2);
        voice.combo(G::VoiceGlideMode, 0, 4, 3).setInlineName(true);
        voice.toggle(G::VoiceTails, 3, 4, 2);
        voice.bar(G::VoiceCount, 0, 5, 2);
        voice.bar(G::FiltersRouting, 2, 5, 3).setName("Filter Routing"); // far from FILTER, so named in full

        // LIVE: while a source plays the live input (step 2).
        auto& live = addSection("live", "Live", 3);
        live.knob(G::LiveLength, 0, 0);
        live.knob(G::LiveGain, 1, 0);
        // The input's level, beside its gain.
        if (auto* bus = ctx.analysis)
            inputMeter_.meter().setLevelSource([bus](int) { return bus->read(G::InputLevelNow); });
        inputMeter_.setTooltip("How loud the live input is");
        live.place(inputMeter_, 2, 0, 1, 3);
        live.segmented(G::LiveMode, 0, 3, 3, 1);
        live.bar(G::LiveFeedback, 0, 4, 3);
        capture_.setClickingTogglesState(false);
        capture_.setTooltip(
            "Writes what the buffer holds into the project as a file, loaded into this source");
        live.place(capture_, 0, 5, 2, 1, ui::PanelSection::Kind::Line);
        showSectionWhen(live,
                        [&ctx]
                        {
                            for (int n = 0; n < G::numSources; ++n)
                                if (plain(ctx, G::source(n, G::SrcInput)) >= 0.5f &&
                                    plain(ctx, G::source(n, G::SrcOn)) >= 0.5f)
                                    return true;
                            return false;
                        });
        // Shown as soon as a source turns to Live (or goes), not only on the next refresh tick.
        for (const auto p : {G::source(0, G::SrcInput), G::source(1, G::SrcInput), G::source(0, G::SrcOn),
                             G::source(1, G::SrcOn)})
            addChildComponent(own(std::make_unique<ParamWatcher>(ctx, p,
                                                                 [this]
                                                                 {
                                                                     updateShownSections();
                                                                     updateDimming(); // the sample's name
                                                                 })));

        // ---- Modulation ----
        addPage("modulation", "Modulation");
        lfo_ = &addSection("lfo", "LFO", 6);
        lfoSelector_ =
            &own(std::make_unique<ui::Selector>(juce::StringArray{"1", "2", "3", "4", "R1", "R2"}, 3));
        lfoSelector_->onSelect = [this](int) { rebuildLfo(); };
        shape_ = &own(std::make_unique<ShapeEditor>(ctx, modulation_));
        auto& matrix = addSection("matrix", "Matrix", 11);
        matrix_ = &own(std::make_unique<MatrixList>(ctx, modulation_));
        matrix.place(*matrix_, 0, 0, 11, 6);
        auto& macros = addSection("macros", "Macros", 4);
        for (int n = 0; n < G::numMacros; ++n)
        {
            auto& knob = macros.knob(G::macro(n), n % 4, n < 4 ? 0 : 3);
            knob.setModulationSource(static_cast<int>(G::Source::Macro1) + n);
            knob.setTooltip(knob.getTooltip() + " Drag its name onto a knob to modulate it.");
            macroKnobs_[static_cast<size_t>(n)] = &knob;
        }

        // ---- Header and meter ----
        setSidechainCaption("Input",
                            "Where the live input comes from: a track of the song, for a source on Live");
        addHeaderCombo(G::Quality);
        addHeaderReadout(
            "Grains", "1 024",
            [&ctx]
            {
                if (ctx.analysis == nullptr)
                    return juce::String(juce::String::fromUTF8("–"));
                return juce::String(juce::roundToInt(ctx.analysis->read(G::GrainsPlaying)));
            },
            "How many grains are playing now: Max Grains and Density set how many there can be");
        addHeaderReadout(
            "CPU", "100 %",
            [&ctx]
            {
                if (ctx.analysis == nullptr)
                    return juce::String(juce::String::fromUTF8("–"));
                return juce::String(juce::roundToInt(ctx.analysis->read(G::CpuLoad) * 100.0f)) + " %";
            },
            "How much of the time it has the instrument takes to play now: more grains, a grain filter and a "
            "higher Quality take more");
        if (auto* bus = ctx.analysis)
            meter_.setLevelSource([bus](int) { return bus->takePeak(G::PeakOut); });
        setMeters({{&meter_, "Out"}});

        rebuildSource();
        rebuildGrain();
        rebuildFilter();
        rebuildEnvelope();
        rebuildLfo();
        refreshMacroNames();
    }

    ~GranularPanel() override
    {
        if (context().modulation == &modulation_)
            context().modulation = nullptr;
    }

    bool takeFile(const juce::File& file) override
    {
        loadSourceFile(context(), sourceSelector_->selected(), file);
        return true;
    }

    bool showParameter(size_t param) override
    {
        // Select what shows it, then turn to its page.
        if (param < G::SrcMix)
            sourceSelector_->setSelected(static_cast<int>(param / G::SourceParamCount),
                                         juce::sendNotification);
        else if (param >= G::FilterBase && param < G::FiltersRouting)
            filterSelector_->setSelected(static_cast<int>((param - G::FilterBase) / G::FilterParamCount),
                                         juce::sendNotification);
        else if (param >= G::EnvBase && param < G::LfoBase)
            envSelector_->setSelected(static_cast<int>((param - G::EnvBase) / G::EnvParamCount),
                                      juce::sendNotification);
        else if (param >= G::LfoBase && param < G::RandomBase)
            lfoSelector_->setSelected(static_cast<int>((param - G::LfoBase) / G::LfoParamCount),
                                      juce::sendNotification);
        else if (param >= G::RandomBase && param < G::MacroBase)
            lfoSelector_->setSelected(G::numLfos +
                                          static_cast<int>((param - G::RandomBase) / G::RandomParamCount),
                                      juce::sendNotification);
        else if (param >= G::RouteBase && param < G::VoiceMode)
        {
            setPage(2, true); // the matrix
            return true;
        }
        return DevicePanel::showParameter(param);
    }

    /// GRAIN's controls for the mode (for tests).
    [[nodiscard]] Mode shownMode() const noexcept { return shownMode_; }

private:
    /// SOURCE for the selected source: the display, its name, its Input, Gain and Tune; the Mix
    /// and Pick of both under them.
    void rebuildSource()
    {
        const int n = sourceSelector_->selected();
        source_->clear();
        source_->setSectionTitle("Source " + juce::String(n + 1));
        source_->setPowerParam(G::source(n, G::SrcOn));
        display_->setSource(n);
        picker_->setSource(n);
        source_->place(*display_, 0, 0, 5, 5);
        source_->holdsParam(G::Pos);
        source_->holdsParam(G::source(n, G::SrcStart));
        source_->holdsParam(G::source(n, G::SrcEnd));
        source_->place(*picker_, 0, 5, 5, 1, ui::PanelSection::Kind::Line);
        source_->place(*sourceSelector_, 5, 0, 2, 1, ui::PanelSection::Kind::Line);
        source_->segmented(G::source(n, G::SrcInput), 5, 1, 2, 1);
        source_->bar(G::source(n, G::SrcGain), 5, 2);
        source_->bar(G::source(n, G::SrcTune), 5, 3);
        source_->bar(G::SrcMix, 5, 4);
        source_->combo(G::SrcPick, 5, 5).setInlineName(true);
        resized();
    }

    /// GRAIN for the mode: its knobs, then Mode and Window, the size's sync (Rate in Rhythm,
    /// Resolution in Spectral), the window's Skew and Flatness, Repeats and Fire.
    void rebuildGrain()
    {
        const auto mode = modeOf(context());
        shownMode_ = mode;
        grain_->clear();
        std::array<size_t, 5> knobs{};
        switch (mode)
        {
        case Mode::Classic:
            knobs = {G::GrainSize, G::GrainOverlap, G::GrainJitter, G::GrainChance, G::GrainReverse};
            break;
        case Mode::Loop:
            knobs = {G::GrainSize, G::GrainXfade, G::GrainJitter, G::GrainChance, G::GrainReverse};
            break;
        case Mode::Cloud:
            knobs = {G::GrainSize, G::GrainDensity, G::GrainJitter, G::GrainChance, G::GrainReverse};
            break;
        case Mode::Pitched:
            knobs = {G::GrainPeriods, G::GrainFormant, G::GrainJitter, G::GrainChance, G::GrainReverse};
            break;
        case Mode::Rhythm:
            knobs = {G::GrainSize, G::GrainSwing, G::GrainJitter, G::GrainChance, G::GrainReverse};
            break;
        case Mode::Spectral:
            knobs = {G::SpecSmear, G::SpecHarm, G::SpecClean, G::GrainFormant, G::GrainChance};
            break;
        }
        for (int k = 0; k < 5; ++k)
            grain_->knob(knobs[static_cast<size_t>(k)], k, 0);
        grain_->place(*window_, 5, 0, 1, 3);
        grain_->holdsParam(G::GrainSkew);
        grain_->holdsParam(G::GrainFlat);
        grain_->combo(G::GrainMode, 0, 3, 3).setInlineName(true);
        auto& windowChoice = grain_->combo(G::GrainWindow, 3, 3, 3);
        windowChoice.setInlineName(true);
        const auto sync = mode == Mode::Rhythm     ? G::GrainRate
                          : mode == Mode::Spectral ? G::SpecFft
                                                   : G::GrainSizeSync;
        auto& syncChoice = grain_->combo(sync, 0, 4, 3);
        syncChoice.setInlineName(true);
        auto& skew = grain_->bar(G::GrainSkew, 3, 4, 3);
        auto& flat = grain_->bar(G::GrainFlat, 3, 5, 3);
        grain_->bar(G::GrainRepeats, 0, 5, 2);
        grain_->toggle(G::GrainFire, 2, 5).setMomentary(true); // fires while held, as a pad
        // Pitched sets a grain's length in periods: its sync has nothing to do. Spectral plays
        // frames, not windowed grains.
        auto& ctx = context();
        dimWhen({&syncChoice}, [&ctx] { return modeOf(ctx) == Mode::Pitched; });
        dimWhen({&windowChoice, &skew, &flat}, [&ctx] { return modeOf(ctx) == Mode::Spectral; });
        resized();
    }

    void rebuildFilter()
    {
        const int n = filterSelector_->selected();
        const auto p = [n](G::FilterParam q) { return G::filter(n, q); };
        filter_->clear();
        filter_->setSectionTitle("Filter " + juce::String(n + 1));
        filter_->setPowerParam(p(G::FilterOn));
        filter_->place(*filterSelector_, 0, 0, 2, 1, ui::PanelSection::Kind::Line);
        filter_->combo(p(G::FilterType), 2, 0, 4).setInlineName(true);
        response_->setFilter(n);
        filter_->place(*response_, 0, 1, 6, 2);
        filter_->holdsParam(p(G::FilterCutoff));
        filter_->holdsParam(p(G::FilterRes));
        filter_->knob(p(G::FilterCutoff), 0, 3);
        filter_->knob(p(G::FilterRes), 1, 3);
        filter_->knob(p(G::FilterDrive), 2, 3);
        filter_->knob(p(G::FilterShape), 3, 3);
        filter_->knob(p(G::FilterEnv), 4, 3);
        filter_->knob(p(G::FilterKey), 5, 3);
        resized();
    }

    void rebuildEnvelope()
    {
        const int n = envSelector_->selected();
        const auto p = [n](G::EnvParam q) { return G::env(n, q); };
        envelopes_->clear();
        envelopes_->place(*envSelector_, 0, 0, 2, 2, ui::PanelSection::Kind::Line);
        envelope_->setEnvelope(n);
        envelopes_->place(*envelope_, 0, 2, 2, 4);
        envelopes_->knob(p(G::EnvAttack), 2, 0);
        envelopes_->knob(p(G::EnvHold), 3, 0);
        envelopes_->knob(p(G::EnvDecay), 4, 0);
        envelopes_->knob(p(G::EnvSustain), 5, 0);
        envelopes_->knob(p(G::EnvRelease), 6, 0);
        envelopes_->bar(p(G::EnvAttackCurve), 2, 3);
        envelopes_->bar(p(G::EnvDecayCurve), 2, 4);
        envelopes_->bar(p(G::EnvReleaseCurve), 2, 5);
        envelopes_->combo(p(G::EnvMode), 4, 3, 3).setInlineName(true);
        if (n == 0) // the amp's: how much the velocity moves the level
            envelopes_->bar(G::AmpVelocity, 4, 4, 3);
        resized();
    }

    void rebuildLfo()
    {
        const int n = lfoSelector_->selected();
        lfo_->clear();
        lfo_->place(*lfoSelector_, 0, 0, 3, 2, ui::PanelSection::Kind::Line);
        shape_->setSource(n);
        lfo_->place(*shape_, 0, 2, 3, 4);
        auto& ctx = context();
        if (n < G::numLfos)
        {
            const auto p = [n](G::LfoParam q) { return G::lfo(n, q); };
            lfo_->setSectionTitle("LFO " + juce::String(n + 1));
            auto& rate = lfo_->knob(p(G::LfoRate), 3, 0);
            lfo_->knob(p(G::LfoPhase), 4, 0);
            lfo_->knob(p(G::LfoSmooth), 5, 0);
            lfo_->knob(p(G::LfoFade), 3, 3);
            lfo_->combo(p(G::LfoSync), 4, 3).setInlineName(true);
            lfo_->combo(p(G::LfoMode), 4, 4);
            lfo_->toggle(p(G::LfoPoly), 4, 5, 2);
            dimWhen({&rate}, [&ctx, p] { return plain(ctx, p(G::LfoSync)) >= 0.5f; });
        }
        else
        {
            const int r = n - G::numLfos;
            const auto p = [r](G::RandomParam q) { return G::random(r, q); };
            lfo_->setSectionTitle("Random " + juce::String(r + 1));
            auto& rate = lfo_->knob(p(G::RandomRate), 3, 0);
            lfo_->combo(p(G::RandomSync), 3, 3, 3).setInlineName(true);
            lfo_->combo(p(G::RandomMode), 3, 4, 3).setInlineName(true);
            lfo_->toggle(p(G::RandomPoly), 3, 5, 3);
            // Synced, or new with each grain, the rate does nothing.
            dimWhen({&rate},
                    [&ctx, p]
                    {
                        return plain(ctx, p(G::RandomSync)) >= 0.5f ||
                               static_cast<int>(plain(ctx, p(G::RandomMode))) == G::RandomPerGrain;
                    });
        }
        resized();
    }

    void refreshMacroNames()
    {
        const auto* g = dynamic_cast<const G*>(context().device);
        for (int n = 0; n < G::numMacros; ++n)
        {
            auto* knob = macroKnobs_[static_cast<size_t>(n)];
            const auto name = juce::String(g != nullptr ? g->macroName(n) : "Macro " + std::to_string(n + 1));
            if (knob->getName() != name)
                knob->setName(name);
        }
    }

    void tick()
    {
        if (!isShowing())
            return;
        refreshMacroNames();
        display_->refresh();
        shape_->refresh();
    }

    Modulation modulation_;
    ui::Selector *sourceSelector_ = nullptr, *filterSelector_ = nullptr, *envSelector_ = nullptr,
                 *lfoSelector_ = nullptr;
    ui::PanelSection *source_ = nullptr, *grain_ = nullptr, *filter_ = nullptr, *envelopes_ = nullptr,
                     *lfo_ = nullptr;
    SourceDisplay* display_ = nullptr;
    SourcePicker* picker_ = nullptr;
    WindowDisplay* window_ = nullptr;
    GrainFilterResponse* grainResponse_ = nullptr;
    FilterResponse* response_ = nullptr;
    EnvelopeEditor* envelope_ = nullptr;
    ShapeEditor* shape_ = nullptr;
    MatrixList* matrix_ = nullptr;
    ui::Toggle capture_{"Capture"};
    std::array<ui::ParamKnob*, G::numMacros> macroKnobs_{};
    ui::Meter meter_{1};
    InputMeter inputMeter_;
    Mode shownMode_ = Mode::Classic;
    juce::VBlankAttachment vblank_{this, [this] { tick(); }};
};
} // namespace

const ui::PanelFactory& granularPanelFactory()
{
    static const ui::PanelFactory factory{
        .deviceId = "granular",
        .create = [](ui::DeviceUiContext& ctx) -> std::unique_ptr<ui::DevicePanel>
        { return std::make_unique<GranularPanel>(ctx); },
    };
    return factory;
}
} // namespace fd::devices
