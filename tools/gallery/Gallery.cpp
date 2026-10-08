#include "Gallery.h"

#include <fd/device/Realtime.h>

namespace fd::gallery
{
namespace
{
// ---------------------------------------------------------------------------
// A mock device that only exists to show controls in their states.
// ---------------------------------------------------------------------------
constexpr std::string_view kModes[] = {"Clean", "Warm", "Hot"};
constexpr std::array<ParamSpec, 6> kDemoParams{{
    {.id = "drive", .name = "Drive", .min = 0.0f, .max = 100.0f, .def = 35.0f, .unit = Unit::Percent, .decimals = 0},
    {.id = "freq", .name = "Freq", .min = 20.0f, .max = 20000.0f, .def = 1200.0f, .skewCentre = 632.0f,
     .unit = Unit::Hertz, .decimals = 0},
    {.id = "pan", .name = "Pan", .min = -1.0f, .max = 1.0f, .def = -0.3f, .unit = Unit::Pan},
    {.id = "mode", .name = "Mode", .kind = ParamKind::Choice, .min = 0.0f, .max = 2.0f, .def = 1.0f, .choices = kModes},
    {.id = "sync", .name = "Sync", .kind = ParamKind::Toggle, .min = 0.0f, .max = 1.0f, .def = 1.0f},
    {.id = "hq", .name = "HQ", .kind = ParamKind::Toggle, .min = 0.0f, .max = 1.0f, .def = 0.0f},
}};
constexpr DeviceInfo kDemoInfo{.id = "gallery-controls", .name = "Controls", .params = kDemoParams};

class DemoDevice final : public Device
{
public:
    DemoDevice() : Device(kDemoInfo) {}
    void prepare(const ProcessSpec&) override {}
    void reset() noexcept override {}
    void process(ProcessContext&) noexcept override {}
};

class DemoPanel final : public ui::DevicePanel
{
public:
    explicit DemoPanel(ui::DeviceUiContext& ctx) : DevicePanel(ctx)
    {
        for (size_t i = 0; i < 3; ++i)
            knobs_.push_back(&bind(std::make_unique<ui::ParamKnob>(ctx, i)));
        mode_ = &bind(std::make_unique<ui::ParamKnob>(ctx, 3));
        sync_ = &bind(std::make_unique<ui::ParamToggle>(ctx, 4));
        hq_ = &bind(std::make_unique<ui::ParamToggle>(ctx, 5));
    }

    int preferredWidth() const override { return 236; }

