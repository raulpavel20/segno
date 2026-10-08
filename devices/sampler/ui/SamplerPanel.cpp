#include "SamplerPanel.h"

#include "Sampler.h"

#include <nlohmann/json.hpp>

namespace fd::devices
{
namespace
{
using S = Sampler;

bool isAudioFile(const juce::String& path)
{
    return juce::File(path).hasFileExtension("wav;wave;w64;aif;aiff;aifc;flac;mp3");
}

/// The loaded sample with the playback region, loop and live playheads. Drag the markers to
/// set Start, End and Loop Start; drop a file (or click when empty) to load one.
class WaveformView final : public juce::Component, public ui::ParamControl, public juce::FileDragAndDropTarget
{
public:
    explicit WaveformView(ui::DeviceUiContext& ctx) : ctx_(ctx), sampler_(dynamic_cast<const Sampler*>(ctx.device))
    {
        refresh();
    }

    void refresh() override
    {
        const auto sample = sampler_ != nullptr ? sampler_->sample() : nullptr;
        bool dirty = sample != sample_;
        sample_ = sample;
        for (size_t i = 0; i < watched_.size(); ++i)
        {
            const float v = ctx_.params.getPlain(watched_[i]);
            dirty |= !(v == last_[i]);
            last_[i] = v;
        }
        if (ctx_.analysis != nullptr)
            for (int i = 0; i < S::numPlayheads; ++i)
            {
                const float p = ctx_.analysis->read(S::FirstPlayhead + i);
                dirty |= !(p == playheads_[static_cast<size_t>(i)]);
                playheads_[static_cast<size_t>(i)] = p;
            }
        if (dirty)
            repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto& t = ui::Theme::current();
        const auto r = getLocalBounds().toFloat();
        g.setColour(dragOver_ ? t.surface1.brighter(0.08f) : t.surface0);
        g.fillRoundedRectangle(r, t.radiusControl);
        g.setFont(ui::fonts::font(t.textXs, ui::fonts::Weight::Medium));

        if (sample_ == nullptr)
        {
            const auto error = sampler_ != nullptr ? sampler_->loadError() : std::string{};
            g.setColour(error.empty() ? t.textSecondary : t.meterClip);
            g.drawFittedText(error.empty() ? juce::String("Drop a sample, or click to browse")
                                           : juce::String::fromUTF8(error.c_str()),
                             getLocalBounds().reduced(12), juce::Justification::centred, 3);
            return;
        }

        const auto wave = waveArea();
        const auto region = S::Region(S::regionFor(store(), sample_->frames));
        const bool oneShot = mode() == S::PlayMode::OneShot;
        const bool loop = !oneShot && ctx_.params.getPlain(S::Loop) > 0.5f;

        // Waveform: flat min/max columns; dimmer outside the playback region.
        const auto& lo = sample_->overviewMin;
        const auto& hi = sample_->overviewMax;
        const float mid = wave.getCentreY(), half = wave.getHeight() * 0.5f;
        for (int x = 0; x < static_cast<int>(wave.getWidth()); ++x)
        {
            const double f0 = static_cast<double>(x) / wave.getWidth() * sample_->frames;
            const double f1 = static_cast<double>(x + 1) / wave.getWidth() * sample_->frames;
            const auto b0 = static_cast<size_t>(f0 / SampleData::overviewBlock);
            const auto b1 = std::max(b0 + 1, static_cast<size_t>(f1 / SampleData::overviewBlock));
            float mn = 0.0f, mx = 0.0f;
            for (size_t b = b0; b < std::min(b1, lo.size()); ++b)
            {
                mn = std::min(mn, lo[b]);
                mx = std::max(mx, hi[b]);
            }
            const bool inside = f1 > region.start && f0 < region.end;
            g.setColour(inside ? t.value : t.textTertiary.withAlpha(0.6f));
            const float px = wave.getX() + static_cast<float>(x);
            g.fillRect(px, mid - mx * half, 1.0f, std::max(1.0f, (mx - mn) * half));
        }

        // Loop region tint, markers and playheads.
        if (loop)
        {
            g.setColour(t.value.withAlpha(0.10f));
            g.fillRect(juce::Rectangle<float>(xFor(region.loopStart), wave.getY(),
                                              xFor(region.end) - xFor(region.loopStart), wave.getHeight()));
        }
        drawMarker(g, S::Start, "S", t.textPrimary);
        drawMarker(g, S::End, "E", t.textPrimary);
        if (loop)
            drawMarker(g, S::LoopStart, "L", t.active);
        g.setColour(t.textPrimary.withAlpha(0.8f));
        for (float p : playheads_)
            if (p >= 0.0f)
                g.fillRect(xFor(static_cast<double>(p) * sample_->frames), wave.getY(), 1.0f, wave.getHeight());

        // File name and length.
        g.setColour(t.textSecondary);
        const auto name = juce::File(juce::String::fromUTF8(sample_->path.c_str())).getFileName();
        g.drawText(name + juce::String::fromUTF8("  \xC2\xB7  ") + juce::String(sample_->seconds(), 2) + " s",
                   getLocalBounds().reduced(6, 3).removeFromTop(14), juce::Justification::centredLeft, true);
    }

