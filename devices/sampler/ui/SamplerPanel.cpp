#include "SamplerPanel.h"

#include "Sampler.h"
#include "fd/device/Library.h"

#include <nlohmann/json.hpp>

namespace fd::devices
{
namespace
{
using S = Sampler;
using Zone = SampleMap::Zone;

bool isLoadable(const juce::String& path)
{
    return juce::File(path).hasFileExtension("wav;wave;w64;aif;aiff;aifc;flac;mp3;sfz");
}

juce::String fileName(const std::string& path)
{
    return juce::File(juce::String::fromUTF8(path.c_str())).getFileName();
}

/// Loads a sample or an SFZ file through the host (ADR 0017), then what the file sets on the
/// Sampler's controls as an undo step of its own (SPEC, S6).
void loadFile(ui::DeviceUiContext& ctx, const Sampler* sampler, const juce::String& path)
{
    const juce::File file(path);
    ctx.params.beginEdit("Load sample " + file.getFileNameWithoutExtension());
    ctx.params.applyExtraState({{"sample.path", library::reference(path.toStdString())}}); // ADR 0055
    const auto map = sampler != nullptr ? sampler->map() : nullptr;
    if (map == nullptr)
        return;
    const auto settings = S::settingsFor(*map);
    if (settings.empty())
        return;
    ctx.params.beginEdit("Settings from " + file.getFileNameWithoutExtension());
    for (const auto& [param, value] : settings)
    {
        ctx.params.beginGesture(param);
        ctx.params.setPlain(param, value);
        ctx.params.endGesture(param);
    }
}

void browseFor(std::unique_ptr<juce::FileChooser>& chooser, juce::Component& owner,
               std::function<void(const juce::String&)> chosen)
{
    chooser = std::make_unique<juce::FileChooser>("Load a sample", juce::File(),
                                                  "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.sfz");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [safe = juce::Component::SafePointer<juce::Component>(&owner),
                          done = std::move(chosen)](const juce::FileChooser& fc)
                         {
                             if (safe != nullptr && fc.getResult().existsAsFile())
                                 done(fc.getResult().getFullPathName());
                         });
}

/// The sample one zone plays (a single sample's, or the last zone a multisample played) with the
/// playback region, loop and live playheads. Drag the markers to set Start, End and Loop Start
/// (every zone's, in proportion); drop a file (or click when empty) to load one.
class WaveformView final : public juce::Component, public ui::ParamControl, public juce::FileDragAndDropTarget
{
public:
    explicit WaveformView(ui::DeviceUiContext& ctx) : ctx_(ctx), sampler_(dynamic_cast<const Sampler*>(ctx.device))
    {
        setName("Sample");
        refresh();
    }

    /// Called when what's loaded changes between one sample and a multisample.
    std::function<void()> onMapChanged;

    void refresh() override
    {
        const auto map = sampler_ != nullptr ? sampler_->map() : nullptr;
        bool dirty = map != map_;
        if (map != map_ && onMapChanged && (map == nullptr || map_ == nullptr || map->single != map_->single))
        {
            map_ = map;
            onMapChanged();
        }
        map_ = map;
        // A multisample shows the zone the last note started (the first that has a sample, before any).
        int zone = -1;
        if (map_ != nullptr && !map_->zones.empty())
        {
            const int last =
                ctx_.analysis != nullptr ? juce::roundToInt(ctx_.analysis->read(S::LastZone)) : -1;
            zone = juce::isPositiveAndBelow(last, static_cast<int>(map_->zones.size())) ? last
                                                                                        : firstWithSample();
        }
        dirty |= zone != zone_;
        zone_ = zone;
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
                const float z = ctx_.analysis->read(S::FirstPlayheadZone + i);
                const float shown = juce::roundToInt(z) == zone_ ? p : -1.0f;
                dirty |= !(shown == playheads_[static_cast<size_t>(i)]);
                playheads_[static_cast<size_t>(i)] = shown;
            }
        if (dirty)
            repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto& t = ui::Theme::current();
        const auto r = getLocalBounds().toFloat();
        g.setColour(dragOver_ ? t.roles.dropWell : t.surface0);
        g.fillRoundedRectangle(r, t.radiusControl);
        g.setFont(ui::font(ui::TextStyle::Label));