    void layoutBody(juce::Rectangle<int> body) override
    {
        const auto& t = ui::Theme::current();
        auto toggles = body.removeFromBottom(20);
        body.removeFromBottom(t.grid * 2);
        const int w = body.getWidth() / 4;
        for (auto* k : knobs_)
            k->setBounds(body.removeFromLeft(w));
        mode_->setBounds(body);
        const int pillW = (toggles.getWidth() - t.grid) / 2;
        sync_->setBounds(toggles.removeFromLeft(pillW));
        hq_->setBounds(toggles.removeFromRight(pillW));
    }

private:
    std::vector<ui::ParamKnob*> knobs_;
    ui::ParamKnob* mode_ = nullptr;
    ui::ParamToggle* sync_ = nullptr;
    ui::ParamToggle* hq_ = nullptr;
};

// ---------------------------------------------------------------------------
// Arrangement mock: track headers, ruler, flat clips, an automation lane, the playhead.
// ---------------------------------------------------------------------------
class ArrangementMock final : public juce::Component
{
public:
    void paint(juce::Graphics& g) override
    {
        const auto& t = ui::Theme::current();
        auto area = getLocalBounds();
        g.setColour(t.surface0);
        g.fillRect(area);

        constexpr int headerW = 150;
        constexpr int rulerH = 22;
        auto ruler = area.removeFromTop(rulerH);
        auto headers = area.removeFromLeft(headerW);
        g.setColour(t.surface1);
        g.fillRect(ruler);
        g.fillRect(headers);

        const float x0 = static_cast<float>(headers.getRight());
        const float barW = (static_cast<float>(getWidth()) - x0) / 8.0f;
        g.setFont(ui::fonts::mono(t.textXs, ui::fonts::Weight::Regular));
        for (int bar = 0; bar < 8; ++bar)
        {
            const float x = x0 + static_cast<float>(bar) * barW;
            g.setColour(t.textTertiary);
            g.drawText(juce::String(bar + 1), juce::Rectangle<float>(x + 4.0f, 0.0f, 30.0f, rulerH),
                       juce::Justification::centredLeft);
            g.setColour(t.line);
            g.fillRect(juce::Rectangle<float>(x, static_cast<float>(rulerH), 1.0f, static_cast<float>(getHeight())));
        }

        struct Lane
        {
            const char* name;
            int colour;
            bool automation;
            std::vector<std::pair<float, float>> clips; // bar start, length
        };
        const std::vector<Lane> lanes{
            {"Drums", 0, false, {{0.0f, 4.0f}, {4.0f, 4.0f}}},
            {"Bass", 4, true, {{0.0f, 2.0f}, {2.0f, 2.0f}, {5.0f, 3.0f}}},
            {"Pad", 5, false, {{1.0f, 6.0f}}},
            {"Lead", 2, false, {{4.0f, 2.0f}, {6.5f, 1.5f}}},
        };

        int y = rulerH;
        constexpr int laneH = 54;
        constexpr int autoH = 40;
        for (const auto& lane : lanes)
        {
            const auto colour = t.clipColours[lane.colour];
            const auto laneArea = juce::Rectangle<int>(0, y, getWidth(), laneH);

            // Track header: colour tag + name.
            g.setColour(colour);
            g.fillRect(juce::Rectangle<int>(headers.getX(), y + 1, 3, laneH - 2));
            g.setColour(t.textPrimary);
            g.setFont(ui::fonts::font(t.textSm, ui::fonts::Weight::Medium));
            g.drawText(lane.name, juce::Rectangle<int>(12, y, headerW - 16, laneH), juce::Justification::centredLeft);

            for (auto [start, length] : lane.clips)
                paintClip(g, juce::Rectangle<float>(x0 + start * barW + 1.0f, static_cast<float>(y) + 2.0f,
                                                    length * barW - 2.0f, laneH - 4.0f),
                          colour, lane.name);

            g.setColour(t.line);
            g.fillRect(laneArea.withY(y + laneH - 1).withHeight(1));
            y += laneH;

            if (lane.automation)
            {
                g.setColour(t.textTertiary);
                g.setFont(ui::fonts::font(t.textXs));
                g.drawText("Filter cutoff", juce::Rectangle<int>(20, y, headerW - 24, autoH),
                           juce::Justification::centredLeft);
                paintAutomation(g, juce::Rectangle<float>(x0, static_cast<float>(y), static_cast<float>(getWidth()) - x0,
                                                          static_cast<float>(autoH)));
                g.setColour(t.line);
                g.fillRect(laneArea.withY(y + autoH - 1).withHeight(1));
                y += autoH;
            }
        }

        // Playhead.
        g.setColour(t.textPrimary.withAlpha(0.9f));
        g.fillRect(juce::Rectangle<float>(x0 + 3.35f * barW, static_cast<float>(rulerH), 1.0f,
                                          static_cast<float>(y - rulerH)));
    }

private:
    static void paintClip(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour colour, const char* name)
    {
        const auto& t = ui::Theme::current();
        // Flat: dark body in the clip's hue, a solid title strip, a waveform in a light tint.
        g.setColour(colour.withMultipliedBrightness(0.32f).withMultipliedSaturation(0.8f));
        g.fillRoundedRectangle(r, 2.0f);
        auto title = r.removeFromTop(14.0f);
        g.setColour(colour);
        g.fillRoundedRectangle(title, 2.0f);
        g.fillRect(title.withTrimmedTop(7.0f));
        g.setColour(t.surface0);
        g.setFont(ui::fonts::font(t.textXs, ui::fonts::Weight::SemiBold));
        g.drawText(name, title.reduced(5.0f, 0.0f), juce::Justification::centredLeft, true);

        juce::Random rng(static_cast<juce::int64>(r.getX() * 13.0f + r.getY()));
        g.setColour(colour.brighter(0.5f).withAlpha(0.9f));
        const float mid = r.getCentreY();
        for (float x = r.getX() + 2.0f; x < r.getRight() - 2.0f; x += 2.0f)
        {
            const float env = 0.35f + 0.65f * std::abs(std::sin(x * 0.045f));
            const float h = (r.getHeight() * 0.42f) * env * (0.6f + 0.4f * rng.nextFloat());
            g.fillRect(x, mid - h, 1.0f, 2.0f * h);
        }
    }