    // ---- markers ------------------------------------------------------------------------

    void mouseMove(const juce::MouseEvent& e) override
    {
        setMouseCursor(markerAt(e.position.x) ? juce::MouseCursor::LeftRightResizeCursor
                                              : juce::MouseCursor::NormalCursor);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (sample_ == nullptr)
        {
            browse();
            return;
        }
        dragging_ = markerAt(e.position.x);
        if (dragging_)
            ctx_.params.beginGesture(*dragging_);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!dragging_ || sample_ == nullptr)
            return;
        const auto wave = waveArea();
        const double frame =
            juce::jlimit(0.0, 1.0, static_cast<double>((e.position.x - wave.getX()) / wave.getWidth())) * sample_->frames;
        float value = static_cast<float>(frame / sample_->frames * 100.0);
        if (*dragging_ == S::LoopStart) // relative to the start..end region
        {
            const auto region = S::regionFor(store(), sample_->frames);
            value = static_cast<float>(
                juce::jlimit(0.0, 100.0, (frame - region.start) / (region.end - region.start) * 100.0));
        }
        ctx_.params.setPlain(*dragging_, value);
        refresh();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (dragging_)
            ctx_.params.endGesture(*dragging_);
        dragging_.reset();
    }

    void mouseDoubleClick(const juce::MouseEvent&) override { browse(); }

    // ---- files --------------------------------------------------------------------------

    bool isInterestedInFileDrag(const juce::StringArray& files) override
    {
        return files.size() == 1 && isAudioFile(files[0]);
    }
    void fileDragEnter(const juce::StringArray&, int, int) override { setDragOver(true); }
    void fileDragExit(const juce::StringArray&) override { setDragOver(false); }
    void filesDropped(const juce::StringArray& files, int, int) override
    {
        setDragOver(false);
        load(files[0]);
    }

private:
    [[nodiscard]] juce::Rectangle<float> waveArea() const
    {
        return getLocalBounds().toFloat().reduced(6.0f, 4.0f).withTrimmedTop(14.0f);
    }

    [[nodiscard]] float xFor(double frame) const
    {
        const auto wave = waveArea();
        return wave.getX() + static_cast<float>(frame / sample_->frames) * wave.getWidth();
    }

    [[nodiscard]] S::PlayMode mode() const
    {
        return static_cast<S::PlayMode>(juce::roundToInt(ctx_.params.getPlain(S::Mode)));
    }

