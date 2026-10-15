#include "ui/ModulatorPanels.h"

#include <algorithm>
#include <cmath>
#include <fd/dsp/Modulators.h>
#include <fd/dsp/VoiceModules.h>

namespace fd::app
{
namespace
{
namespace mod = fd::dsp::mod;
using model::ModulatorKind;
using namespace model::modulator;

constexpr float kTraceStroke = 1.5f; // as the modules' displays draw their curves
constexpr int kMostCopies = 8;       // traces drawn; past them, the handle's count says (§9)
constexpr double kRandomSteps = 8.0; // how many of Random's steps its display shows

/// " · ", between the parts of a readout or a title.
juce::String dotted()
{
    return juce::String::fromUTF8(" \xC2\xB7 ");
}

/// The parameters each display draws from: a change repaints it.
std::vector<size_t> drawnFrom(ModulatorKind kind)
{
    std::vector<size_t> all(model::modulatorInfo(kind).params.size());
    for (size_t i = 0; i < all.size(); ++i)
        all[i] = i;
    return all;
}

/// `ms` as the display spaces an envelope's stage: by the log of its time, so short and long both
/// show.
float stageWidth(double ms)
{
    return static_cast<float>(std::log1p(std::max(0.0, ms) / 5.0)) + 0.4f;
}
} // namespace

// ---- the modulator as a source ----------------------------------------------------------------

/// One source, the modulator itself: what its handles read for their name and lit state. Its
/// own controls aren't its targets (a route to itself would be a loop, §8).
struct ModulatorPanel::Self final : ui::ModulationModel
{
    juce::String name;
    [[nodiscard]] std::vector<SourceInfo> sources() const override { return {{0, name, {}}}; }
    [[nodiscard]] std::vector<Route> routes() const override { return {}; }
    [[nodiscard]] bool canModulate(size_t) const override { return false; }
    [[nodiscard]] juce::String sourceName(int) const override { return name; }
    bool addRoute(int, size_t) override { return false; }
    [[nodiscard]] bool isFull() const override { return false; }
    void removeRoute(int) override {}
    void beginAmount(int) override {}
    void setAmount(int, float) override {}
    void endAmount(int) override {}
};

// ---- the display ------------------------------------------------------------------------------

/// A modulator's display (modulators.md §9): a readout line with the handle at its right, and its
/// output drawn from fd::dsp::mod, the same functions the engine plays. With Per Target, one trace
/// for each target, over each other, each a step fainter (`Theme::echo`), and each copy's dot
/// where it is now.
class ModulatorPanel::Display final : public ui::MiniDisplay
{
public:
    Display(ui::DeviceUiContext& context, ModulatorKind kind, ModulatorHost& host)
        : MiniDisplay(context, {}, drawnFrom(kind)), ctx_(context), kind_(kind), host_(host)
    {
    }

    void setHandle(ui::ModulationHandle& handle)
    {
        handle_ = &handle;
        fitHandle();
    }

    /// The handle's width follows its count and lock.
    void fitHandle()
    {
        if (handle_ != nullptr)
            setAccessory(*handle_, handle_->preferredWidth());
    }

    void refresh() override
    {
        MiniDisplay::refresh();
        // What moves without a parameter changing: where it is, its targets, its roll, what it heard.
        const auto position = host_.position();
        const auto routes = host_.routeCount();
        const auto roll = host_.roll();
        if (position != position_ || routes != routes_ || roll != roll_ || kind_ == ModulatorKind::Follower)
        {
            position_ = position;
            routes_ = routes;
            roll_ = roll;
            repaint();
        }
    }

private:
    [[nodiscard]] float value(size_t index) const { return ctx_.params.getPlain(index); }
    [[nodiscard]] bool on(size_t index) const { return value(index) >= 0.5f; }
    [[nodiscard]] juce::String text(size_t index) const
    {
        return juce::String(ctx_.info.params[index].toText(value(index)));
    }

    /// How many copies it draws: one for each target with Per Target (up to kMostCopies), else one.
    [[nodiscard]] int copies() const
    {
        const auto perTarget = model::perTargetParam(kind_);
        return perTarget && on(*perTarget) ? std::clamp(host_.routeCount(), 1, kMostCopies) : 1;
    }

