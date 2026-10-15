#include "TunerPanel.h"

#include "Tuner.h"
#include "fd/music/Key.h"

#include <cmath>
#include <optional>

namespace fd::devices
{
namespace
{
using T = Tuner;

float slot(const ui::DeviceUiContext& ctx, int s)
{
    return ctx.analysis != nullptr ? ctx.analysis->read(s) : NAN;
}

/// The song's key the Tuner measures against, as the catalogue names it: the first scale with
/// those notes from that root (for spelling, key-and-scale.md §4). None without one.
std::optional<music::Key> keyOf(const ui::DeviceUiContext& ctx)
{
    const float root = slot(ctx, T::KeyRoot), notes = slot(ctx, T::KeyNotes);
    if (!std::isfinite(root) || root < 0.0f || !std::isfinite(notes) || notes <= 0.0f)
        return std::nullopt;
    const auto set = static_cast<music::PitchSet>(notes);
    for (const auto& scale : music::scales())
    {
        const music::Key key{static_cast<int>(root), std::string(scale.id)};
        if (music::notes(key) == set)
            return key;
    }
    return std::nullopt;
}

/// "A♯" and "3" (C3 = 60), spelled in `key`.
std::pair<juce::String, juce::String> nameOf(int pitch, const std::optional<music::Key>& key)
{
    const auto pc = ((pitch % 12) + 12) % 12;
    return {juce::String::fromUTF8(music::pitchClassName(key ? &*key : nullptr, pc).c_str()),
            juce::String(static_cast<int>(std::floor(pitch / 12.0)) - 2)};
}

/// The note, large; under it the needle's scale or the strobe; the cents and the frequency.
class TunerReading final : public juce::Component, public ui::ParamControl
{
public:
    explicit TunerReading(ui::DeviceUiContext& ctx) : ctx_(ctx) {}

    void refresh() override
    {
        State s;
        s.note = slot(ctx_, T::Note);
        s.cents = slot(ctx_, T::Cents);
        s.hz = slot(ctx_, T::Frequency);
        s.since = slot(ctx_, T::Since);
        s.strobe = slot(ctx_, T::StrobePhase);
        s.strobeView = ctx_.params.getPlain(T::View) >= 0.5f;
        s.inKey = ctx_.params.getPlain(T::Notes) >= 0.5f;
        s.keyRoot = slot(ctx_, T::KeyRoot);
        s.keyNotes = slot(ctx_, T::KeyNotes);
        if (std::isfinite(s.note))
            last_ = s; // the last note, dimmed for a second after the sound stops
        if (!(s == state_))
        {
            state_ = s;
            repaint();
        }
    }