    /// The region needs every parameter through a ParamStore; mirror the bound values.
    [[nodiscard]] const ParamStore& store()
    {
        for (size_t i = 0; i < mirror_.size(); ++i)
            mirror_.set(i, ctx_.params.getPlain(i));
        return mirror_;
    }

    [[nodiscard]] double markerFrame(size_t param)
    {
        const auto region = S::regionFor(store(), sample_->frames);
        return param == S::Start ? region.start : param == S::End ? region.end : region.loopStart;
    }

    void drawMarker(juce::Graphics& g, size_t param, const char* label, juce::Colour colour)
    {
        const auto wave = waveArea();
        const float x = xFor(markerFrame(param));
        g.setColour(colour);
        g.fillRect(x - 0.5f, wave.getY(), 1.0f, wave.getHeight());
        auto tag = juce::Rectangle<float>(12.0f, 11.0f).withPosition(param == S::End ? x - 12.0f : x, wave.getY());
        g.fillRect(tag);
        g.setColour(ui::Theme::current().surface0);
        g.drawText(label, tag, juce::Justification::centred, false);
    }

    [[nodiscard]] std::optional<size_t> markerAt(float x)
    {
        if (sample_ == nullptr)
            return std::nullopt;
        const bool loop = mode() == S::PlayMode::Classic && ctx_.params.getPlain(S::Loop) > 0.5f;
        std::optional<size_t> best;
        float bestDistance = 6.0f;
        for (size_t p : {S::LoopStart, S::Start, S::End})
        {
            if (p == S::LoopStart && !loop)
                continue;
            const float d = std::abs(xFor(markerFrame(p)) - x);
            if (d < bestDistance)
            {
                best = p;
                bestDistance = d;
            }
        }
        return best;
    }

    void setDragOver(bool over)
    {
        dragOver_ = over;
        repaint();
    }

    void browse()
    {
        chooser_ = std::make_unique<juce::FileChooser>("Load a sample", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe = juce::Component::SafePointer<WaveformView>(this)](const juce::FileChooser& fc) {
                                  if (safe != nullptr && fc.getResult().existsAsFile())
                                      safe->load(fc.getResult().getFullPathName());
                              });
    }

    /// Through the host (ADR 0017); then the file's root note, if it has one.
    void load(const juce::String& path)
    {
        ctx_.params.applyExtraState({{"sample.path", path.toStdString()}});
        refresh();
        if (sample_ != nullptr && sample_->rootKey)
        {
            ctx_.params.beginGesture(S::Root);
            ctx_.params.setPlain(S::Root, static_cast<float>(*sample_->rootKey));
            ctx_.params.endGesture(S::Root);
        }
    }

    ui::DeviceUiContext& ctx_;
    const Sampler* sampler_;
    std::shared_ptr<const SampleData> sample_;
    ParamStore mirror_{ctx_.info.params};
    const std::array<size_t, 5> watched_{S::Mode, S::Start, S::End, S::Loop, S::LoopStart};
    std::array<float, 5> last_{};
    std::array<float, S::numPlayheads> playheads_{};
    std::optional<size_t> dragging_;
    std::unique_ptr<juce::FileChooser> chooser_;
    bool dragOver_ = false;
};

/// Invisible helper: runs `fn` on every panel refresh (e.g. to dim controls a mode ignores).
struct RefreshHook final : juce::Component, ui::ParamControl
{
    std::function<void()> fn;
    void refresh() override { fn(); }
};