    [[nodiscard]] bool bothWays() const
    {
        const auto both = model::bothWaysParam(kind_);
        return both && on(*both);
    }

    /// Where a value (-1 ... +1) lands: around the middle while it swings both ways, up from the
    /// bottom (0 at the foot) while it moves one way.
    [[nodiscard]] float yFor(juce::Rectangle<float> plot, double v, bool both) const
    {
        const double u = both ? 0.5 + 0.5 * v : 0.5 * (v + 1.0);
        return plot.getBottom() - static_cast<float>(std::clamp(u, 0.0, 1.0)) * plot.getHeight();
    }

    [[nodiscard]] juce::String readout() const
    {
        const auto rate = [&](size_t rateParam, size_t syncParam)
        { return value(syncParam) >= 0.5f ? text(syncParam) : text(rateParam); };
        switch (kind_)
        {
        case ModulatorKind::Lfo:
            return rate(Lfo::Rate, Lfo::Sync);
        case ModulatorKind::Random:
            return rate(Random::Rate, Random::Sync) +
                   (on(Random::Freeze) ? dotted() + "frozen" : juce::String());
        case ModulatorKind::Envelope:
        {
            const int trigger = static_cast<int>(value(Envelope::Trigger));
            const juce::String fires = trigger == 0   ? juce::String("On notes")
                                       : trigger == 1 ? "Every " + text(Envelope::Every)
                                                      : juce::String("On Fire");
            return fires + (on(Envelope::Legato) && trigger == 0 ? juce::String(", legato") : juce::String());
        }
        case ModulatorKind::Follower:
        {
            const auto heard = host_.heard();
            const float level = heard.empty() ? 0.0f : heard.back();
            return level <= 1.0e-5f ? juce::String("Silent")
                                    : juce::String(20.0 * std::log10(static_cast<double>(level)), 1) + " dB";
        }
        case ModulatorKind::Steps:
            return juce::String(static_cast<int>(value(Steps::Length))) + " steps" + dotted() +
                   rate(Steps::Rate, Steps::Sync);
        }
        return {};
    }

    void drawContent(juce::Graphics& g, juce::Rectangle<float> area) override
    {
        const auto& t = ui::Theme::current();
        const auto inset = static_cast<float>(t.deviceGrid.displayInset);
        auto line = area.removeFromTop(static_cast<float>(t.deviceGrid.displayTextHeight));
        if (handle_ != nullptr)
            line.removeFromRight(static_cast<float>(handle_->getWidth()) + inset);
        g.setFont(ui::font(ui::TextStyle::ValueSmall));
        g.setColour(t.textSecondary);
        g.drawText(readout(), line, juce::Justification::centredLeft, true);
        const auto plot = area.withTrimmedTop(inset * 0.5f).reduced(0.0f, inset * 0.25f);
        switch (kind_)
        {
        case ModulatorKind::Lfo:
            drawLfo(g, plot);
            break;
        case ModulatorKind::Random:
            drawRandom(g, plot);
            break;
        case ModulatorKind::Envelope:
            drawEnvelope(g, plot);
            break;
        case ModulatorKind::Follower:
            drawFollower(g, plot);
            break;
        case ModulatorKind::Steps:
            drawSteps(g, plot);
            break;
        }
    }

    /// The middle line (both ways) or the foot (one way): where the knob is set.
    void baseline(juce::Graphics& g, juce::Rectangle<float> plot, bool both) const
    {
        const auto& t = ui::Theme::current();
        g.setColour(t.surface3);
        g.drawHorizontalLine(juce::roundToInt(both ? plot.getCentreY() : plot.getBottom() - 0.5f),
                             plot.getX(), plot.getRight());
    }