        const auto* z = zone();
        if (z == nullptr || z->sample == nullptr)
        {
            const auto error = sampler_ != nullptr ? sampler_->loadError() : std::string{};
            const auto text = !error.empty() ? juce::String::fromUTF8(error.c_str())
                              : z != nullptr ? fileName(z->file) + " is missing"
                                             : juce::String("Drop a sample, or click to browse");
            g.setColour(error.empty() && z == nullptr ? t.textSecondary : t.meterClip);
            g.drawFittedText(text, getLocalBounds().reduced(12, 4), juce::Justification::centred, 3);
            return;
        }

        const auto& sample = *z->sample;
        const auto wave = waveArea();
        const auto region = S::regionFor(store(), *z);

        // Waveform: flat min/max columns; dimmer outside the playback region.
        const auto& lo = sample.overviewMin;
        const auto& hi = sample.overviewMax;
        const float mid = wave.getCentreY(), half = wave.getHeight() * 0.5f;
        for (int x = 0; x < static_cast<int>(wave.getWidth()); ++x)
        {
            const double f0 = static_cast<double>(x) / wave.getWidth() * sample.frames;
            const double f1 = static_cast<double>(x + 1) / wave.getWidth() * sample.frames;
            const auto b0 = static_cast<size_t>(f0 / SampleData::overviewBlock);
            const auto b1 = std::max(b0 + 1, static_cast<size_t>(f1 / SampleData::overviewBlock));
            float mn = 0.0f, mx = 0.0f;
            for (size_t b = b0; b < std::min(b1, lo.size()); ++b)
            {
                mn = std::min(mn, lo[b]);
                mx = std::max(mx, hi[b]);
            }
            const bool inside = f1 > region.start && f0 < region.end;
            g.setColour(inside ? t.value : t.roles.outside);
            const float px = wave.getX() + static_cast<float>(x);
            g.fillRect(px, mid - mx * half, 1.0f, std::max(1.0f, (mx - mn) * half));
        }

        // Loop region tint, markers and playheads.
        if (loops())
        {
            g.setColour(t.roles.loopTint);
            g.fillRect(juce::Rectangle<float>(xFor(region.loopStart), wave.getY(),
                                              xFor(region.loopEnd) - xFor(region.loopStart),
                                              wave.getHeight()));
        }
        drawMarker(g, S::Start, "S", t.textPrimary);
        drawMarker(g, S::End, "E", t.textPrimary);
        if (loops() && !z->loops)
            drawMarker(g, S::LoopStart, "L", t.active);
        g.setColour(t.roles.marker);
        for (float p : playheads_)
            if (p >= 0.0f)
                g.fillRect(xFor(static_cast<double>(p) * sample.frames), wave.getY(), 1.0f, wave.getHeight());

        // File name and length.
        g.setColour(t.textSecondary);
        g.drawText(fileName(sample.path) + juce::String::fromUTF8("  \xC2\xB7  ") +
                       juce::String(sample.seconds(), 2) + " s",
                   textLine(), juce::Justification::centredLeft, true);
    }

    // ---- markers ------------------------------------------------------------------------