/// Layout per devices/sampler/SPEC.md: waveform on the left, then pitch and level, playback
/// region, envelopes, filter, and voice settings; two rows like the other instruments.
class SamplerPanel final : public ui::DevicePanel, public juce::FileDragAndDropTarget
{
public:
    explicit SamplerPanel(ui::DeviceUiContext& ctx) : DevicePanel(ctx)
    {
        auto knob = [&](size_t i) { return &bind(std::make_unique<ui::ParamKnob>(ctx, i)); };
        auto bar = [&](size_t i) {
            auto* k = knob(i);
            k->setStyle(ui::Knob::Style::Bar);
            return k;
        };
        auto toggle = [&](size_t i) { return &bind(std::make_unique<ui::ParamToggle>(ctx, i)); };

        wave_ = &bind(std::make_unique<WaveformView>(ctx));
        pitch_ = {knob(S::Transpose), knob(S::Detune), knob(S::Gain), knob(S::VelAmp)};

        mode_ = &bind(std::make_unique<ui::ParamSegmented>(ctx, S::Mode, 2));
        loop_ = toggle(S::Loop);
        gate_ = toggle(S::OneShotGate);
        start_ = bar(S::Start);
        end_ = bar(S::End);
        region_ = {knob(S::LoopStart), knob(S::LoopXfade), knob(S::FadeIn), knob(S::FadeOut)};

        ampView_ = &bind(std::make_unique<ui::EnvelopeDisplay>(ctx, "AMP", S::AmpA));
        amp_ = {knob(S::AmpA), knob(S::AmpD), knob(S::AmpS), knob(S::AmpR)};
        fenvView_ = &bind(std::make_unique<ui::EnvelopeDisplay>(ctx, "FILTER", S::FenvA));
        fenv_ = {knob(S::FenvA), knob(S::FenvD), knob(S::FenvS), knob(S::FenvR)};

        filterOn_ = toggle(S::FilterOn);
        filterType_ = &bind(std::make_unique<ui::ParamSegmented>(ctx, S::FilterType, 4));
        filterView_ = &bind(std::make_unique<ui::FilterDisplay>(ctx, S::FilterType, S::FilterCutoff, S::FilterRes,
                                                                S::FilterOn));
        filter_ = {knob(S::FilterCutoff), knob(S::FilterRes), knob(S::FilterEnv), knob(S::FilterKey)};

        voiceMode_ = &bind(std::make_unique<ui::ParamComboBox>(ctx, S::VoiceMode));
        voice_ = {bar(S::Root), bar(S::VoiceCount), bar(S::VoiceGlide), bar(S::VoiceBend)};

        // Loop applies to Classic, Gate to One-shot: dim the one the mode ignores.
        auto* hook = &bind(std::make_unique<RefreshHook>());
        hook->fn = [this, &ctx] {
            const bool oneShot = juce::roundToInt(ctx.params.getPlain(S::Mode)) == 1;
            loop_->setAlpha(oneShot ? 0.35f : 1.0f);
            gate_->setAlpha(oneShot ? 1.0f : 0.35f);
        };
        hook->refresh();

        if (auto* bus = ctx.analysis)
            meter_.setLevelSource([bus](int) { return bus->takePeak(S::PeakOut); });
        addAndMakeVisible(meter_);
    }

    int preferredWidth() const override { return 1346; }

    // Dropping anywhere on the panel loads the file (the waveform view handles its own area).
    bool isInterestedInFileDrag(const juce::StringArray& files) override { return wave_->isInterestedInFileDrag(files); }
    void filesDropped(const juce::StringArray& files, int x, int y) override { wave_->filesDropped(files, x, y); }