    /// One trace per copy across the plot, back to front (the first on top), and each copy's dot
    /// at `now` (0..1 across), where `value(copy, u)` (u 0..1 across) has it.
    template <typename Value>
    void traces(juce::Graphics& g, juce::Rectangle<float> plot, bool both, double now, Value value) const
    {
        const auto& t = ui::Theme::current();
        const int n = std::max(16, static_cast<int>(plot.getWidth()));
        for (int copy = copies() - 1; copy >= 0; --copy)
        {
            juce::Path path;
            for (int i = 0; i <= n; ++i)
            {
                const double u = static_cast<double>(i) / n;
                const juce::Point<float> p(plot.getX() + static_cast<float>(u) * plot.getWidth(),
                                           yFor(plot, value(copy, u), both));
                i == 0 ? path.startNewSubPath(p) : path.lineTo(p);
            }
            g.setColour(t.echo(t.value, copy));
            g.strokePath(path, juce::PathStrokeType(kTraceStroke, juce::PathStrokeType::curved));
        }
        for (int copy = copies() - 1; copy >= 0; --copy)
            dot(g,
                {plot.getX() + static_cast<float>(now) * plot.getWidth(), yFor(plot, value(copy, now), both)},
                copy);
    }

    /// Where a copy is now: a dot, as on a moved knob's ring (§7.4), each copy a step fainter.
    static void dot(juce::Graphics& g, juce::Point<float> at, int copy)
    {
        const auto& t = ui::Theme::current();
        const float size = 2.0f * (t.knobRingThickness + 0.5f);
        g.setColour(t.echo(t.textPrimary, copy));
        g.fillEllipse(juce::Rectangle<float>(size, size).withCentre(at));
    }

    void drawLfo(juce::Graphics& g, juce::Rectangle<float> plot) const
    {
        // One cycle; each copy starts its own way into it (Per Target spreads them evenly).
        const auto shape = static_cast<mod::Shape>(static_cast<int>(value(Lfo::Shape)));
        const double slant = value(Lfo::Slant) * 0.01, smooth = value(Lfo::Smooth) * 0.01;
        const bool both = bothWays();
        const int n = copies();
        baseline(g, plot, both);
        const double now = position_ - std::floor(position_);
        traces(g, plot, both, now,
               [&](int copy, double u) { return mod::lfo(shape, u + mod::spread(copy, n), slant, smooth); });
    }

    void drawRandom(juce::Graphics& g, juce::Rectangle<float> plot) const
    {
        // The last few steps, now at the right edge; each copy its own path (Per Target).
        const mod::Wander w{.smooth = value(Random::Smooth) * 0.01,
                            .slant = value(Random::Slant) * 0.01,
                            .cyclic = value(Random::Cyclic) * 0.01,
                            .jitter = value(Random::Jitter) * 0.01};
        const bool both = bothWays();
        baseline(g, plot, both);
        const auto roll = host_.roll();
        traces(g, plot, both, 1.0,
               [&](int copy, double u)
               {
                   return mod::random(roll, static_cast<std::uint64_t>(copy),
                                      position_ - kRandomSteps * (1.0 - u), w);
               });
    }