    void mouseMove(const juce::MouseEvent& e) override
    {
        setMouseCursor(markerAt(e.position.x) ? juce::MouseCursor::LeftRightResizeCursor
                                              : juce::MouseCursor::NormalCursor);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (map_ == nullptr)
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
        const auto* z = zone();
        if (!dragging_ || z == nullptr || z->sample == nullptr)
            return;
        const auto wave = waveArea();
        const double frame =
            juce::jlimit(0.0, 1.0, static_cast<double>((e.position.x - wave.getX()) / wave.getWidth())) *
            z->frames();
        // Start and End within what the zone plays; Loop Start within Start..End.
        const double from = z->start, to = std::max(from + 1.0, static_cast<double>(z->playEnd()));
        float value = static_cast<float>(juce::jlimit(0.0, 100.0, (frame - from) / (to - from) * 100.0));
        if (*dragging_ == S::LoopStart)
        {
            const auto region = S::regionFor(store(), *z);
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
        return files.size() == 1 && isLoadable(files[0]);
    }
    void fileDragEnter(const juce::StringArray&, int, int) override { setDragOver(true); }
    void fileDragExit(const juce::StringArray&) override { setDragOver(false); }
    void filesDropped(const juce::StringArray& files, int, int) override
    {
        setDragOver(false);
        load(files[0]);
    }

    void load(const juce::String& path)
    {
        loadFile(ctx_, sampler_, path);
        refresh();
    }

    void browse()
    {
        browseFor(chooser_, *this, [this](const juce::String& path) { load(path); });
    }

private:
    [[nodiscard]] const Zone* zone() const
    {
        return map_ != nullptr && juce::isPositiveAndBelow(zone_, static_cast<int>(map_->zones.size()))
                   ? &map_->zones[static_cast<size_t>(zone_)]
                   : nullptr;
    }

    [[nodiscard]] int firstWithSample() const
    {
        for (size_t i = 0; i < map_->zones.size(); ++i)
            if (map_->zones[i].sample != nullptr)
                return static_cast<int>(i);
        return 0;
    }

    [[nodiscard]] bool loops() const
    {
        return juce::roundToInt(ctx_.params.getPlain(S::Mode)) == static_cast<int>(S::PlayMode::Classic) &&
               ctx_.params.getPlain(S::Loop) > 0.5f;
    }

    [[nodiscard]] juce::Rectangle<int> textLine() const
    {
        const auto& m = ui::Theme::current().deviceGrid;
        return getLocalBounds()
            .reduced(m.displayInset, m.displayInset / 2)
            .removeFromTop(m.displayTextHeight);
    }

    [[nodiscard]] juce::Rectangle<float> waveArea() const
    {
        const auto& m = ui::Theme::current().deviceGrid;
        return getLocalBounds()
            .toFloat()
            .reduced(static_cast<float>(m.displayInset), static_cast<float>(m.displayInset) * 0.5f)
            .withTrimmedTop(static_cast<float>(m.displayTextHeight));
    }

    [[nodiscard]] float xFor(double frame) const
    {
        const auto wave = waveArea();
        const auto* z = zone();
        return wave.getX() + static_cast<float>(frame / std::max(1, z->frames())) * wave.getWidth();
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
        const auto region = S::regionFor(store(), *zone());
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
        const auto* z = zone();
        if (z == nullptr || z->sample == nullptr)
            return std::nullopt;
        std::optional<size_t> best;
        float bestDistance = 6.0f;
        for (size_t p : {S::LoopStart, S::Start, S::End})
        {
            if (p == S::LoopStart && (!loops() || z->loops))
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

    ui::DeviceUiContext& ctx_;
    const Sampler* sampler_;
    std::shared_ptr<const SampleMap> map_;
    int zone_ = -1;
    ParamStore mirror_{ctx_.info.params};
    const std::array<size_t, 5> watched_{S::Mode, S::Start, S::End, S::Loop, S::LoopStart};
    std::array<float, 5> last_{};
    std::array<float, S::numPlayheads> playheads_{};
    std::optional<size_t> dragging_;
    std::unique_ptr<juce::FileChooser> chooser_;
    bool dragOver_ = false;
};

/// A multisample's zones (SPEC, S9): keys across, velocity up, a rectangle a zone; zones that
/// take turns or share a random choice drawn as one with their count. Zones light while they
/// play; hovering names one, clicking plays its root.
class ZoneMap final : public juce::Component,
                      public ui::ParamControl,
                      public juce::FileDragAndDropTarget,
                      public juce::TooltipClient
{
public:
    explicit ZoneMap(ui::DeviceUiContext& ctx) : ctx_(ctx), sampler_(dynamic_cast<const Sampler*>(ctx.device))
    {
        setName("Zones");
        refresh();
    }

    std::function<void(const juce::String&)> onLoad;

    void refresh() override
    {
        const auto map = sampler_ != nullptr ? sampler_->map() : nullptr;
        bool dirty = map != map_;
        if (map != map_)
        {
            map_ = map;
            group();
        }
        std::array<int, S::numPlayheads> playing{};
        playing.fill(-1);
        if (ctx_.analysis != nullptr)
            for (int i = 0; i < S::numPlayheads; ++i)
                if (ctx_.analysis->read(S::FirstPlayhead + i) >= 0.0f)
                    playing[static_cast<size_t>(i)] =
                        juce::roundToInt(ctx_.analysis->read(S::FirstPlayheadZone + i));
        dirty |= playing != playing_;
        playing_ = playing;
        if (dirty)
            repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto& t = ui::Theme::current();
        g.setColour(dragOver_ ? t.roles.dropWell : t.surface0);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), t.radiusControl);
        if (map_ == nullptr || map_->single)
            return;

        // The file, its zones, and what didn't make it (the line's tooltip names them).
        auto line = textLine();
        const auto font = ui::font(ui::TextStyle::Label);
        g.setFont(font);
        const auto dot = juce::String::fromUTF8("  \xC2\xB7  ");
        const auto name =
            juce::File(juce::String::fromUTF8(map_->path.c_str())).getFileNameWithoutExtension() + dot +
            juce::String(map_->zones.size()) + (map_->zones.size() == 1 ? " zone" : " zones");
        g.setColour(t.textSecondary);
        g.drawText(name, line, juce::Justification::centredLeft, true);
        if (const auto summary = problems(); summary.isNotEmpty())
        {
            line.removeFromLeft(juce::GlyphArrangement::getStringWidthInt(font, name + dot));
            g.setColour(t.roles.warning);
            g.drawText(summary, line, juce::Justification::centredLeft, true);
        }

        // Octave lines and their names.
        const auto area = zoneArea();
        g.setFont(ui::font(ui::TextStyle::Caption));
        for (int key = low_; key <= high_; ++key)
            if (key % 12 == 0)
            {
                const float x = xFor(key);
                g.setColour(t.roles.gridOctave);
                g.fillRect(x, area.getY(), 1.0f, area.getHeight());
                g.setColour(t.textTertiary);
                g.drawText(juce::String(ctx_.info.params[S::Root].toText(static_cast<float>(key))),
                           juce::Rectangle<float>(x + 2.0f, area.getBottom(), 40.0f, keyLabelHeight()),
                           juce::Justification::centredLeft, false);
            }

        // The zones: dimmed where the sample is missing, lit where they play, outlined when hovered.
        for (size_t i = 0; i < groups_.size(); ++i)
        {
            const auto& grp = groups_[i];
            const auto r = rectFor(grp).reduced(0.5f);
            const bool missing =
                std::all_of(grp.zones.begin(), grp.zones.end(),
                            [this](int z) { return map_->zones[static_cast<size_t>(z)].sample == nullptr; });
            const bool lit =
                std::any_of(grp.zones.begin(), grp.zones.end(), [this](int z)
                            { return std::find(playing_.begin(), playing_.end(), z) != playing_.end(); });
            const auto colour = missing ? t.roles.warning : lit ? t.active : t.value;
            g.setColour(lit ? colour : t.fill(colour));
            g.fillRect(r);
            g.setColour(static_cast<int>(i) == hovered_ ? t.textPrimary : colour);
            g.drawRect(r, 1.0f);
            if (grp.zones.size() > 1)
            {
                const auto count = juce::String::fromUTF8("\xC3\x97") + juce::String(grp.zones.size());
                const int needs =
                    juce::GlyphArrangement::getStringWidthInt(ui::font(ui::TextStyle::Caption), count) +
                    t.deviceGrid.displayInset;
                if (static_cast<float>(needs) <= r.getWidth() && r.getHeight() >= keyLabelHeight())
                {
                    g.setColour(lit ? t.onActive : t.textSecondary);
                    g.drawText(count, r, juce::Justification::centred, false);
                }
            }
        }
    }

    // ---- hover, click, tooltip ----------------------------------------------------------

    void mouseMove(const juce::MouseEvent& e) override
    {
        pointer_ = e.position;
        setHovered(groupAt(e.position));
    }
    void mouseExit(const juce::MouseEvent&) override
    {
        pointer_ = {-1.0f, -1.0f};
        setHovered(-1);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (map_ == nullptr)
        {
            browse();
            return;
        }
        const int i = groupAt(e.position);
        if (i < 0 || !ctx_.playNote)
            return;
        // Its root, at the middle of its velocities.
        const auto& z = map_->zones[static_cast<size_t>(groups_[static_cast<size_t>(i)].zones.front())];
        playing_note_ = z.root;
        ctx_.playNote(playing_note_, static_cast<float>(std::max(1, (z.loVel + z.hiVel) / 2)) / 127.0f);
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (playing_note_ >= 0 && ctx_.playNote)
            ctx_.playNote(playing_note_, 0.0f);
        playing_note_ = -1;
    }

    juce::String getTooltip() override
    {
        if (map_ != nullptr && !map_->single && textLine().toFloat().contains(pointer_))
            return problemDetails();
        if (!juce::isPositiveAndBelow(hovered_, static_cast<int>(groups_.size())))
            return {};
        const auto& grp = groups_[static_cast<size_t>(hovered_)];
        const auto& z = map_->zones[static_cast<size_t>(grp.zones.front())];
        const auto note = [this](int key)
        { return juce::String(ctx_.info.params[S::Root].toText(static_cast<float>(key))); };
        const auto dot = juce::String::fromUTF8(" \xC2\xB7 ");
        auto text =
            fileName(z.file) + (z.sample == nullptr ? " (missing)" : juce::String()) + dot +
            (z.loKey == z.hiKey ? note(z.loKey)
                                : note(z.loKey) + juce::String::fromUTF8("\xE2\x80\x93") + note(z.hiKey)) +
            dot + "velocity " + juce::String(z.loVel) + juce::String::fromUTF8("\xE2\x80\x93") +
            juce::String(z.hiVel) + dot + "root " + note(z.root);
        if (grp.zones.size() > 1)
            text += dot + juce::String(grp.zones.size()) + (grp.random ? " chosen at random" : " take turns");
        return text;
    }

    // ---- files --------------------------------------------------------------------------

    bool isInterestedInFileDrag(const juce::StringArray& files) override
    {
        return files.size() == 1 && isLoadable(files[0]);
    }
    void fileDragEnter(const juce::StringArray&, int, int) override { setDragOver(true); }
    void fileDragExit(const juce::StringArray&) override { setDragOver(false); }
    void filesDropped(const juce::StringArray& files, int, int) override
    {
        setDragOver(false);
        if (onLoad)
            onLoad(files[0]);
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        if (groupAt(e.position) < 0)
            browse();
    }

private:
    struct Group
    {
        int loKey = 0, hiKey = 0, loVel = 0, hiVel = 0;
        std::vector<int> zones;
        bool random = false;
    };

    void group()
    {
        groups_.clear();
        hovered_ = -1;
        if (map_ == nullptr || map_->single)
            return;
        for (size_t i = 0; i < map_->zones.size(); ++i)
        {
            const auto& z = map_->zones[i];
            auto it = std::find_if(groups_.begin(), groups_.end(),
                                   [&z](const Group& g)
                                   {
                                       return g.loKey == z.loKey && g.hiKey == z.hiKey &&
                                              g.loVel == z.loVel && g.hiVel == z.hiVel;
                                   });
            if (it == groups_.end())
                it = groups_.insert(groups_.end(), Group{z.loKey, z.hiKey, z.loVel, z.hiVel, {}, false});
            it->zones.push_back(static_cast<int>(i));
            it->random |= z.loRand > 0.0f || z.hiRand < 1.0f;
        }
        // The instrument's keys and a little either side, whole octaves.
        low_ = std::max(0, (map_->lowestKey() - 2) / 12 * 12);
        high_ = std::min(127, (map_->highestKey() + 13) / 12 * 12 - 1);
    }

    /// What didn't make it, short: "4 missing · 3 ignored".
    [[nodiscard]] juce::String problems() const
    {
        juce::StringArray parts;
        if (map_->missing > 0)
            parts.add(juce::String(map_->missing) + " missing");
        if (!map_->ignored.empty())
            parts.add(juce::String(map_->ignored.size()) + " ignored");
        return parts.joinIntoString(juce::String::fromUTF8("  \xC2\xB7  "));
    }

    /// The same in full, for the line's tooltip.
    [[nodiscard]] juce::String problemDetails() const
    {
        juce::StringArray lines;
        if (map_->missing > 0)
        {
            juce::StringArray files;
            for (const auto& z : map_->zones)
                if (z.sample == nullptr)
                    files.addIfNotAlreadyThere(fileName(z.file));
            lines.add(juce::String(map_->missing) + " of " + juce::String(map_->zones.size()) +
                      " samples missing: " + files.joinIntoString(", "));
        }
        if (!map_->ignored.empty())
        {
            juce::StringArray names;
            for (const auto& i : map_->ignored)
                names.add(juce::String::fromUTF8(i.c_str()));
            lines.add("In the file but not played: " + names.joinIntoString(", "));
        }
        return lines.joinIntoString("\n");
    }

    [[nodiscard]] float keyLabelHeight() const
    {
        return static_cast<float>(ui::Theme::current().deviceGrid.displayTextHeight);
    }

    [[nodiscard]] juce::Rectangle<int> textLine() const
    {
        const auto& m = ui::Theme::current().deviceGrid;
        return getLocalBounds()
            .reduced(m.displayInset, m.displayInset / 2)
            .removeFromTop(m.displayTextHeight);
    }

    [[nodiscard]] juce::Rectangle<float> zoneArea() const
    {
        const auto& m = ui::Theme::current().deviceGrid;
        return getLocalBounds()
            .toFloat()
            .reduced(static_cast<float>(m.displayInset), static_cast<float>(m.displayInset) * 0.5f)
            .withTrimmedTop(static_cast<float>(m.displayTextHeight))
            .withTrimmedBottom(keyLabelHeight());
    }

    [[nodiscard]] float xFor(int key) const
    {
        const auto a = zoneArea();
        return a.getX() +
               a.getWidth() * static_cast<float>(key - low_) / static_cast<float>(high_ - low_ + 1);
    }

    [[nodiscard]] juce::Rectangle<float> rectFor(const Group& grp) const
    {
        const auto a = zoneArea();
        const auto y = [&a](int vel)
        { return a.getBottom() - a.getHeight() * static_cast<float>(vel) / 128.0f; };
        return juce::Rectangle<float>::leftTopRightBottom(xFor(grp.loKey), y(grp.hiVel + 1),
                                                          xFor(grp.hiKey + 1), y(grp.loVel));
    }

    [[nodiscard]] int groupAt(juce::Point<float> p) const
    {
        // The smallest under the pointer, so a layer drawn over a wider one can be reached.
        int best = -1;
        float bestArea = 0.0f;
        for (size_t i = 0; i < groups_.size(); ++i)
            if (const auto r = rectFor(groups_[i]);
                r.contains(p) && (best < 0 || r.getWidth() * r.getHeight() < bestArea))
            {
                best = static_cast<int>(i);
                bestArea = r.getWidth() * r.getHeight();
            }
        return best;
    }

    void setHovered(int i)
    {
        if (i == hovered_)
            return;
        hovered_ = i;
        repaint();
    }

    void setDragOver(bool over)
    {
        dragOver_ = over;
        repaint();
    }

    void browse()
    {
        browseFor(chooser_, *this,
                  [this](const juce::String& path)
                  {
                      if (onLoad)
                          onLoad(path);
                  });
    }

    ui::DeviceUiContext& ctx_;
    const Sampler* sampler_;
    std::shared_ptr<const SampleMap> map_;
    std::vector<Group> groups_;
    int low_ = 0, high_ = 127;
    int hovered_ = -1;
    juce::Point<float> pointer_{-1.0f, -1.0f};
    int playing_note_ = -1;
    std::array<int, S::numPlayheads> playing_{};
    std::unique_ptr<juce::FileChooser> chooser_;
    bool dragOver_ = false;
};

/// Sections per devices/sampler/SPEC.md, on the module grid (docs/design/device-panels.md).
class SamplerPanel final : public ui::DevicePanel, public juce::FileDragAndDropTarget
{
public:
    explicit SamplerPanel(ui::DeviceUiContext& ctx)
        : DevicePanel(ctx), sampler_(dynamic_cast<const Sampler*>(ctx.device))
    {
        sample_ = &addSection("sample", "Sample", 6);
        wave_ = &own(std::make_unique<WaveformView>(ctx));
        zones_ = &own(std::make_unique<ZoneMap>(ctx));
        zones_->onLoad = [this](const juce::String& path) { wave_->load(path); };
        placeSample();
        wave_->onMapChanged = [safe = juce::Component::SafePointer<SamplerPanel>(this)]
        {
            // Laid out again outside the refresh that noticed it.
            juce::MessageManager::callAsync(
                [safe]
                {
                    if (safe != nullptr)
                        safe->placeSample();
                });
        };

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
        auto& root = voice.bar(S::Root, 0, 1, 2);
        voice.bar(S::VoiceCount, 0, 2, 2);
        voice.bar(S::VoiceGlide, 0, 3, 2);
        voice.bar(S::VoiceBend, 0, 4, 2);
        // A multisample's zones have their own roots (SPEC, S3).
        dimWhen({&root}, [this] { return multisample(); });

        if (auto* bus = ctx.analysis)
            meter_.setLevelSource([bus](int) { return bus->takePeak(S::PeakOut); });
        setMeters({{&meter_, "Out"}});
    }