    void paint(juce::Graphics& g) override
    {
        const auto& t = ui::Theme::current();
        const auto& grid = t.deviceGrid;
        ui::paintDisplayWell(g, getLocalBounds().toFloat());
        auto area = getLocalBounds().toFloat().reduced(static_cast<float>(grid.displayInset));
        const float text = static_cast<float>(grid.displayTextHeight);
        const auto top = area.removeFromTop(text);
        area.removeFromTop(static_cast<float>(grid.displayInset));
        const auto readouts = area.removeFromBottom(text);
        area.removeFromBottom(static_cast<float>(grid.displayInset));

        // What it measures against, while In key.
        const auto key = keyOf(ctx_);
        g.setFont(ui::font(ui::TextStyle::ValueSmall));
        g.setColour(t.textTertiary);
        if (state_.inKey)
            g.drawText(key ? juce::String::fromUTF8(("In " + music::keyName(*key)).c_str())
                           : juce::String("No key: every note"),
                       top, juce::Justification::centredLeft, true);

        // The reading now, or the last one (dimmed) for a second, or none.
        const bool now = std::isfinite(state_.note);
        const bool recent = !now && std::isfinite(last_.note) && state_.since < 1.0f;
        const State& shown = now ? state_ : last_;
        const bool any = now || recent;
        const float cents = any ? shown.cents : 0.0f;
        const bool inTune = any && std::abs(cents) <= T::inTuneCents;
        // Now, or dimmed: the last note for a second after the sound stops.
        const auto shade = [&t, now](juce::Colour c) { return now ? c : t.dimmed(c); };
        const auto ink = shade(inTune ? t.roles.tunerInTune : t.textPrimary);

        // The note, large, its octave beside it.
        const float noteHeight = std::ceil(ui::font(ui::TextStyle::Display).getHeight());
        auto noteArea = area.removeFromTop(noteHeight);
        area.removeFromTop(static_cast<float>(grid.displayInset));
        if (any)
        {
            const auto [name, octave] = nameOf(static_cast<int>(std::lround(shown.note)), key);
            const auto big = ui::font(ui::TextStyle::Display), small = ui::font(ui::TextStyle::ValueLarge);
            const float nameW = juce::GlyphArrangement::getStringWidth(big, name);
            const float octaveW = juce::GlyphArrangement::getStringWidth(small, octave);
            const float gap = static_cast<float>(grid.displayInset) * 0.5f;
            auto line = noteArea.withSizeKeepingCentre(nameW + gap + octaveW, noteArea.getHeight());
            g.setFont(big);
            g.setColour(ink);
            g.drawText(name, line.removeFromLeft(nameW), juce::Justification::centred, false);
            line.removeFromLeft(gap);
            g.setFont(small);
            g.setColour(shade(t.textSecondary));
            g.drawText(octave, line.withTrimmedBottom(noteArea.getHeight() * 0.12f),
                       juce::Justification::bottomLeft, false);
        }
        else
        {
            g.setFont(ui::font(ui::TextStyle::Display));
            g.setColour(t.textTertiary);
            g.drawText(ui::Readouts::none(), noteArea, juce::Justification::centred, false);
        }

        // Which way to tune: an arrow on that side, level with the band, none in tune.
        const float arrow = text * 0.6f;
        auto scale = area;
        const auto leftArrow = scale.removeFromLeft(arrow);
        const auto rightArrow = scale.removeFromRight(arrow);
        scale = scale.reduced(static_cast<float>(grid.displayInset), 0.0f);
        const float bandY = state_.strobeView ? scale.getCentreY()
                                              : scale.withTrimmedBottom(text).getCentreY(); // over its labels
        if (any && !inTune)
        {
            const auto side = cents > 0.0f ? leftArrow : rightArrow;
            const auto box = juce::Rectangle<float>(arrow, arrow).withCentre({side.getCentreX(), bandY});
            juce::Path p;
            if (cents > 0.0f) // sharp: tune down, to the left
                p.addTriangle(box.getRight(), box.getY(), box.getRight(), box.getBottom(), box.getX(),
                              box.getCentreY());
            else
                p.addTriangle(box.getX(), box.getY(), box.getX(), box.getBottom(), box.getRight(),
                              box.getCentreY());
            g.setColour(shade(t.value));
            g.fillPath(p);
        }

        if (state_.strobeView)
            paintStrobe(g, scale, any, shade(inTune ? t.roles.tunerInTune : t.value));
        else
            paintNeedle(g, scale, any, cents, shade(inTune ? t.roles.tunerInTune : t.value));

        // The cents and the frequency.
        g.setFont(ui::font(ui::TextStyle::Value));
        g.setColour(shade(t.textSecondary));
        if (any)
        {
            const auto c = juce::String(std::abs(cents) < 0.05f ? 0.0f : cents, 1);
            g.drawText((cents > 0.05f ? "+" : "") + c + juce::String::fromUTF8(" \xC2\xA2"), readouts,
                       juce::Justification::centredLeft, false);
            g.drawText(juce::String(shown.hz, shown.hz < 1000.0f ? 2 : 1) + " Hz", readouts,
                       juce::Justification::centredRight, false);
        }
    }

private:
    struct State
    {
        float note = NAN, cents = NAN, hz = NAN, since = 0.0f, strobe = 0.0f, keyRoot = -1.0f,
              keyNotes = 0.0f;
        bool strobeView = false, inKey = false;
        bool operator==(const State& o) const
        {
            const auto same = [](float a, float b) { return a == b || (std::isnan(a) && std::isnan(b)); };
            return same(note, o.note) && same(cents, o.cents) && same(hz, o.hz) && same(since, o.since) &&
                   same(strobe, o.strobe) && keyRoot == o.keyRoot && keyNotes == o.keyNotes &&
                   strobeView == o.strobeView && inKey == o.inKey;
        }
    };