    void drawSteps(juce::Graphics& g, juce::Rectangle<float> plot) const
    {
        const auto& t = ui::Theme::current();
        const auto values = host_.stepValues();
        const auto chances = host_.stepChances();
        const int count = std::clamp(static_cast<int>(value(Steps::Length)), 1, Steps::maxSteps);
        const bool both = bothWays();
        // The chance row along the foot, the bars above it.
        const float gap = static_cast<float>(t.grid) * 0.5f;
        const auto chanceRow = plot.removeFromBottom(static_cast<float>(t.deviceGrid.displayInset));
        plot.removeFromBottom(gap);
        baseline(g, plot, both);
        const float slot = plot.getWidth() / static_cast<float>(count);
        const int current = static_cast<int>(std::floor(position_)) % count;
        const auto at = [&](const std::vector<float>& v, int i, float fallback)
        { return i < static_cast<int>(v.size()) ? v[static_cast<size_t>(i)] : fallback; };
        for (int i = 0; i < count; ++i)
        {
            const float x = plot.getX() + slot * static_cast<float>(i);
            const float v = at(values, i, 0.0f);
            const float from = yFor(plot, 0.0, both), to = yFor(plot, v, both);
            g.setColour(i == current ? t.value : t.faded(t.value));
            g.fillRect(
                juce::Rectangle<float>(x + gap * 0.5f, std::min(from, to), slot - gap, std::abs(to - from)));
            // The chance: a track, filled as far as the step's chance.
            const auto cell =
                juce::Rectangle<float>(x + gap * 0.5f, chanceRow.getY(), slot - gap, chanceRow.getHeight());
            g.setColour(t.surface3);
            g.fillRect(cell);
            g.setColour(t.textTertiary);
            g.fillRect(cell.withWidth(cell.getWidth() * std::clamp(at(chances, i, 1.0f), 0.0f, 1.0f)));
        }
        // Per Target: what each copy plays this time round, its chances rolled its own way.
        if (copies() > 1)
        {
            const auto roll = host_.roll();
            const auto pass = static_cast<std::int64_t>(std::floor(position_ / count)) * count;
            for (int copy = copies() - 1; copy >= 0; --copy)
            {
                juce::Path path;
                for (int i = 0; i < count; ++i)
                {
                    const bool plays = mod::stepPlays(roll, static_cast<std::uint64_t>(copy), pass + i,
                                                      at(chances, i, 1.0f));
                    const float y = yFor(plot, plays ? at(values, i, 0.0f) : 0.0, both);
                    const float x0 = plot.getX() + slot * static_cast<float>(i), x1 = x0 + slot;
                    i == 0 ? path.startNewSubPath(x0, y) : path.lineTo(x0, y);
                    path.lineTo(x1, y);
                }
                g.setColour(t.echo(t.textPrimary, copy));
                g.strokePath(path, juce::PathStrokeType(kTraceStroke));
            }
        }
    }

    void drawEnvelope(juce::Graphics& g, juce::Rectangle<float> plot) const
    {
        const auto& t = ui::Theme::current();
        // The stages up to the sustain, spaced by the log of their times; the sustain a quarter of
        // the rest, while a note is held; then the release.
        struct Stage
        {
            double ms, from, to, bend;
            float width = 0.0f;
        };
        const double sustain = value(Envelope::Sustain) * 0.01;
        Stage stages[] = {{value(Envelope::Delay), 0.0, 0.0, 0.0},
                          {value(Envelope::Attack), 0.0, 1.0, value(Envelope::AttackCurve) * 0.01},
                          {value(Envelope::Hold), 1.0, 1.0, 0.0},
                          {value(Envelope::Decay), 1.0, sustain, value(Envelope::DecayCurve) * 0.01}};
        Stage release{value(Envelope::Release), sustain, 0.0, value(Envelope::ReleaseCurve) * 0.01};
        float total = 0.0f;
        for (auto& stage : stages)
            total += stage.width = stage.ms > 0.0 || stage.to != stage.from ? stageWidth(stage.ms) : 0.0f;
        total += release.width = stageWidth(release.ms);
        const float held = total * 0.25f;
        // The points it's dragged by stay inside the well: half a point in from its edges.
        const auto half = static_cast<float>(t.deviceGrid.displayHandle) * 0.5f;
        plot = plot.withTrimmedLeft(half).withTrimmedRight(half).withTrimmedBottom(half).withTrimmedTop(half);
        const float scale = plot.getWidth() / (total + held);
        for (auto& stage : stages)
            stage.width *= scale;
        release.width *= scale;
        const auto yOf = [&](double level)
        { return plot.getBottom() - static_cast<float>(level) * plot.getHeight(); };
        const auto levelIn = [](const Stage& stage, double u)
        { return stage.from + (stage.to - stage.from) * fd::dsp::CurvedEnvelope::shape(u, stage.bend); };

        juce::Path curve;
        std::vector<juce::Point<float>> points; // the ends of attack, decay and release: dragged
        float x = plot.getX();
        curve.startNewSubPath(x, plot.getBottom());
        const auto draw = [&](const Stage& stage)
        {
            const int n = std::max(2, static_cast<int>(stage.width));
            for (int i = 1; i <= n; ++i)
            {
                const double u = static_cast<double>(i) / n;
                curve.lineTo(x + stage.width * static_cast<float>(u), yOf(levelIn(stage, u)));
            }
            x += stage.width;
        };
        for (size_t i = 0; i < std::size(stages); ++i)
        {
            draw(stages[i]);
            if (i == 1 || i == 3)
                points.emplace_back(x, yOf(stages[i].to));
        }
        draw({0.0, sustain, sustain, 0.0, held * scale});
        draw(release);
        points.emplace_back(x, plot.getBottom());

        auto fill = curve;
        fill.lineTo(plot.getRight(), plot.getBottom());
        fill.closeSubPath();
        g.setColour(t.fill(t.value));
        g.fillPath(fill);
        g.setColour(t.value);
        g.strokePath(curve, juce::PathStrokeType(kTraceStroke, juce::PathStrokeType::curved));
        // The points dragged to shape it, as any display handle (the Wavetable's envelope).
        const auto handle = static_cast<float>(t.deviceGrid.displayHandle);
        for (const auto& point : points)
        {
            const auto r = juce::Rectangle<float>(handle, handle).withCentre(point);
            g.setColour(t.surface3);
            g.fillEllipse(r);
            g.setColour(t.value);
            g.drawEllipse(r.reduced(0.5f), 1.0f);
        }
        // Each copy playing, where it is (Per Target: each fires on its own chance): through its
        // stages up to the sustain, where a held note waits.
        const auto playing = host_.playing();
        for (int copy = std::min(copies(), static_cast<int>(playing.size())) - 1; copy >= 0; --copy)
        {
            double ms = playing[static_cast<size_t>(copy)];
            if (ms < 0.0)
                continue;
            float at = plot.getX();
            double level = sustain;
            bool placed = false;
            for (const auto& stage : stages)
            {
                if (ms < stage.ms)
                {
                    const double u = ms / stage.ms;
                    at += stage.width * static_cast<float>(u);
                    level = levelIn(stage, u);
                    placed = true;
                    break;
                }
                ms -= stage.ms;
                at += stage.width;
            }
            if (!placed)
                at += held * scale * 0.5f; // held, in the sustain
            dot(g, {at, yOf(level)}, copy);
        }
    }

