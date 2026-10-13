#include "WavetablePanel.h"

#include "WavetableUi.h"

namespace fd::devices
{
namespace
{
using namespace wavetable_ui;

float plain(const ui::DeviceUiContext& ctx, size_t index)
{
    return ctx.params.getPlain(index);
}

/// Sections per devices/wavetable/SPEC.md ("UI layout"), on three pages (ADR 0051). The sections
/// after a selector show the selected oscillator, filter, envelope or LFO; they're placed again
/// when the selection changes, as the EQ's BAND section is.
class WavetablePanel final : public ui::DevicePanel
{
public:
    explicit WavetablePanel(ui::DeviceUiContext& ctx) : DevicePanel(ctx), modulation_(ctx)
    {
        ctx.modulation = &modulation_;

        // ---- Oscillators ----
        addPage("oscillators", "Oscillators");
        auto& sources = addSection("sources", "Sources", 4);
        sourceSelector_ = &own(
            std::make_unique<ui::Selector>(juce::StringArray{"Osc 1", "Osc 2", "Osc 3", "Sub", "Noise"}, 1));
        // A row a source: its On (a dot), its name (selecting it), its level.
        sources.place(*sourceSelector_, 1, 0, 1, 5, ui::PanelSection::Kind::Line);
        for (int s = 0; s < 5; ++s)
        {
            sources.toggle(onParam(s), 0, s).setButtonText({});
            auto& level = sources.bar(levelParam(s), 2, s);
            dimWhen({&level}, [&ctx, s] { return plain(ctx, onParam(s)) < 0.5f; });
        }
        sources.bar(W::FiltersRouting, 0, 5, 4);
        sourceSelector_->onSelect = [this](int) { rebuildOscillator(); };

        source_ = &addSection("osc", "Osc 1", 8, W::osc(0, W::OscOn));
        tableDisplay_ = &own(std::make_unique<TableDisplay>(ctx));
        tablePicker_ = &own(std::make_unique<TablePicker>(ctx));
        subWave_ = &own(std::make_unique<SourceWave>(ctx, false));
        noiseWave_ = &own(std::make_unique<SourceWave>(ctx, true));
        warp_ = &addSection("warp", "Warp", 4);
        cycleDisplay_ = &own(std::make_unique<CycleDisplay>(ctx));
        unison_ = &addSection("unison", "Unison", 4);

        // ---- Filter & Amp ----
        addPage("filter", "Filter & Amp");
        filter_ = &addSection("filter", "Filter", 6, W::filter(0, W::FilterOn));
        filterSelector_ = &own(std::make_unique<ui::Selector>(juce::StringArray{"1", "2"}, 2));
        filterSelector_->onSelect = [this](int) { rebuildFilter(); };
        response_ = &own(std::make_unique<FilterResponse>(ctx));
        envelopes_ = &addSection("env", "Envelopes", 7);
        envSelector_ =
            &own(std::make_unique<ui::Selector>(juce::StringArray{"Amp", "Filter", "Env 3", "Env 4"}, 2));
        envSelector_->onSelect = [this](int) { rebuildEnvelope(); };
        envelope_ = &own(std::make_unique<EnvelopeEditor>(ctx, modulation_));
        auto& voice = addSection("voice", "Voice", 4);
        voice.knob(W::VoiceGlide, 0, 0);
        voice.knob(W::VoiceBend, 1, 0);
        voice.knob(W::VoiceDrift, 2, 0);
        voice.knob(W::OutVolume, 3, 0);
        voice.combo(W::VoiceMode, 0, 3, 4).setInlineName(true);
        voice.combo(W::VoiceGlideMode, 0, 4, 4).setInlineName(true);
        voice.bar(W::VoiceCount, 0, 5, 4);

        // ---- Modulation ----
        addPage("modulation", "Modulation");
        lfo_ = &addSection("lfo", "LFO", 6);
        lfoSelector_ = &own(std::make_unique<ui::Selector>(
            juce::StringArray{"1", "2", "3", "4", "5", "6", "7", "8", "R1", "R2"}, 5));
        lfoSelector_->onSelect = [this](int) { rebuildLfo(); };
        shape_ = &own(std::make_unique<ShapeEditor>(ctx, modulation_));
        auto& matrix = addSection("matrix", "Matrix", 11);
        matrix_ = &own(std::make_unique<MatrixList>(ctx, modulation_));
        matrix.place(*matrix_, 0, 0, 11, 6);
        auto& macros = addSection("macros", "Macros", 4);
        for (int n = 0; n < W::numMacros; ++n)
        {
            auto& knob = macros.knob(W::macro(n), n % 4, n < 4 ? 0 : 3);
            knob.setModulationSource(static_cast<int>(W::Source::Macro1) + n);
            knob.setTooltip(knob.getTooltip() + " Drag its name onto a knob to modulate it.");
            knob.onPopupMenu = [this, &knob, n]
            {
                ui::showParamMenu(context(), W::macro(n), knob,
                                  [this, &knob, n](juce::PopupMenu& menu)
                                  {
                                      menu.addItem(juce::String::fromUTF8("Rename…"),
                                                   [this, &knob, n] { rename(knob, n); });
                                  });
            };
            macroKnobs_[static_cast<size_t>(n)] = &knob;
        }

        // ---- Header and meter ----
        addHeaderCombo(W::Quality);
        addHeaderReadout(
            "CPU", "100 %",
            [&ctx]
            {
                if (ctx.analysis == nullptr)
                    return juce::String(juce::String::fromUTF8("–"));
                return juce::String(juce::roundToInt(ctx.analysis->read(W::CpuLoad) * 100.0f)) + " %";
            },
            "How much of the time it has the synth takes to play now: more voices, unison copies, moving "
            "warps and a higher Quality take more");
        if (auto* bus = ctx.analysis)
            meter_.setLevelSource([bus](int) { return bus->takePeak(W::PeakOut); });
        setMeters({{&meter_, "Out"}});

        rebuildOscillator();
        rebuildFilter();
        rebuildEnvelope();
        rebuildLfo();
        refreshMacroNames();
    }