    static void paintAutomation(juce::Graphics& g, juce::Rectangle<float> r)
    {
        const auto& t = ui::Theme::current();
        const std::vector<juce::Point<float>> points{{0.00f, 0.75f}, {0.18f, 0.35f}, {0.30f, 0.35f}, {0.52f, 0.85f},
                                                     {0.70f, 0.20f}, {0.86f, 0.55f}, {1.00f, 0.55f}};
        juce::Path curve;
        for (size_t i = 0; i < points.size(); ++i)
        {
            const auto p = juce::Point<float>(r.getX() + points[i].x * r.getWidth(),
                                              r.getY() + 4.0f + points[i].y * (r.getHeight() - 8.0f));
            i == 0 ? curve.startNewSubPath(p) : curve.lineTo(p);
        }
        juce::Path fill(curve);
        fill.lineTo(r.getBottomRight());
        fill.lineTo(r.getBottomLeft());
        fill.closeSubPath();

        // The one permitted gradient (ui-principles §4): automation.
        g.setGradientFill(juce::ColourGradient(t.effectStart.withAlpha(0.40f), r.getX(), r.getY(),
                                               t.effectEnd.withAlpha(0.04f), r.getX(), r.getBottom(), false));
        g.fillPath(fill);
        g.setColour(t.effectStart);
        g.strokePath(curve, juce::PathStrokeType(1.5f));
        for (const auto& pt : points)
            g.fillEllipse(juce::Rectangle<float>(5.0f, 5.0f).withCentre(
                {r.getX() + pt.x * r.getWidth(), r.getY() + 4.0f + pt.y * (r.getHeight() - 8.0f)}));
    }
};
} // namespace

GalleryComponent::GalleryComponent(ui::LookAndFeel& lnf) : lnf_(lnf)
{
    const auto& themes = ui::Theme::candidates();
    for (int i = 0; i < themes.size(); ++i)
    {
        auto* b = themeButtons_.add(new ui::Toggle(themes[i].name));
        b->setRadioGroupId(1);
        b->setTooltip(themes[i].description);
        b->onClick = [this, i] { selectTheme(i); };
        addAndMakeVisible(b);
    }

    auto addDevice = [this](std::unique_ptr<Device> device, const ui::PanelFactory* panelFactory,
                            std::unique_ptr<ui::DevicePanel> (*fallback)(ui::DeviceUiContext&)) {
        DeviceSlot slot;
        slot.device = std::move(device);
        slot.binding = std::make_unique<ui::LocalParamBinding>(*slot.device);
        slot.context = std::make_unique<ui::DeviceUiContext>(
            ui::DeviceUiContext{slot.device->info(), *slot.binding, &slot.device->analysis(), slot.device.get()});
        slot.panel = panelFactory != nullptr ? panelFactory->create(*slot.context) : fallback(*slot.context);
        slot.panel->onEnabledChange = [](bool) {};
        addAndMakeVisible(*slot.panel);
        chain_.push_back(std::move(slot));
    };
    addDevice(findDevice("utility")->create(), ui::findPanel("utility"), nullptr);
    addDevice(std::make_unique<DemoDevice>(), nullptr,
              [](ui::DeviceUiContext& ctx) -> std::unique_ptr<ui::DevicePanel> { return std::make_unique<DemoPanel>(ctx); });

    for (auto& slot : chain_)
        slot.device->prepare({.sampleRate = 48000.0, .maxBlockSize = 2048});

    // Demo signal for the meters: two detuned tones with a slow swell.
    demoL_.resize(96000);
    demoR_.resize(96000);
    for (size_t i = 0; i < demoL_.size(); ++i)
    {
        const double t = static_cast<double>(i) / 48000.0;
        const double swell = 0.35 + 0.3 * std::sin(juce::MathConstants<double>::twoPi * 0.5 * t);
        demoL_[i] = static_cast<float>(swell * std::sin(juce::MathConstants<double>::twoPi * 220.0 * t));
        demoR_[i] = static_cast<float>(swell * 0.8 * std::sin(juce::MathConstants<double>::twoPi * 330.0 * t));
    }

    arrangement_ = std::make_unique<ArrangementMock>();
    addAndMakeVisible(*arrangement_);

    selectTheme(0);
    startTimerHz(30);
}

GalleryComponent::~GalleryComponent()
{
    stopTimer();
}

void GalleryComponent::runDemoAudio(int numSamples)
{
    std::vector<float> l(static_cast<size_t>(numSamples)), r(static_cast<size_t>(numSamples));
    for (int i = 0; i < numSamples; ++i)
    {
        const auto idx = static_cast<size_t>((demoPos_ + i) % static_cast<std::int64_t>(demoL_.size()));
        l[static_cast<size_t>(i)] = demoL_[idx];
        r[static_cast<size_t>(i)] = demoR_[idx];
    }
    demoPos_ += numSamples;
    float* ptrs[] = {l.data(), r.data()};
    for (auto& slot : chain_)
    {
        ProcessContext ctx;
        ctx.input = {ptrs, 2};
        ctx.output = {ptrs, 2};
        ctx.numSamples = numSamples;
        processRealtime(*slot.device, ctx);
    }
}

void GalleryComponent::selectTheme(int index)
{
    themeIndex_ = index;
    ui::Theme::setCurrent(ui::Theme::candidates().getReference(index));
    lnf_.applyTheme();
    themeButtons_[index]->setToggleState(true, juce::dontSendNotification);
    sendLookAndFeelChange();
    resized();
    repaint();
}

void GalleryComponent::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    g.fillAll(t.surface0);