    void drawFollower(juce::Graphics& g, juce::Rectangle<float> plot) const
    {
        const auto& t = ui::Theme::current();
        // What it heard, the last few seconds, on a dB scale from -60 dB at the foot to 0 at the top.
        const auto heard = host_.heard();
        baseline(g, plot, false);
        if (heard.size() < 2)
            return;
        const auto yOf = [&](float level)
        {
            const double db = 20.0 * std::log10(std::max(1.0e-6, static_cast<double>(level)));
            return plot.getBottom() -
                   static_cast<float>(std::clamp((db + 60.0) / 60.0, 0.0, 1.0)) * plot.getHeight();
        };
        juce::Path line;
        for (size_t i = 0; i < heard.size(); ++i)
        {
            const juce::Point<float> p(plot.getX() + plot.getWidth() * static_cast<float>(i) /
                                                         static_cast<float>(heard.size() - 1),
                                       yOf(heard[i]));
            i == 0 ? line.startNewSubPath(p) : line.lineTo(p);
        }
        auto fill = line;
        fill.lineTo(plot.getRight(), plot.getBottom());
        fill.lineTo(plot.getX(), plot.getBottom());
        fill.closeSubPath();
        g.setColour(t.fill(t.value));
        g.fillPath(fill);
        g.setColour(t.value);
        g.strokePath(line, juce::PathStrokeType(kTraceStroke, juce::PathStrokeType::curved));
        dot(g, {plot.getRight(), yOf(heard.back())}, 0);
    }