    ~WavetablePanel() override { context().modulation = nullptr; }

    bool showParameter(size_t param) override
    {
        // Select what shows it, then turn to its page.
        if (param < W::SubOn)
            sourceSelector_->setSelected(static_cast<int>(param / W::OscParamCount), juce::sendNotification);
        else if (param <= W::SubRoute)
            sourceSelector_->setSelected(3, juce::sendNotification);
        else if (param <= W::NoiseRoute)
            sourceSelector_->setSelected(4, juce::sendNotification);
        else if (param >= W::FilterBase && param < W::FiltersRouting)
            filterSelector_->setSelected(static_cast<int>((param - W::FilterBase) / W::FilterParamCount),
                                         juce::sendNotification);
        else if (param >= W::EnvBase && param < W::LfoBase)
            envSelector_->setSelected(static_cast<int>((param - W::EnvBase) / W::EnvParamCount),
                                      juce::sendNotification);
        else if (param >= W::LfoBase && param < W::RandomBase)
            lfoSelector_->setSelected(static_cast<int>((param - W::LfoBase) / W::LfoParamCount),
                                      juce::sendNotification);
        else if (param >= W::RandomBase && param < W::MacroBase)
            lfoSelector_->setSelected(W::numLfos +
                                          static_cast<int>((param - W::RandomBase) / W::RandomParamCount),
                                      juce::sendNotification);
        else if (param >= W::RouteBase && param < W::VoiceMode)
        {
            setPage(2, true); // the matrix
            return true;
        }
        return DevicePanel::showParameter(param);
    }

private:
    static size_t onParam(int source)
    {
        return source < W::numOscs ? W::osc(source, W::OscOn) : source == 3 ? W::SubOn : W::NoiseOn;
    }
    static size_t levelParam(int source)
    {
        return source < W::numOscs ? W::osc(source, W::OscLevel) : source == 3 ? W::SubLevel : W::NoiseLevel;
    }