    auto top = getLocalBounds().removeFromTop(44);
    g.setColour(t.surface1);
    g.fillRect(top);
    g.setColour(t.textPrimary);
    g.setFont(ui::fonts::font(t.textMd, ui::fonts::Weight::SemiBold));
    g.drawText("Theme gallery", top.reduced(16, 0), juce::Justification::centredLeft);
    g.setColour(t.textTertiary);
    g.setFont(ui::fonts::font(t.textSm));
    g.drawText(t.description, top.reduced(16, 0).withTrimmedLeft(130), juce::Justification::centredLeft);

    // Detail panel (bottom), where the device chain lives.
    auto detail = getLocalBounds().removeFromBottom(t.devicePanelHeight + 2 * t.grid * 3);
    g.setColour(t.surface1);
    g.fillRect(detail);
}

void GalleryComponent::resized()
{
    const auto& t = ui::Theme::current();
    auto area = getLocalBounds();
    auto top = area.removeFromTop(44).reduced(16, 10);
    for (int i = themeButtons_.size(); --i >= 0;)
    {
        themeButtons_[i]->setBounds(top.removeFromRight(84));
        top.removeFromRight(6);
    }

    auto detail = area.removeFromBottom(t.devicePanelHeight + 2 * t.grid * 3).reduced(t.grid * 3);
    for (auto& slot : chain_)
    {
        slot.panel->setBounds(detail.removeFromLeft(slot.panel->preferredWidth()));
        detail.removeFromLeft(t.grid * 2);
    }
    arrangement_->setBounds(area);
}
} // namespace fd::gallery