    ui::DeviceUiContext& ctx_;
    ModulatorKind kind_;
    ModulatorHost& host_;
    ui::ModulationHandle* handle_ = nullptr;
    double position_ = 0.0;
    int routes_ = 0;
    std::uint64_t roll_ = 0;
};

// ---- the panel --------------------------------------------------------------------------------

ModulatorPanel::ModulatorPanel(ui::DeviceUiContext& context, ModulatorKind kind, ModulatorHost& host)
    : DevicePanel(context), kind_(kind), host_(host), self_(std::make_unique<Self>())
{
    const auto& t = ui::Theme::current();
    self_->name = juce::String(std::string(context.info.name));
    setHeaderIcon(ui::Icon::Modulator);

    // Its handle: in the display's corner, and at the folded strip's foot (the arrow alone).
    handle_ = std::make_unique<ui::ModulationHandle>(*self_, 0);
    foldedHandle_ = std::make_unique<ui::ModulationHandle>(*self_, 0);
    foldedHandle_->setCompact(true);
    foldedHandle_->setSize(foldedHandle_->preferredWidth(), t.deviceGrid.sourceHandle);
    setFoldedAccessory(foldedHandle_.get());
    for (auto* h : {handle_.get(), foldedHandle_.get()})
    {
        h->onDrag = [this](const juce::MouseEvent& e)
        {
            if (onHandleDrag)
                onHandleDrag(e);
        };
        h->onRelease = [this](const juce::MouseEvent& e, bool cancelled)
        {
            if (onHandleRelease)
                onHandleRelease(e, cancelled);
        };
        h->onCancel = [this]
        {
            if (onHandleCancel)
                onHandleCancel();
        };
        h->onPopupMenu = [this]
        {
            if (onRoutes)
                onRoutes();
        };
    }
    if (model::hasKeep(kind))
    {
        keep_ = std::make_unique<ui::Toggle>("Keep");
        keep_->setTooltip("Keeps the roll it played: it plays the same every time (Module > Keep as played)");
        keep_->onClick = [this]
        {
            host_.setKept(keep_->getToggleState());
            refreshFromHost();
        };
    }
    display_ = &own(std::make_unique<Display>(context, kind, host));
    display_->setHandle(*handle_);
    display_->setTitle(self_->name);

    auto& p = context.params;
    switch (kind)
    {
    case ModulatorKind::Lfo:
    {
        auto& s = addSection("shape", "Shape", 7);
        s.place(*display_, 0, 0, 7, 3);
        auto& rate = s.knob(Lfo::Rate, 0, 3);
        s.knob(Lfo::Phase, 1, 3);
        s.knob(Lfo::Slant, 2, 3);
        s.knob(Lfo::Smooth, 3, 3);
        auto& fade = s.knob(Lfo::Fade, 4, 3);
        s.combo(Lfo::Shape, 5, 3);
        s.combo(Lfo::Sync, 5, 4).setInlineName(true);
        s.combo(Lfo::Mode, 5, 5);
        dimWhen({&rate}, [&p] { return p.getPlain(Lfo::Sync) >= 0.5f; });
        dimWhen({&fade}, [&p] { return p.getPlain(Lfo::Mode) < 0.5f; }); // Free never restarts
        break;
    }
    case ModulatorKind::Random:
    {
        auto& s = addSection("movement", "Movement", 7);
        s.place(*display_, 0, 0, 7, 3);
        auto& rate = s.knob(Random::Rate, 0, 3);
        s.knob(Random::Smooth, 1, 3);
        s.knob(Random::Slant, 2, 3);
        s.knob(Random::Cyclic, 3, 3);
        s.knob(Random::Jitter, 4, 3);
        s.combo(Random::Sync, 5, 3).setInlineName(true);
        s.toggle(Random::Freeze, 5, 4, 2);
        s.place(*keep_, 5, 5, 2, 1, ui::PanelSection::Kind::Line);
        dimWhen({&rate}, [&p] { return p.getPlain(Random::Sync) >= 0.5f; });
        break;
    }
    case ModulatorKind::Envelope:
    {
        auto& s = addSection("shape", "Shape", 8);
        s.place(*display_, 0, 0, 8, 3);
        s.knob(Envelope::Attack, 0, 3);
        s.knob(Envelope::Hold, 1, 3);
        s.knob(Envelope::Decay, 2, 3);
        auto& sustain = s.knob(Envelope::Sustain, 3, 3);
        s.knob(Envelope::Release, 4, 3);
        s.bar(Envelope::AttackCurve, 5, 3, 3);
        s.bar(Envelope::DecayCurve, 5, 4, 3);
        s.bar(Envelope::ReleaseCurve, 5, 5, 3);
        auto& trigger = addSection("trigger", "Trigger", 4);
        trigger.combo(Envelope::Trigger, 0, 0);
        auto& every = trigger.combo(Envelope::Every, 2, 0);
        trigger.combo(Envelope::Mode, 0, 1, 3).setInlineName(true);
        trigger.toggle(Envelope::Fire, 3, 1);
        auto& legato = trigger.toggle(Envelope::Legato, 0, 2, 2);
        trigger.place(*keep_, 2, 2, 2, 1, ui::PanelSection::Kind::Line);
        trigger.knob(Envelope::Delay, 0, 3);
        trigger.knob(Envelope::Repeat, 1, 3);
        trigger.knob(Envelope::Chance, 2, 3);
        dimWhen({&every}, [&p] { return static_cast<int>(p.getPlain(Envelope::Trigger)) != 1; });
        dimWhen({&legato}, [&p] { return static_cast<int>(p.getPlain(Envelope::Trigger)) != 0; });
        dimWhen({&sustain}, [&p] { return static_cast<int>(p.getPlain(Envelope::Mode)) == 2; }); // One-shot
        break;
    }
    case ModulatorKind::Follower:
    {
        setSidechainCaption("Listen",
                            "What it follows: the sound where it sits in the chain (Here), or another "
                            "track's");
        auto& s = addSection("level", "Level", 3);
        s.place(*display_, 0, 0, 3, 3);
        s.knob(Follower::Gain, 0, 3);
        s.knob(Follower::Attack, 1, 3);
        s.knob(Follower::Release, 2, 3);
        auto& band = addSection("band", "Band", 2, Follower::Band);
        auto& curve =
            own(std::make_unique<ui::CutFilterCurve>(context, Follower::Band, Follower::Low, Follower::High));
        band.place(curve, 0, 0, 2, 3);
        band.holdsParam(Follower::Low);
        band.holdsParam(Follower::High);
        band.knob(Follower::Low, 0, 3);
        band.knob(Follower::High, 1, 3);
        break;
    }
    case ModulatorKind::Steps:
    {
        auto& s = addSection("pattern", "Pattern", 8);
        s.place(*display_, 0, 0, 8, 3);
        auto& rate = s.knob(Steps::Rate, 0, 3);
        s.knob(Steps::Length, 1, 3);
        s.knob(Steps::Smooth, 2, 3);
        s.knob(Steps::Jitter, 3, 3);
        s.combo(Steps::Sync, 4, 3, 3).setInlineName(true);
        s.combo(Steps::Advance, 4, 4, 3).setInlineName(true);
        s.combo(Steps::Restart, 4, 5, 3).setInlineName(true);
        s.place(*keep_, 7, 3, 1, 1, ui::PanelSection::Kind::Line);
        s.toggle(Steps::Fire, 7, 4);
        dimWhen({&rate}, [&p] { return p.getPlain(Steps::Sync) >= 0.5f; });
        break;
    }
    }

    // TARGETS: how far it moves everything it's routed to, and how (§10).
    auto& targets = addSection("targets", "Targets", 2);
    targets.knob(model::depthParam(kind), 0, 0, 2);
    int row = 3;
    if (const auto both = model::bothWaysParam(kind))
        targets.toggle(*both, 0, row++, 2);
    if (const auto perTarget = model::perTargetParam(kind))
        targets.toggle(*perTarget, 0, row, 2);
    refreshFromHost();
}

ModulatorPanel::~ModulatorPanel()
{
    setFoldedAccessory(nullptr);
}

ui::ModulationModel& ModulatorPanel::source() noexcept
{
    return *self_;
}
ui::ModulationHandle& ModulatorPanel::handle() noexcept
{
    return isFolded() ? *foldedHandle_ : *handle_;
}
juce::Component& ModulatorPanel::display() noexcept
{
    return *display_;
}

void ModulatorPanel::refreshFromHost()
{
    const int routes = host_.routeCount();
    const bool kept = host_.kept();
    handle_->setCount(routes);
    handle_->setLocked(kept);
    foldedHandle_->setLocked(kept);
    for (auto* h : {handle_.get(), foldedHandle_.get()})
    {
        h->setTooltip(routes == 0 ? "Drag onto any control on the track to move it with " + self_->name
                                  : self_->name + " moves " + juce::String(routes) +
                                        (routes == 1 ? " control" : " controls") +
                                        ": drag onto another to add it; right-click for its routes");
        h->refresh();
    }
    display_->fitHandle();
    if (keep_ != nullptr)
        keep_->setToggleState(kept, juce::dontSendNotification);
    display_->refresh();
}

// ---- the route list ---------------------------------------------------------------------------

RouteList::RouteList(juce::String modulator, std::vector<Route> routes)
    : modulator_(std::move(modulator)), routes_(std::move(routes))
{
    for (size_t i = 0; i < routes_.size(); ++i)
    {
        Row row;
        row.amount = std::make_unique<ui::Knob>();
        row.amount->setStyle(ui::Knob::Style::Bar);
        row.amount->setName("Amount");
        row.amount->setArcOrigin(0.5f);
        row.amount->setDefaultValue(0.5f + 0.5f / 4.0f); // 50 %
        row.amount->textFromValue = [](float v)
        { return juce::String(juce::roundToInt((v - 0.5f) * 400.0f)) + " %"; };
        row.amount->setValue(0.5f + routes_[i].amount / 4.0f, juce::dontSendNotification);
        row.amount->onValueChange = [this, i](float v)
        {
            if (onAmount)
                onAmount(static_cast<int>(i), (v - 0.5f) * 4.0f);
        };
        row.amount->setTooltip("How far it moves " + routes_[i].target +
                               ": -200 to +200 %; below zero the other way");
        row.show = std::make_unique<ui::IconButton>(ui::Icon::ChevronRight, "Show");
        row.show->setTooltip("Show " + routes_[i].target);
        row.show->onClick = [this, i]
        {
            if (onShow)
                onShow(static_cast<int>(i));
        };
        row.remove = std::make_unique<ui::IconButton>(ui::Icon::Close, "Remove");
        row.remove->setTooltip("Remove the route to " + routes_[i].target);
        row.remove->onClick = [this, i]
        {
            if (onRemove)
                onRemove(static_cast<int>(i));
        };
        addAndMakeVisible(*row.amount);
        addAndMakeVisible(*row.show);
        addAndMakeVisible(*row.remove);
        rows_.push_back(std::move(row));
    }
    setSize(preferredBounds().getWidth(), preferredBounds().getHeight());
}

juce::Rectangle<int> RouteList::preferredBounds() const
{
    const auto& s = ui::Theme::current().shell;
    const int rows = std::max(1, static_cast<int>(routes_.size()));
    return {0, 0, s.formPopoverWidth, 2 * s.groupGap + s.rowHeight + s.itemGap + rows * s.listRowHeight};
}

void RouteList::resized()
{
    const auto& t = ui::Theme::current();
    const auto& s = t.shell;
    auto area = getLocalBounds().reduced(s.groupGap);
    title_ = area.removeFromTop(s.rowHeight);
    area.removeFromTop(s.itemGap);
    for (auto& row : rows_)
    {
        auto r = area.removeFromTop(s.listRowHeight);
        row.remove->setBounds(r.removeFromRight(s.rowHeight).withSizeKeepingCentre(s.rowHeight, s.rowHeight));
        r.removeFromRight(s.itemGap);
        row.show->setBounds(r.removeFromRight(s.rowHeight).withSizeKeepingCentre(s.rowHeight, s.rowHeight));
        r.removeFromRight(s.groupGap);
        const int bar = t.deviceGrid.width(3);
        row.amount->setBounds(r.removeFromRight(bar).withSizeKeepingCentre(bar, t.deviceGrid.row));
        r.removeFromRight(s.groupGap);
        row.name = r;
    }
}

void RouteList::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    ui::paintPopover(g, getLocalBounds());
    ui::paintCapsLabel(g, "Routes" + dotted() + modulator_, title_.toFloat(),
                       juce::Justification::centredLeft);
    if (routes_.empty())
    {
        ui::paintEmptyState(g, getLocalBounds().withTrimmedTop(title_.getBottom()),
                            "No routes: drag its handle onto a control");
        return;
    }
    g.setFont(ui::font(ui::TextStyle::Body));
    g.setColour(t.textPrimary);
    for (size_t i = 0; i < rows_.size(); ++i)
        g.drawText(routes_[i].target, rows_[i].name, juce::Justification::centredLeft, true);
}
} // namespace fd::app