    /// OSC, WARP and UNISON for the selected source: an oscillator's, or the sub's or the noise's
    /// own controls in the same cells.
    void rebuildOscillator()
    {
        const int s = sourceSelector_->selected();
        source_->clear();
        warp_->clear();
        unison_->clear();
        source_->setPowerParam(onParam(s));
        if (s < W::numOscs)
        {
            const auto p = [s](W::OscParam q) { return W::osc(s, q); };
            source_->setSectionTitle("Osc " + juce::String(s + 1));
            tableDisplay_->setOscillator(s);
            tablePicker_->setOscillator(s);
            source_->place(*tableDisplay_, 0, 0, 4, 4);
            source_->holdsParam(p(W::OscPos));
            source_->place(*tablePicker_, 0, 4, 4, 1, ui::PanelSection::Kind::Line);
            source_->segmented(p(W::OscRoute), 0, 5, 4, 1);
            source_->knob(p(W::OscPos), 4, 0);
            source_->knob(p(W::OscOct), 5, 0);
            source_->knob(p(W::OscSemi), 6, 0);
            source_->knob(p(W::OscFine), 7, 0);
            source_->knob(p(W::OscPhase), 4, 3);
            source_->knob(p(W::OscRand), 5, 3);
            source_->knob(p(W::OscPan), 6, 3);

            for (int k = 0; k < 2; ++k)
            {
                const auto type = p(k == 0 ? W::OscWarp1 : W::OscWarp2);
                const auto source = p(k == 0 ? W::OscWarp1Src : W::OscWarp2Src);
                warp_->combo(type, 0, 2 * k, 4).setInlineName(true);
                auto& from = warp_->combo(source, 0, 2 * k + 1, 2);
                from.setInlineName(true);
                warp_->bar(p(k == 0 ? W::OscWarp1Amt : W::OscWarp2Amt), 2, 2 * k + 1);
                // The source matters only to the warps from another source (FM, PM, RM, AM).
                auto& ctx = context();
                dimWhen({&from},
                        [&ctx, type]
                        {
                            return !dsp::isSampleWarp(
                                static_cast<dsp::Warp>(static_cast<int>(plain(ctx, type))));
                        });
            }
            cycleDisplay_->setOscillator(s);
            warp_->place(*cycleDisplay_, 0, 4, 4, 2);

            unison_->knob(p(W::OscUni), 0, 0);
            unison_->knob(p(W::OscDetune), 1, 0);
            unison_->knob(p(W::OscBlend), 2, 0);
            unison_->knob(p(W::OscWidth), 3, 0);
            unison_->combo(p(W::OscUniMode), 0, 3, 4).setInlineName(true);
            unison_->combo(p(W::OscStack), 0, 4, 4).setInlineName(true);
            // The spreads across the copies: of the position, of the warps.
            unison_->bar(p(W::OscSpread), 0, 5).setName("Position");
            unison_->bar(p(W::OscWarpSpread), 2, 5).setName("Warp");
        }
        else if (s == 3)
        {
            source_->setSectionTitle("Sub");
            source_->place(*subWave_, 0, 0, 4, 4);
            source_->segmented(W::SubShape, 0, 4, 4, 1);
            source_->segmented(W::SubRoute, 0, 5, 4, 1);
            source_->knob(W::SubOct, 4, 0);
            source_->knob(W::SubLevel, 5, 0);
            source_->knob(W::SubPan, 6, 0);
            warp_->label("Oscillators only", 0, 0, 4);
            unison_->label("Oscillators only", 0, 0, 4);
        }
        else
        {
            source_->setSectionTitle("Noise");
            source_->place(*noiseWave_, 0, 0, 4, 4);
            source_->combo(W::NoiseType, 0, 4, 4).setInlineName(true);
            source_->segmented(W::NoiseRoute, 0, 5, 4, 1);
            source_->knob(W::NoiseColor, 4, 0);
            source_->knob(W::NoiseLevel, 5, 0);
            source_->knob(W::NoisePan, 6, 0);
            warp_->label("Oscillators only", 0, 0, 4);
            unison_->label("Oscillators only", 0, 0, 4);
        }
        resized();
    }

    void rebuildFilter()
    {
        const int n = filterSelector_->selected();
        const auto p = [n](W::FilterParam q) { return W::filter(n, q); };
        filter_->clear();
        filter_->setSectionTitle("Filter " + juce::String(n + 1));
        filter_->setPowerParam(p(W::FilterOn));
        filter_->place(*filterSelector_, 0, 0, 2, 1, ui::PanelSection::Kind::Line);
        filter_->combo(p(W::FilterType), 2, 0, 4).setInlineName(true);
        response_->setFilter(n);
        filter_->place(*response_, 0, 1, 6, 2);
        filter_->holdsParam(p(W::FilterCutoff));
        filter_->holdsParam(p(W::FilterRes));
        filter_->knob(p(W::FilterCutoff), 0, 3);
        filter_->knob(p(W::FilterRes), 1, 3);
        filter_->knob(p(W::FilterDrive), 2, 3);
        filter_->knob(p(W::FilterShape), 3, 3);
        filter_->knob(p(W::FilterEnv), 4, 3);
        filter_->knob(p(W::FilterKey), 5, 3);
        resized();
    }

    void rebuildEnvelope()
    {
        const int n = envSelector_->selected();
        const auto p = [n](W::EnvParam q) { return W::env(n, q); };
        envelopes_->clear();
        envelopes_->place(*envSelector_, 0, 0, 2, 2, ui::PanelSection::Kind::Line);
        envelope_->setEnvelope(n);
        envelopes_->place(*envelope_, 0, 2, 2, 4);
        envelopes_->knob(p(W::EnvAttack), 2, 0);
        envelopes_->knob(p(W::EnvHold), 3, 0);
        envelopes_->knob(p(W::EnvDecay), 4, 0);
        envelopes_->knob(p(W::EnvSustain), 5, 0);
        envelopes_->knob(p(W::EnvRelease), 6, 0);
        envelopes_->bar(p(W::EnvAttackCurve), 2, 3);
        envelopes_->bar(p(W::EnvDecayCurve), 2, 4);
        envelopes_->bar(p(W::EnvReleaseCurve), 2, 5);
        envelopes_->combo(p(W::EnvMode), 4, 3, 3).setInlineName(true);
        if (n == 0) // the amp's: how much the velocity moves the level
            envelopes_->bar(W::AmpVelocity, 4, 4, 3);
        resized();
    }