    // Dropping anywhere on the panel loads the file (the sample views handle their own areas).
    bool isInterestedInFileDrag(const juce::StringArray& files) override { return wave_->isInterestedInFileDrag(files); }
    void filesDropped(const juce::StringArray& files, int x, int y) override { wave_->filesDropped(files, x, y); }

    /// Module > Load sample… (SPEC, S10).
    bool performCommand(std::string_view id) override
    {
        if (id != "sample.load")
            return false;
        wave_->browse();
        return true;
    }
    bool takeFile(const juce::File& file) override
    {
        if (!isLoadable(file.getFullPathName()))
            return false;
        wave_->load(file.getFullPathName());
        return true;
    }

private:
    [[nodiscard]] bool multisample() const
    {
        const auto map = sampler_ != nullptr ? sampler_->map() : nullptr;
        return map != nullptr && !map->single;
    }

    /// One sample: its waveform fills the section. A multisample: the zone map, and below it
    /// the last zone's waveform (SPEC, S9).
    void placeSample()
    {
        const bool multi = multisample();
        if (placedMulti_ && *placedMulti_ == multi)
            return;
        placedMulti_ = multi;
        sample_->clear();
        if (multi)
        {
            sample_->place(*zones_, 0, 0, 6, 4);
            sample_->place(*wave_, 0, 4, 6, 2);
        }
        else
        {
            sample_->place(*wave_, 0, 0, 6, 6);
            zones_->setVisible(false);
        }
        zones_->refresh();
        wave_->refresh();
        resized();
    }

    const Sampler* sampler_;
    ui::PanelSection* sample_ = nullptr;
    WaveformView* wave_ = nullptr;
    ZoneMap* zones_ = nullptr;
    std::optional<bool> placedMulti_;
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