    /// The scale: ±50 cents (wider with In key, as far as the note it's off by), ticks, the
    /// in-tune band, and the needle from the middle to the reading.
    void paintNeedle(juce::Graphics& g, juce::Rectangle<float> area, bool any, float cents,
                     juce::Colour colour) const
    {
        const auto& t = ui::Theme::current();
        const auto& grid = t.deviceGrid;
        const float range =
            state_.inKey ? std::max(50.0f, std::ceil(std::abs(cents) / 50.0f) * 50.0f) : 50.0f;
        const float step = range > 50.0f ? 25.0f : 10.0f;
        const auto labels = area.removeFromBottom(static_cast<float>(grid.displayTextHeight));
        const auto band = area.withSizeKeepingCentre(
            area.getWidth(), std::min(area.getHeight(), static_cast<float>(grid.row)));
        const auto xOf = [&](float c)
        { return band.getCentreX() + std::clamp(c / range, -1.0f, 1.0f) * band.getWidth() * 0.5f; };

        g.setColour(t.roles.tunerInTuneBand);
        g.fillRect(juce::Rectangle<float>(xOf(-T::inTuneCents), band.getY(),
                                          xOf(T::inTuneCents) - xOf(-T::inTuneCents), band.getHeight()));
        g.setColour(t.surface3);
        g.fillRect(band.withSizeKeepingCentre(band.getWidth(), 1.0f));
        g.setFont(ui::font(ui::TextStyle::ValueSmall));
        for (float c = -range; c <= range + 0.01f; c += step)
        {
            const float x = xOf(c);
            const bool middle = std::abs(c) < 0.01f;
            const float h = middle ? band.getHeight() : band.getHeight() * 0.4f;
            g.setColour(middle ? t.textSecondary : t.textTertiary);
            g.fillRect(juce::Rectangle<float>(x - 0.5f, band.getCentreY() - h * 0.5f, 1.0f, h));
            if (std::fmod(std::abs(c), step * (range > 50.0f ? 2.0f : 2.5f)) < 0.01f || middle)
            {
                const auto label = (c > 0.0f ? "+" : "") + juce::String(static_cast<int>(c));
                const float w = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), label);
                const float left = std::clamp(x - w * 0.5f, labels.getX(), labels.getRight() - w);
                g.setColour(t.textTertiary);
                g.drawText(label, juce::Rectangle<float>(left, labels.getY(), w, labels.getHeight()),
                           juce::Justification::centred, false);
            }
        }
        if (!any)
            return;
        // The needle: a bar from the middle as thick as a modulation ring's line twice, its end
        // a mark across the band.
        const float x = xOf(cents), mid = band.getCentreX(), thick = 2.0f * t.knobRingThickness;
        g.setColour(colour);
        g.fillRect(juce::Rectangle<float>(std::min(x, mid), band.getCentreY() - thick * 0.5f,
                                          std::abs(x - mid), thick));
        g.fillRect(juce::Rectangle<float>(x - thick * 0.5f, band.getY(), thick, band.getHeight()));
    }