    void layoutBody(juce::Rectangle<int> body) override
    {
        constexpr int gap = 16, knobW = 46, regionKnobW = 50, filterKnobW = 48, cellW = 76, smallGap = 4, lineH = 18;
        const int rowH = body.getHeight() / 2;
        auto rows = [&](int width) {
            auto block = body.removeFromLeft(width);
            body.removeFromLeft(gap);
            return std::pair{block.removeFromTop(rowH), block};
        };
        auto knobs = [](juce::Rectangle<int> r, auto& list, int w) {
            for (auto* k : list)
                k->setBounds(r.removeFromLeft(w));
        };

        meter_.setBounds(body.removeFromRight(9).reduced(0, 4));
        body.removeFromRight(8);

        wave_->setBounds(body.removeFromLeft(340).reduced(0, 3));
        body.removeFromLeft(gap);

        // Pitch and level.
        {
            auto [r1, r2] = rows(2 * knobW);
            r1.setWidth(2 * knobW);
            pitch_[0]->setBounds(r1.removeFromLeft(knobW));
            pitch_[1]->setBounds(r1);
            pitch_[2]->setBounds(r2.removeFromLeft(knobW));
            pitch_[3]->setBounds(r2);
        }
        // Playback region: mode, loop/gate, start/end on top; loop and fades below.
        {
            auto [r1, r2] = rows(4 * regionKnobW);
            r1.reduce(0, 3);
            mode_->setBounds(r1.removeFromTop(lineH));
            r1.removeFromTop(smallGap);
            auto line = r1.removeFromTop(lineH);
            loop_->setBounds(line.removeFromLeft(line.getWidth() / 2 - 2));
            line.removeFromLeft(smallGap);
            gate_->setBounds(line);
            r1.removeFromTop(smallGap);
            line = r1.removeFromTop(lineH);
            start_->setBounds(line.removeFromLeft(line.getWidth() / 2 - 2));
            line.removeFromLeft(smallGap);
            end_->setBounds(line);
            knobs(r2, region_, regionKnobW);
        }
        // Envelopes: amp on top, filter envelope below.
        {
            auto [r1, r2] = rows(cellW + smallGap + 4 * knobW);
            ampView_->setBounds(r1.removeFromLeft(cellW).reduced(0, 3));
            r1.removeFromLeft(smallGap);
            knobs(r1, amp_, knobW);
            fenvView_->setBounds(r2.removeFromLeft(cellW).reduced(0, 3));
            r2.removeFromLeft(smallGap);
            knobs(r2, fenv_, knobW);
        }
        // Filter: on/type and response on top, knobs below.
        {
            auto [r1, r2] = rows(4 * filterKnobW);
            r1.reduce(0, 3);
            auto line = r1.removeFromTop(lineH);
            filterOn_->setBounds(line.removeFromLeft(46));
            line.removeFromLeft(smallGap);
            filterType_->setBounds(line);
            r1.removeFromTop(smallGap);
            filterView_->setBounds(r1);
            knobs(r2, filter_, filterKnobW);
        }
        // Voice: mode and bars, stacked over both rows.
        {
            auto column = body.reduced(0, 3);
            const int lines = 1 + static_cast<int>(voice_.size());
            const int h = std::min(lineH + 2, (column.getHeight() - (lines - 1) * smallGap) / lines);
            voiceMode_->setBounds(column.removeFromTop(h));
            for (auto* b : voice_)
            {
                column.removeFromTop(smallGap);
                b->setBounds(column.removeFromTop(h));
            }
        }
    }

private:
    WaveformView* wave_ = nullptr;
    std::array<ui::ParamKnob*, 4> pitch_{}, region_{}, amp_{}, fenv_{}, filter_{}, voice_{};
    ui::ParamSegmented *mode_ = nullptr, *filterType_ = nullptr;
    ui::ParamToggle *loop_ = nullptr, *gate_ = nullptr, *filterOn_ = nullptr;
    ui::ParamKnob *start_ = nullptr, *end_ = nullptr;
    ui::EnvelopeDisplay *ampView_ = nullptr, *fenvView_ = nullptr;
    ui::FilterDisplay* filterView_ = nullptr;
    ui::ParamComboBox* voiceMode_ = nullptr;
    ui::Meter meter_{1};
};
} // namespace

const ui::PanelFactory& samplerPanelFactory()
{
    static const ui::PanelFactory factory{
        .deviceId = "sampler",
        .create = [](ui::DeviceUiContext& ctx) -> std::unique_ptr<ui::DevicePanel> {
            return std::make_unique<SamplerPanel>(ctx);
        },
    };
    return factory;
}
} // namespace fd::devices
