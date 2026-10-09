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

/// Sections per devices/sampler/SPEC.md, on the module grid (docs/design/device-panels.md).
class SamplerPanel final : public ui::DevicePanel, public juce::FileDragAndDropTarget
{
public:
    explicit SamplerPanel(ui::DeviceUiContext& ctx) : DevicePanel(ctx)
    {
        auto& sample = addSection("sample", "Sample", 6);
        wave_ = &own(std::make_unique<WaveformView>(ctx));
        sample.place(*wave_, 0, 0, 6, 6);

        auto& pitch = addSection("pitch", "Pitch", 1);
        pitch.knob(S::Transpose, 0, 0);
        pitch.knob(S::Detune, 0, 3);

        auto& level = addSection("level", "Level", 1);
        level.knob(S::Gain, 0, 0);
        level.knob(S::VelAmp, 0, 3);

        // Loop under Classic, Gate under One-shot: the one the mode ignores dims.
        auto& region = addSection("region", "Region", 4);
        region.segmented(S::Mode, 0, 0, 4, 1);
        auto& loop = region.toggle(S::Loop, 0, 1, 2);
        auto& gate = region.toggle(S::OneShotGate, 2, 1, 2);
        region.bar(S::Start, 0, 2, 2);
        region.bar(S::End, 2, 2, 2);
        region.knob(S::LoopStart, 0, 3);
        region.knob(S::LoopXfade, 1, 3);
        auto oneShot = [&ctx] { return juce::roundToInt(ctx.params.getPlain(S::Mode)) == 1; };
        dimWhen({&loop}, oneShot);
        dimWhen({&gate}, [oneShot] { return !oneShot(); });

        auto& fades = addSection("fades", "Fades", 1);
        fades.knob(S::FadeIn, 0, 0).setName("In");
        fades.knob(S::FadeOut, 0, 3).setName("Out");

        auto& env = addSection("env", "Envelopes", 6);
        env.place(own(std::make_unique<ui::EnvelopeDisplay>(ctx, "Amp", S::AmpA)), 0, 0, 2, 3);
        env.place(own(std::make_unique<ui::EnvelopeDisplay>(ctx, "Filter", S::FenvA)), 0, 3, 2, 3);
        for (size_t i = 0; i < 4; ++i)
        {
            env.knob(S::AmpA + i, 2 + static_cast<int>(i), 0);
            env.knob(S::FenvA + i, 2 + static_cast<int>(i), 3);
        }

        // The section's power is the filter's On: off dims the whole section.
        auto& filter = addSection("filter", "Filter", 4, S::FilterOn);
        filter.segmented(S::FilterType, 0, 0, 4, 1);
        filter.place(own(std::make_unique<ui::FilterDisplay>(ctx, S::FilterType, S::FilterCutoff, S::FilterRes)), 0, 1, 4, 2);
        filter.knob(S::FilterCutoff, 0, 3);
        filter.knob(S::FilterRes, 1, 3);
        filter.knob(S::FilterEnv, 2, 3);
        filter.knob(S::FilterKey, 3, 3);

        auto& voice = addSection("voice", "Voice", 2);
        voice.combo(S::VoiceMode, 0, 0, 2);
        voice.bar(S::Root, 0, 1, 2);
        voice.bar(S::VoiceCount, 0, 2, 2);
        voice.bar(S::VoiceGlide, 0, 3, 2);
        voice.bar(S::VoiceBend, 0, 4, 2);

        if (auto* bus = ctx.analysis)
            meter_.setLevelSource([bus](int) { return bus->takePeak(S::PeakOut); });
        setMeters({{&meter_, "Out"}});
    }

    // Dropping anywhere on the panel loads the file (the waveform view handles its own area).
    bool isInterestedInFileDrag(const juce::StringArray& files) override { return wave_->isInterestedInFileDrag(files); }
    void filesDropped(const juce::StringArray& files, int x, int y) override { wave_->filesDropped(files, x, y); }

private:
    WaveformView* wave_ = nullptr;
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