    /// Stripes that drift right when sharp and left when flat, as fast as the reading says (the
    /// device turns them: StrobePhase), still in tune.
    void paintStrobe(juce::Graphics& g, juce::Rectangle<float> area, bool any, juce::Colour colour) const
    {
        const auto& t = ui::Theme::current();
        const auto& grid = t.deviceGrid;
        const auto band = area.withSizeKeepingCentre(
            area.getWidth(), std::min(area.getHeight(), static_cast<float>(grid.row)));
        g.setColour(t.surface1);
        g.fillRoundedRectangle(band, t.radiusControl);
        if (!any)
            return;
        juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion(band.toNearestInt());
        const float period = static_cast<float>(grid.column) / 3.0f; // a stripe and its gap
        const float offset = (std::isfinite(state_.strobe) ? state_.strobe : 0.0f) * period;
        g.setColour(colour);
        for (float x = band.getX() - period + offset; x < band.getRight(); x += period)
            g.fillRect(juce::Rectangle<float>(x, band.getY(), period * 0.5f, band.getHeight()));
    }

    ui::DeviceUiContext& ctx_;
    State state_, last_;
};

/// The pitch over the last 12 seconds on rows of notes, as the piano roll's, the key's lighter;
/// each row's middle is in tune. It follows the pitch up and down.
class TunerHistory final : public juce::Component, public ui::ParamControl, public juce::TooltipClient
{
public:
    static constexpr double seconds = 12.0;
    static constexpr int rowsEachWay = 7;

    explicit TunerHistory(ui::DeviceUiContext& ctx) : ctx_(ctx) {}

    void refresh() override
    {
        const auto* tuner = dynamic_cast<const T*>(ctx_.device);
        const auto written = tuner != nullptr ? tuner->historyWritten() : 0u;
        if (written != written_)
        {
            written_ = written;
            // Follow the pitch: centred again when it strays five semitones from the middle.
            const float now = tuner->historyAt(0);
            if (std::isfinite(now) && (!centre_ || std::abs(now - static_cast<float>(*centre_)) > 5.0f))
                centre_ = static_cast<int>(std::lround(now));
            repaint();
        }
    }

    void paint(juce::Graphics& g) override
    {
        const auto& t = ui::Theme::current();
        ui::paintDisplayWell(g, getLocalBounds().toFloat());
        const auto plot = plotArea();
        const int centre = centre_.value_or(57);
        const auto key = keyOf(ctx_);
        const float rowH = plot.getHeight() / (2 * rowsEachWay + 1);
        const auto yOf = [&](double pitch)
        { return plot.getCentreY() - static_cast<float>((pitch - centre) * rowH); };
        {
            juce::Graphics::ScopedSaveState saved(g);
            g.reduceClipRegion(plot.toNearestInt());
            g.setFont(ui::font(ui::TextStyle::Micro));
            for (int p = centre - rowsEachWay - 1; p <= centre + rowsEachWay + 1; ++p)
            {
                const float y = yOf(p);
                const auto row = juce::Rectangle<float>(plot.getX(), y - rowH * 0.5f, plot.getWidth(), rowH);
                const bool light =
                    key ? music::contains(*key, p) : ((0xAB5 >> (((p % 12) + 12) % 12)) & 1) != 0;
                g.setColour(light ? t.roles.keyRowLight : t.roles.keyRowDark);
                g.fillRect(row);
                g.setColour(t.roles.gridSub);
                g.fillRect(row.withSizeKeepingCentre(row.getWidth(), 1.0f));
                if (((p % 12) + 12) % 12 == 0 || p == centre)
                {
                    const auto [name, octave] = nameOf(p, key);
                    g.setColour(t.textTertiary);
                    g.drawText(name + octave, row.withTrimmedLeft(2.0f), juce::Justification::centredLeft,
                               false);
                }
            }
        }
        const auto* tuner = dynamic_cast<const T*>(ctx_.device);
        if (tuner == nullptr)
            return;
        const double each = tuner->readingSeconds();
        const auto count = static_cast<std::uint32_t>(seconds / each);
        juce::Path line;
        bool drawing = false;
        for (std::uint32_t ago = count; ago-- > 0;)
        {
            const float p = tuner->historyAt(ago);
            if (!std::isfinite(p))
            {
                drawing = false;
                continue;
            }
            const float x =
                plot.getRight() - static_cast<float>(ago) / static_cast<float>(count) * plot.getWidth();
            const juce::Point<float> at(x, yOf(p));
            drawing ? line.lineTo(at) : line.startNewSubPath(at);
            drawing = true;
        }
        juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion(plot.toNearestInt());
        g.setColour(t.value);
        g.strokePath(line, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved));
    }

    juce::String getTooltip() override
    {
        const auto* tuner = dynamic_cast<const T*>(ctx_.device);
        const auto plot = plotArea();
        const auto mouse = getMouseXYRelative().toFloat();
        if (tuner == nullptr || !plot.contains(mouse))
            return {};
        const double ago = (plot.getRight() - mouse.x) / plot.getWidth() * seconds;
        const float p = tuner->historyAt(static_cast<std::uint32_t>(ago / tuner->readingSeconds()));
        if (!std::isfinite(p))
            return "Nothing heard " + juce::String(ago, 1) + " s ago";
        const int note = static_cast<int>(std::lround(p));
        const auto [name, octave] = nameOf(note, keyOf(ctx_));
        const float cents = (p - static_cast<float>(note)) * 100.0f;
        return name + octave + " " + (cents >= 0.0f ? "+" : "") + juce::String(cents, 1) +
               juce::String::fromUTF8(" \xC2\xA2, ") + juce::String(ago, 1) + " s ago";
    }