    void rebuildLfo()
    {
        const int n = lfoSelector_->selected();
        lfo_->clear();
        lfo_->place(*lfoSelector_, 0, 0, 3, 2, ui::PanelSection::Kind::Line);
        shape_->setSource(n);
        lfo_->place(*shape_, 0, 2, 3, 4);
        if (n < W::numLfos)
        {
            const auto p = [n](W::LfoParam q) { return W::lfo(n, q); };
            lfo_->setSectionTitle("LFO " + juce::String(n + 1));
            auto& rate = lfo_->knob(p(W::LfoRate), 3, 0);
            lfo_->knob(p(W::LfoPhase), 4, 0);
            lfo_->knob(p(W::LfoSmooth), 5, 0);
            lfo_->knob(p(W::LfoFade), 3, 3);
            lfo_->combo(p(W::LfoSync), 4, 3).setInlineName(true);
            lfo_->combo(p(W::LfoMode), 4, 4);
            lfo_->toggle(p(W::LfoPoly), 4, 5, 2);
            // Synced, the rate comes from the song: Rate recedes.
            auto& ctx = context();
            dimWhen({&rate}, [&ctx, p] { return plain(ctx, p(W::LfoSync)) >= 0.5f; });
        }
        else
        {
            const int r = n - W::numLfos;
            const auto p = [r](W::RandomParam q) { return W::random(r, q); };
            lfo_->setSectionTitle("Random " + juce::String(r + 1));
            auto& rate = lfo_->knob(p(W::RandomRate), 3, 0);
            lfo_->combo(p(W::RandomSync), 3, 3, 3).setInlineName(true);
            lfo_->combo(p(W::RandomMode), 3, 4, 3).setInlineName(true);
            lfo_->toggle(p(W::RandomPoly), 3, 5, 3);
            auto& ctx = context();
            dimWhen({&rate}, [&ctx, p] { return plain(ctx, p(W::RandomSync)) >= 0.5f; });
        }
        resized();
    }

    /// A macro's name, typed over its knob's name.
    void rename(ui::ParamKnob& knob, int n)
    {
        const auto area = getLocalArea(&knob, knob.nameStrip().toNearestInt());
        renamer_.show(area, knob.getName(), ui::TextStyle::Label,
                      [this, n](const juce::String& name)
                      {
                          context().params.beginEdit("Rename a macro");
                          context().params.applyExtraState(
                              {{"macro" + std::to_string(n + 1) + ".name", name.trim().toStdString()}});
                          refreshMacroNames();
                      });
    }

    void refreshMacroNames()
    {
        const auto* synth = dynamic_cast<const W*>(context().device);
        for (int n = 0; n < W::numMacros; ++n)
        {
            auto* knob = macroKnobs_[static_cast<size_t>(n)];
            const auto name =
                juce::String(synth != nullptr ? synth->macroName(n) : "Macro " + std::to_string(n + 1));
            if (knob->getName() != name)
                knob->setName(name);
        }
    }

    void tick()
    {
        if (!isShowing())
            return;
        refreshMacroNames();
        // The rings and handles follow the pointer at the display's rate.
        tableDisplay_->refresh();
        shape_->refresh();
    }

    Modulation modulation_;
    ui::Selector *sourceSelector_ = nullptr, *filterSelector_ = nullptr, *envSelector_ = nullptr,
                 *lfoSelector_ = nullptr;
    ui::PanelSection *source_ = nullptr, *warp_ = nullptr, *unison_ = nullptr, *filter_ = nullptr,
                     *envelopes_ = nullptr, *lfo_ = nullptr;
    TableDisplay* tableDisplay_ = nullptr;
    TablePicker* tablePicker_ = nullptr;
    SourceWave *subWave_ = nullptr, *noiseWave_ = nullptr;
    CycleDisplay* cycleDisplay_ = nullptr;
    FilterResponse* response_ = nullptr;
    EnvelopeEditor* envelope_ = nullptr;
    ShapeEditor* shape_ = nullptr;
    MatrixList* matrix_ = nullptr;
    std::array<ui::ParamKnob*, W::numMacros> macroKnobs_{};
    ui::Meter meter_{1};
    ui::NameEditor renamer_{*this};
    juce::VBlankAttachment vblank_{this, [this] { tick(); }};
};
} // namespace

const ui::PanelFactory& wavetablePanelFactory()
{
    static const ui::PanelFactory factory{
        .deviceId = "wavetable",
        .create = [](ui::DeviceUiContext& ctx) -> std::unique_ptr<ui::DevicePanel>
        { return std::make_unique<WavetablePanel>(ctx); },
    };
    return factory;
}
} // namespace fd::devices