private:
    [[nodiscard]] juce::Rectangle<float> plotArea() const
    {
        return getLocalBounds().toFloat().reduced(
            static_cast<float>(ui::Theme::current().deviceGrid.displayInset));
    }

    ui::DeviceUiContext& ctx_;
    std::uint32_t written_ = 0;
    std::optional<int> centre_;
};

/// Sections per devices/tuner/SPEC.md, on the module grid (docs/design/device-panels.md).
class TunerPanel final : public ui::DevicePanel
{
public:
    explicit TunerPanel(ui::DeviceUiContext& ctx) : DevicePanel(ctx)
    {
        auto& tuner = addSection("tuner", "Tuner", 7);
        tuner.place(display(std::make_unique<TunerReading>(ctx)), 0, 0, 7, 6);

        auto& history = addSection("history", "History", 8);
        history.place(display(std::make_unique<TunerHistory>(ctx)), 0, 0, 8, 6);

        auto& settings = addSection("settings", "Settings", 3);
        settings.bar(T::Reference, 0, 0, 3);
        settings.segmented(T::View, 0, 1, 3, 1);
        settings.segmented(T::Notes, 0, 2, 3, 1);
        settings.toggle(T::Mute, 0, 3, 3);

        // Muted: said in the header, so it isn't forgotten with the panel folded (TU6).
        showBadge([&ctx]
                  { return ctx.params.getPlain(T::Mute) >= 0.5f ? juce::String("Muted") : juce::String(); },
                  [] { return juce::String("The Tuner silences this track while Mute is on"); }, "Muted");
        for (auto* d : displays_) // drawn from the start, before the first frame
            d->refresh();
    }

private:
    /// Owns a display that follows the device, so the frame clock can refresh it.
    template <typename D> D& display(std::unique_ptr<D> component)
    {
        auto& d = own(std::move(component));
        displays_.push_back(&d);
        return d;
    }

    /// Every frame on screen (the panel's 30 Hz refresh covers hosts without a frame clock).
    void tick()
    {
        if (isShowing())
            for (auto* d : displays_)
                d->refresh();
    }

    std::vector<ui::ParamControl*> displays_;
    juce::VBlankAttachment vblank_{this, [this] { tick(); }};
};
} // namespace

const ui::PanelFactory& tunerPanelFactory()
{
    static const ui::PanelFactory factory{
        .deviceId = "tuner",
        .create = [](ui::DeviceUiContext& ctx) -> std::unique_ptr<ui::DevicePanel>
        { return std::make_unique<TunerPanel>(ctx); },
    };
    return factory;
}
} // namespace fd::devices
