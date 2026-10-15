#include "ui/SettingsView.h"

#include "model/Recording.h"

#include <cmath>
#include <set>

namespace fd::app
{
using model::SettingsSection;

namespace
{
juce::String text(const std::string& s)
{
    return juce::String::fromUTF8(s.c_str());
}

/// A line of text in a row's control area: a path, a count, a readout.
class Readout final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Readout(ui::TextStyle style = ui::TextStyle::Body) : style_(style) {}
    void setText(const juce::String& t)
    {
        if (t == text_)
            return;
        text_ = t;
        setTooltip(t);
        repaint();
    }
    [[nodiscard]] const juce::String& getText() const noexcept { return text_; }
    void paint(juce::Graphics& g) override
    {
        g.setFont(ui::font(style_));
        g.setColour(ui::Theme::current().textSecondary);
        g.drawText(text_, getLocalBounds(), juce::Justification::centredLeft, true);
    }

private:
    ui::TextStyle style_;
    juce::String text_;
};

juce::String rateText(double rate)
{
    return juce::String(rate / 1000.0, rate == std::round(rate / 1000.0) * 1000.0 ? 0 : 1) + " kHz";
}

juce::String withDefault(juce::String label, bool isDefault)
{
    return isDefault ? label + " (default)" : label;
}

constexpr double kDefaultRate = 48000.0;
constexpr int kDefaultBuffer = 256;
} // namespace

// ---- rows ----------------------------------------------------------------------------------

struct SettingsView::Row
{
    std::string id; // a catalogue id, "key:<action>", or "heading:<category>"
    SettingsSection section = SettingsSection::Audio;
    juce::String name, description, searchText;
    bool heading = false;
    bool shortcut = false; // Keys: a long name, its shortcut narrower at the controls' right edge
    std::vector<juce::Component*> controls;
    int lines = 1; // control lines (lists have more)
    std::function<void()> refresh;
    std::function<bool()> changed;
    std::function<void()> reset;
    /// Places the controls in the control area (lines tall); default: left to right.
    std::function<void(juce::Rectangle<int>)> place;
    juce::Rectangle<int> bounds; // in the content
    bool shown = true;

    [[nodiscard]] int height() const
    {
        const auto& s = ui::Theme::current().shell;
        if (heading)
            return s.rowHeight;
        int h = lines * s.fieldHeight + (lines - 1) * s.itemGap;
        if (description.isNotEmpty())
            h += s.itemGap + s.rowHeight;
        return h;
    }
};

/// A shortcut, as a button: a click starts changing it.
class SettingsView::KeyButton final : public juce::Button
{
public:
    KeyButton(SettingsView& view, std::string action)
        : juce::Button("shortcut"), view_(view), action_(std::move(action))
    {
        setWantsKeyboardFocus(false);
        onClick = [this] { view_.startKeyCapture(action_); };
    }
    void paintButton(juce::Graphics& g, bool hovered, bool down) override
    {
        const auto& t = ui::Theme::current();
        const bool capturing = view_.capturing_ == action_;
        g.setColour(t.lift(t.surface3, hovered, down));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), t.radiusControl);
        if (capturing)
        {
            g.setColour(t.active);
            g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), t.radiusControl, 1.0f);
        }
        const auto shortcut = view_.actions_.shortcut(action_);
        g.setFont(ui::font(capturing || shortcut.isEmpty() ? ui::TextStyle::Caption : ui::TextStyle::Value));
        g.setColour(capturing ? t.textPrimary : (shortcut.isEmpty() ? t.textTertiary : t.textPrimary));
        g.drawText(capturing ? juce::String::fromUTF8("Press keys\xE2\x80\xA6")
                             : (shortcut.isEmpty() ? juce::String("None") : shortcut),
                   getLocalBounds().reduced(t.shell.fieldTextInset, 0), juce::Justification::centredLeft,
                   true);
    }

private:
    SettingsView& view_;
    std::string action_;
};

/// The page's column: the sections' titles and lines, each row's name and description (with
/// what the search matched marked), its controls placed by the view.
class SettingsView::Content final : public juce::Component
{
public:
    explicit Content(SettingsView& view) : view_(view) {}

    void paint(juce::Graphics& g) override
    {
        const auto& t = ui::Theme::current();
        const auto& s = t.shell;
        view_.stack_.paint(g);
        const auto query = view_.search_.getText().toStdString();
        const bool flashing = juce::Time::getMillisecondCounterHiRes() * 0.001 < view_.flashUntil_;
        for (const auto& r : view_.rows_)
        {
            if (!r->shown || !r->bounds.intersects(g.getClipBounds()))
                continue;
            if (flashing && r->id == view_.flash_)
            {
                g.setColour(t.roles.hoverWash);
                g.fillRoundedRectangle(r->bounds.expanded(s.itemGap, s.itemGap / 2).toFloat(),
                                       t.radiusControl); // within the gaps
            }
            if (r->heading)
            {
                g.setFont(ui::font(ui::TextStyle::Label));
                g.setColour(t.textSecondary);
                g.drawText(r->name, r->bounds, juce::Justification::centredLeft, true);
                continue;
            }
            // The name, a gap before its controls.
            const int nameWidth =
                r->shortcut ? s.settingsLabelWidth + s.settingsControlWidth - s.settingsKeyWidth - s.groupGap
                            : s.settingsLabelWidth - s.groupGap;
            paintMarked(g, r->name, r->bounds.withHeight(s.fieldHeight).withWidth(nameWidth),
                        ui::TextStyle::Body, t.textPrimary, query);
            if (r->description.isNotEmpty())
            {
                const auto below = r->bounds.withTrimmedTop(r->bounds.getHeight() - s.rowHeight);
                paintMarked(g, r->description, below, ui::TextStyle::Caption, t.textSecondary, query);
            }
            if (r->id == "key:" + view_.capturing_ && view_.conflict_.isNotEmpty())
            {
                // "Ctrl+D is Duplicate's." on the line under the shortcut, Replace and Cancel after it.
                const auto answer =
                    r->bounds.withTrimmedTop(s.fieldHeight + s.itemGap).withHeight(s.fieldHeight);
                g.setFont(ui::font(ui::TextStyle::Body));
                g.setColour(t.roles.warning);
                g.drawText(view_.conflict_, answer.withRight(view_.replace_.getX() - s.groupGap),
                           juce::Justification::centredRight, true);
            }
        }
    }

private:
    /// `text` in `style`, the query's words marked behind it.
    static void paintMarked(juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area,
                            ui::TextStyle style, juce::Colour colour, const std::string& query)
    {
        const auto f = ui::font(style);
        g.setFont(f);
        if (!query.empty())
        {
            const auto utf8 = text.toStdString();
            g.setColour(ui::Theme::current().roles.textHighlight);
            for (const auto& [start, length] : model::matchRanges(utf8, query))
            {
                const auto before = text.substring(
                    0, static_cast<int>(juce::String::fromUTF8(utf8.substr(0, start).c_str()).length()));
                const auto word = juce::String::fromUTF8(utf8.substr(start, length).c_str());
                const float x = juce::GlyphArrangement::getStringWidth(f, before);
                const float w = juce::GlyphArrangement::getStringWidth(f, word);
                g.fillRect(juce::Rectangle<float>(static_cast<float>(area.getX()) + x,
                                                  static_cast<float>(area.getY()), w,
                                                  static_cast<float>(area.getHeight()))
                               .withSizeKeepingCentre(w, f.getHeight()));
            }
        }
        g.setColour(colour);
        g.drawText(text, area, juce::Justification::centredLeft, true);
    }

    SettingsView& view_;
};

/// The sections down the page's left: a click scrolls to one; those the search left empty dim.
class SettingsView::Index final : public juce::Component
{
public:
    explicit Index(SettingsView& view) : view_(view) {}

    void paint(juce::Graphics& g) override
    {
        const auto& s = ui::Theme::current().shell;
        int y = 0;
        const auto current = currentSection();
        for (const auto section : model::kSettingsSections)
        {
            const bool shown = std::find(view_.shownSections_.begin(), view_.shownSections_.end(), section) !=
                               view_.shownSections_.end();
            ui::paintListRow(g, {0, y, getWidth(), s.listRowHeight},
                             {.name = juce::String(model::sectionName(section)),
                              .enabled = shown,
                              .selected = shown && section == current});
            y += s.listRowHeight;
        }
    }
    void mouseUp(const juce::MouseEvent& e) override
    {
        const int i = e.y / ui::Theme::current().shell.listRowHeight;
        if (i < 0 || i >= static_cast<int>(std::size(model::kSettingsSections)))
            return;
        const auto section = model::kSettingsSections[static_cast<size_t>(i)];
        const auto it = std::find(view_.shownSections_.begin(), view_.shownSections_.end(), section);
        if (it == view_.shownSections_.end())
            return;
        const auto title = view_.stack_.titleArea(static_cast<size_t>(it - view_.shownSections_.begin()));
        view_.viewport_.setViewPosition(0, title.getY());
        repaint();
    }

private:
    /// The section at the top of the view.
    [[nodiscard]] SettingsSection currentSection() const
    {
        const int top = view_.viewport_.getViewPositionY();
        auto current = view_.shownSections_.empty() ? SettingsSection::Audio : view_.shownSections_.front();
        for (size_t i = 0; i < view_.shownSections_.size(); ++i)
            if (view_.stack_.titleArea(i).getY() <= top + ui::Theme::current().shell.rowHeight)
                current = view_.shownSections_[i];
        return current;
    }

    SettingsView& view_;
};

// ---- the view ------------------------------------------------------------------------------

SettingsView::SettingsView(engine::Session& session, ActionRegistry& actions)
    : session_(session), actions_(actions), index_(std::make_unique<Index>(*this)),
      content_(std::make_unique<Content>(*this))
{
    search_.setHint("Search settings");
    search_.addListener(this);
    addAndMakeVisible(search_);
    changedOnly_.setClickingTogglesState(true);
    changedOnly_.setTooltip("Only the settings you've changed from their defaults");
    changedOnly_.onClick = [this] { filter(); };
    addAndMakeVisible(changedOnly_);
    close_.setTooltip("Close settings (Esc)");
    close_.onClick = [this]
    {
        if (onClose)
            onClose();
    };
    addAndMakeVisible(close_);
    addAndMakeVisible(*index_);
    for (auto* b : {&replace_, &cancel_})
    {
        b->setClickingTogglesState(false);
        b->setWantsKeyboardFocus(false);
        content_->addChildComponent(*b);
    }
    replace_.setTooltip("Give the keys to this action; the other is left without");
    replace_.onClick = [this] { answerKeyConflict(true); };
    cancel_.onClick = [this] { answerKeyConflict(false); };
    viewport_.setViewedComponent(content_.get(), false);
    viewport_.setScrollBarsShown(true, false);
    viewport_.onScroll = [this] { index_->repaint(); };
    addAndMakeVisible(viewport_);
    setWantsKeyboardFocus(true);
    rebuild();
}

SettingsView::~SettingsView()
{
    stopTimer();
    cancelPendingUpdate();
    viewport_.setViewedComponent(nullptr, false);
}

void SettingsView::setStore(model::SettingsStore& store)
{
    store_ = &store;
    rebuild();
}

void SettingsView::setSystemTitleBarOption(bool available)
{
    if (titleBarOption_ == available)
        return;
    titleBarOption_ = available;
    rebuild();
}

void SettingsView::change(const std::function<void(model::AppSettings&)>& edit)
{
    store_->change(edit);
    refresh();
}

// ---- making rows ---------------------------------------------------------------------------

SettingsView::Row& SettingsView::addRow(const std::string& id, SettingsSection section)
{
    auto& r = *rows_.emplace_back(std::make_unique<Row>());
    r.id = id;
    r.section = section;
    if (const auto* info = model::findSetting(id))
    {
        r.name = text(info->name);
        r.description = text(info->description);
    }
    return r;
}

juce::ComboBox& SettingsView::combo(Row& row)
{
    auto& c =
        static_cast<juce::ComboBox&>(*components_.emplace_back(std::make_unique<juce::ComboBox>(row.id)));
    c.setWantsKeyboardFocus(false);
    content_->addAndMakeVisible(c);
    row.controls.push_back(&c);
    return c;
}

ui::Toggle& SettingsView::toggle(Row& row, const juce::String& label, bool onOff)
{
    auto& b = static_cast<ui::Toggle&>(*components_.emplace_back(std::make_unique<ui::Toggle>(label)));
    b.setClickingTogglesState(onOff);
    b.setWantsKeyboardFocus(false);
    content_->addAndMakeVisible(b);
    row.controls.push_back(&b);
    return b;
}

std::string SettingsView::signature() const
{
    // What the rows are made of: devices, folders, plugins skipped, the title bar option.
    std::string out;
    auto& dm = session_.deviceManager();
    out += dm.getCurrentAudioDeviceType().toStdString() + "|";
    if (auto* d = dm.getCurrentAudioDevice())
        out += d->getName().toStdString() + "/" + std::to_string(d->getOutputChannelNames().size()) + "/" +
               std::to_string(d->getInputChannelNames().size()) + "|";
    for (const auto& m : session_.midiDevices())
        out += m.name + ",";
    for (const auto& f : settings().browser.places)
        out += f + ";";
    for (const auto& f : settings().plugins.folders)
        out += f + ";";
    for (const auto& f : session_.skippedPlugins())
        out += f + ";";
    out += titleBarOption_ ? "t" : "";
    return out;
}

void SettingsView::rebuild()
{
    capturing_.clear();
    conflict_.clear();
    rows_.clear();
    for (auto& c : components_)
        content_->removeChildComponent(c.get());
    components_.clear();
    addAudio();
    addMidi();
    addRecording();
    addLibrary();
    addPlugins();
    addExport();
    addKeys();
    addDisplay();
    addAssistants();
    for (const auto section : model::kSettingsSections)
        if (!resets_.contains(section))
        {
            const bool keys = section == SettingsSection::Keys;
            auto& b =
                *(resets_[section] = std::make_unique<ui::Toggle>(keys ? "Reset all shortcuts" : "Reset"));
            b.setClickingTogglesState(false);
            b.setWantsKeyboardFocus(false);
            b.setTooltip(keys ? juce::String("Every shortcut back to its default")
                              : "Every setting here back to its default");
            b.onClick = [this, section] { resetSection(section); };
            content_->addChildComponent(b);
        }
    signature_ = signature();
    refresh();
}

void SettingsView::refresh()
{
    if (signature() != signature_)
        triggerAsyncUpdate(); // a list changed: its rows are made again, once this callback returns
    for (auto& r : rows_)
        if (r->refresh)
            r->refresh();
    filter();
}

// ---- Audio ---------------------------------------------------------------------------------

void SettingsView::addAudio()
{
    auto& dm = session_.deviceManager();
    auto setup = [&dm] { return dm.getAudioDeviceSetup(); };
    auto apply = [this, &dm](const juce::AudioDeviceManager::AudioDeviceSetup& s)
    {
        dm.setAudioDeviceSetup(s, true);
        refresh();
    };

    {
        auto& r = addRow("audio.driver", SettingsSection::Audio);
        auto& c = combo(r);
        const auto& types = dm.getAvailableDeviceTypes();
        for (int i = 0; i < types.size(); ++i)
            c.addItem(types[i]->getTypeName(), i + 1);
        r.refresh = [&c, &dm]
        {
            const auto& all = dm.getAvailableDeviceTypes();
            for (int i = 0; i < all.size(); ++i)
                if (all[i]->getTypeName() == dm.getCurrentAudioDeviceType())
                    c.setSelectedId(i + 1, juce::dontSendNotification);
        };
        c.onChange = [this, &c, &dm]
        {
            const auto& all = dm.getAvailableDeviceTypes();
            if (const int i = c.getSelectedId() - 1; juce::isPositiveAndBelow(i, all.size()))
                dm.setCurrentAudioDeviceType(all[i]->getTypeName(), true);
            refresh();
        };
    }
    auto* type = dm.getCurrentDeviceTypeObject();
    for (const bool input : {false, true})
    {
        auto& r = addRow(input ? "audio.input" : "audio.output", SettingsSection::Audio);
        auto& c = combo(r);
        const auto names = type != nullptr ? type->getDeviceNames(input) : juce::StringArray();
        if (input)
            c.addItem("None", 1);
        for (int i = 0; i < names.size(); ++i)
            c.addItem(names[i], i + 2);
        r.refresh = [&c, setup, input, names]
        {
            const auto now = input ? setup().inputDeviceName : setup().outputDeviceName;
            c.setSelectedId(now.isEmpty() ? 1 : names.indexOf(now) + 2, juce::dontSendNotification);
        };
        c.onChange = [&c, setup, apply, input, names]
        {
            auto s = setup();
            const int i = c.getSelectedId() - 2;
            (input ? s.inputDeviceName : s.outputDeviceName) =
                juce::isPositiveAndBelow(i, names.size()) ? names[i] : juce::String();
            apply(s);
        };
    }
    auto* device = dm.getCurrentAudioDevice();
    if (device == nullptr)
        return;
    for (const bool input : {false, true})
    {
        const auto channelNames = input ? device->getInputChannelNames() : device->getOutputChannelNames();
        if (channelNames.isEmpty())
            continue;
        auto& r = addRow(input ? "audio.inputChannels" : "audio.outputChannels", SettingsSection::Audio);
        const int pairs = (channelNames.size() + 1) / 2;
        for (int p = 0; p < pairs; ++p)
        {
            const auto label =
                juce::String(2 * p + 1) +
                (2 * p + 1 < channelNames.size() ? "/" + juce::String(2 * p + 2) : juce::String());
            auto& b = toggle(r, label, true);
            b.setTooltip(channelNames[2 * p] + (2 * p + 1 < channelNames.size()
                                                    ? " + " + channelNames[2 * p + 1]
                                                    : juce::String()));
            b.onClick = [&b, setup, apply, input, p]
            {
                auto s = setup();
                auto& bits = input ? s.inputChannels : s.outputChannels;
                bits.setRange(2 * p, 2, b.getToggleState());
                (input ? s.useDefaultInputChannels : s.useDefaultOutputChannels) = false;
                apply(s);
            };
        }
        // As many pairs to a line as fit, each as wide as the widest.
        const auto& sh = ui::Theme::current().shell;
        const int w =
            ui::Toggle::widthFor(juce::String(channelNames.size()) + "/" + juce::String(channelNames.size()));
        const int perLine = std::max(1, (sh.settingsControlWidth + sh.itemGap) / (w + sh.itemGap));
        r.lines = (pairs + perLine - 1) / perLine;
        r.place = [&r, w, perLine](juce::Rectangle<int> area)
        {
            const auto& g = ui::Theme::current().shell;
            for (size_t i = 0; i < r.controls.size(); ++i)
            {
                const int line = static_cast<int>(i) / perLine, column = static_cast<int>(i) % perLine;
                r.controls[i]->setBounds(area.getX() + column * (w + g.itemGap),
                                         area.getY() + line * (g.fieldHeight + g.itemGap), w, g.fieldHeight);
            }
        };
        r.refresh = [&r, setup, input]
        {
            const auto s = setup();
            const auto& bits = input ? s.inputChannels : s.outputChannels;
            for (size_t p = 0; p < r.controls.size(); ++p)
                static_cast<ui::Toggle*>(r.controls[p])
                    ->setToggleState(bits[static_cast<int>(2 * p)], juce::dontSendNotification);
        };
    }
    {
        auto& r = addRow("audio.sampleRate", SettingsSection::Audio);
        auto& c = combo(r);
        const auto rates = device->getAvailableSampleRates();
        for (int i = 0; i < rates.size(); ++i)
            c.addItem(withDefault(rateText(rates[i]), rates[i] == kDefaultRate), i + 1);
        r.refresh = [&c, &dm, rates]
        {
            if (auto* d = dm.getCurrentAudioDevice())
                c.setSelectedId(rates.indexOf(d->getCurrentSampleRate()) + 1, juce::dontSendNotification);
        };
        c.onChange = [&c, setup, apply, rates]
        {
            auto s = setup();
            if (const int i = c.getSelectedId() - 1; juce::isPositiveAndBelow(i, rates.size()))
                s.sampleRate = rates[i];
            apply(s);
        };
        r.changed = [&dm, rates]
        {
            auto* d = dm.getCurrentAudioDevice();
            return d != nullptr && rates.contains(kDefaultRate) && d->getCurrentSampleRate() != kDefaultRate;
        };
        r.reset = [setup, apply, rates]
        {
            auto s = setup();
            if (rates.contains(kDefaultRate))
                s.sampleRate = kDefaultRate;
            apply(s);
        };
    }
    {
        auto& r = addRow("audio.buffer", SettingsSection::Audio);
        auto& c = combo(r);
        const auto sizes = device->getAvailableBufferSizes();
        const double rate =
            device->getCurrentSampleRate() > 0.0 ? device->getCurrentSampleRate() : kDefaultRate;
        for (int i = 0; i < sizes.size(); ++i)
            c.addItem(withDefault(juce::String(sizes[i]) + " samples" + juce::String::fromUTF8(" \xC2\xB7 ") +
                                      juce::String(1000.0 * sizes[i] / rate, 1) + " ms",
                                  sizes[i] == kDefaultBuffer),
                      i + 1);
        r.refresh = [&c, &dm, sizes]
        {
            if (auto* d = dm.getCurrentAudioDevice())
                c.setSelectedId(sizes.indexOf(d->getCurrentBufferSizeSamples()) + 1,
                                juce::dontSendNotification);
        };
        c.onChange = [&c, setup, apply, sizes]
        {
            auto s = setup();
            if (const int i = c.getSelectedId() - 1; juce::isPositiveAndBelow(i, sizes.size()))
                s.bufferSize = sizes[i];
            apply(s);
        };
        r.changed = [&dm, sizes]
        {
            auto* d = dm.getCurrentAudioDevice();
            return d != nullptr && sizes.contains(kDefaultBuffer) &&
                   d->getCurrentBufferSizeSamples() != kDefaultBuffer;
        };
        r.reset = [setup, apply, sizes]
        {
            auto s = setup();
            if (sizes.contains(kDefaultBuffer))
                s.bufferSize = kDefaultBuffer;
            apply(s);
        };
    }
    {
        auto& r = addRow("audio.test", SettingsSection::Audio);
        auto& b = toggle(r, "Play", false);
        b.onClick = [&dm] { dm.playTestSound(); };
    }
    {
        auto& r = addRow("audio.load", SettingsSection::Audio);
        auto& readout =
            static_cast<Readout&>(*components_.emplace_back(std::make_unique<Readout>(ui::TextStyle::Value)));
        content_->addAndMakeVisible(readout);
        r.controls.push_back(&readout);
        r.refresh = [this, &readout, &dm]
        {
            const int dropouts = std::max(0, dm.getXRunCount() - xruns_);
            readout.setText("CPU " + juce::String(juce::roundToInt(dm.getCpuUsage() * 100.0)) + " %" +
                            juce::String::fromUTF8(" \xC2\xB7 ") +
                            (dropouts == 0
                                 ? juce::String("no dropouts")
                                 : juce::String(dropouts) + (dropouts == 1 ? " dropout" : " dropouts")));
        };
    }
    // What follows the tempo when it's placed or made (audio-editor.md §7.2): both on by default.
    for (auto [id, flag] :
         {std::pair{"audioClips.loopsFollowTempo", &model::AppSettings::AudioClips::loopsFollowTempo},
          std::pair{"audioClips.recordingsFollowTempo",
                    &model::AppSettings::AudioClips::recordingsFollowTempo}})
    {
        auto& r = addRow(id, SettingsSection::Audio);
        auto& b = toggle(r, "On", true);
        b.onClick = [this, &b, member = flag]
        { change([&b, member](model::AppSettings& s) { s.audioClips.*member = b.getToggleState(); }); };
        r.refresh = [this, &b, member = flag]
        {
            const bool on = settings().audioClips.*member;
            b.setToggleState(on, juce::dontSendNotification);
            b.setButtonText(on ? "On" : "Off");
        };
        r.changed = [this, member = flag] { return !(settings().audioClips.*member); };
        r.reset = [this, member = flag]
        { store_->change([member](model::AppSettings& s) { s.audioClips.*member = true; }); };
    }
}

// ---- MIDI ----------------------------------------------------------------------------------

void SettingsView::addMidi()
{
    {
        auto& r = addRow("midi.inputs", SettingsSection::Midi);
        const auto devices = session_.midiDevices();
        if (devices.empty())
        {
            auto& none = static_cast<Readout&>(*components_.emplace_back(std::make_unique<Readout>()));
            none.setText("None connected");
            content_->addAndMakeVisible(none);
            r.controls.push_back(&none);
        }
        for (const auto& d : devices)
        {
            auto& b = toggle(r, text(d.name), true);
            b.onClick = [this, &b, name = d.name]
            {
                session_.setMidiDeviceEnabled(name, b.getToggleState());
                refresh();
            };
        }
        r.lines = std::max(1, static_cast<int>(devices.size()));
        r.place = [&r](juce::Rectangle<int> area)
        {
            const auto& s = ui::Theme::current().shell;
            for (auto* c : r.controls)
            {
                const int w =
                    dynamic_cast<ui::Toggle*>(c) != nullptr
                        ? std::min(area.getWidth(),
                                   ui::Toggle::widthFor(static_cast<ui::Toggle*>(c)->getButtonText()))
                        : area.getWidth();
                c->setBounds(area.removeFromTop(s.fieldHeight).withWidth(w));
                area.removeFromTop(s.itemGap);
            }
        };
        r.refresh = [this, &r]
        {
            const auto now = session_.midiDevices();
            for (size_t i = 0; i < r.controls.size() && i < now.size(); ++i)
                if (auto* b = dynamic_cast<ui::Toggle*>(r.controls[i]))
                    b->setToggleState(now[i].enabled, juce::dontSendNotification);
        };
        r.changed = [this]
        {
            const auto now = session_.midiDevices();
            return std::any_of(now.begin(), now.end(), [](const auto& d) { return !d.enabled; });
        };
        r.reset = [this]
        {
            for (const auto& d : session_.midiDevices())
                session_.setMidiDeviceEnabled(d.name, true);
        };
    }
    {
        auto& r = addRow("midi.computerKeyboard", SettingsSection::Midi);
        auto& b = toggle(r, "Off", true);
        b.onClick = [this, &b]
        { change([&b](model::AppSettings& s) { s.midi.computerKeyboard = b.getToggleState(); }); };
        r.refresh = [this, &b]
        {
            b.setToggleState(settings().midi.computerKeyboard, juce::dontSendNotification);
            b.setButtonText(settings().midi.computerKeyboard ? "On" : "Off");
        };
        r.changed = [this] { return settings().midi.computerKeyboard; };
        r.reset = [this] { store_->change([](model::AppSettings& s) { s.midi.computerKeyboard = false; }); };
    }
}

// ---- Recording -----------------------------------------------------------------------------

void SettingsView::addRecording()
{
    using F = model::RecordingFormat;
    static constexpr F formats[] = {F::Wav, F::Flac, F::Aiff};
    {
        auto& r = addRow("recording.format", SettingsSection::Recording);
        auto& c = combo(r);
        for (int i = 0; i < 3; ++i)
            c.addItem(withDefault(text(model::formatName(formats[i])), formats[i] == F::Wav), i + 1);
        r.refresh = [this, &c]
        {
            for (int i = 0; i < 3; ++i)
                if (formats[i] == settings().recording.format)
                    c.setSelectedId(i + 1, juce::dontSendNotification);
        };
        c.onChange = [this, &c]
        {
            change(
                [&c](model::AppSettings& s)
                {
                    s.recording.format = formats[std::clamp(c.getSelectedId() - 1, 0, 2)];
                    if (const auto depths = model::bitDepthsFor(s.recording.format);
                        std::find(depths.begin(), depths.end(), s.recording.bitDepth) == depths.end())
                        s.recording.bitDepth = 24;
                });
        };
        r.changed = [this] { return settings().recording.format != F::Wav; };
    }
    {
        auto& r = addRow("recording.bitDepth", SettingsSection::Recording);
        auto& c = combo(r);
        r.refresh = [this, &c, madeFor = std::optional<F>()]() mutable
        {
            if (madeFor != settings().recording.format) // the depths the format holds
            {
                madeFor = settings().recording.format;
                c.clear(juce::dontSendNotification);
                for (const int d : model::bitDepthsFor(*madeFor))
                    c.addItem(withDefault(juce::String(d) + (d == 32 ? "-bit float" : "-bit"), d == 24), d);
            }
            c.setSelectedId(settings().recording.bitDepth, juce::dontSendNotification);
        };
        c.onChange = [this, &c]
        {
            if (const int d = c.getSelectedId(); d > 0)
                change([d](model::AppSettings& s) { s.recording.bitDepth = d; });
        };
        r.changed = [this] { return settings().recording.bitDepth != 24; };
        r.reset = [this] { store_->change([](model::AppSettings& s) { s.recording = {}; }); };
    }
    {
        using C = model::CountIn;
        static constexpr std::pair<C, const char*> counts[] = {{C::Off, "Off"},
                                                               {C::OneBeat, "1 beat"},
                                                               {C::TwoBeats, "2 beats"},
                                                               {C::OneBar, "1 bar"},
                                                               {C::TwoBars, "2 bars"}};
        auto& r = addRow("recording.countIn", SettingsSection::Recording);
        auto& c = combo(r);
        for (int i = 0; i < 5; ++i)
            c.addItem(withDefault(counts[i].second, counts[i].first == C::OneBar), i + 1);
        r.refresh = [this, &c]
        {
            for (int i = 0; i < 5; ++i)
                if (counts[i].first == session_.countIn())
                    c.setSelectedId(i + 1, juce::dontSendNotification);
        };
        c.onChange = [this, &c]
        {
            session_.setCountIn(counts[std::clamp(c.getSelectedId() - 1, 0, 4)].first);
            refresh();
        };
        r.changed = [this] { return session_.countIn() != C::OneBar; };
        r.reset = [this] { session_.setCountIn(C::OneBar); };
    }
    for (const bool midi : {false, true})
    {
        auto& r = addRow(midi ? "recording.midiOffset" : "recording.audioOffset", SettingsSection::Recording);
        auto& f =
            static_cast<ui::NumberField&>(*components_.emplace_back(std::make_unique<ui::NumberField>()));
        f.setRange(-500.0, 500.0, 1.0);
        f.textFromValue = [](double v)
        {
            return withDefault((v > 0 ? "+" : "") + juce::String(juce::roundToInt(v)) + " ms",
                               juce::roundToInt(v) == 0);
        };
        f.valueFromText = [](const juce::String& s) -> std::optional<double>
        {
            const auto t = s.retainCharacters("+-0123456789.");
            return t.isEmpty() ? std::nullopt : std::optional(t.getDoubleValue());
        };
        f.onValueChange = [this, midi](double v)
        {
            const auto [audio, midiMs] = session_.recordingOffsets();
            session_.setRecordingOffsets(midi ? audio : v, midi ? v : midiMs);
        };
        content_->addAndMakeVisible(f);
        r.controls.push_back(&f);
        r.refresh = [this, &f, midi]
        {
            const auto [audio, midiMs] = session_.recordingOffsets();
            f.setValue(midi ? midiMs : audio, juce::dontSendNotification);
        };
        r.changed = [this, midi]
        {
            const auto [audio, midiMs] = session_.recordingOffsets();
            return (midi ? midiMs : audio) != 0.0;
        };
        r.reset = [this] { session_.setRecordingOffsets(0.0, 0.0); };
    }
}

// ---- Library -------------------------------------------------------------------------------

juce::File SettingsView::segnoFolder() const
{
    return settings().library.folder.empty()
               ? juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Segno")
               : juce::File(text(settings().library.folder));
}

void SettingsView::moveSegnoFolder(const juce::File& to)
{
    const auto from = segnoFolder();
    if (to == from)
        return;
    (void)to.createDirectory();
    // Your presets come along; an unsaved project's recordings stay where it's using them, and
    // new unsaved projects start in the new place.
    const auto dst = to.getChildFile("Presets");
    const juce::File presets(text(session_.presetLibrary().root().string()));
    if (presets.isDirectory() && presets.isAChildOf(from) && !dst.exists() && !presets.moveFileTo(dst))
        if (presets.copyDirectoryTo(dst))
            presets.deleteRecursively();
    const auto standard =
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Segno");
    change([&](model::AppSettings& s)
           { s.library.folder = to == standard ? std::string() : to.getFullPathName().toStdString(); });
}

void SettingsView::chooseFolder(const juce::String& title, std::function<void(const juce::File&)> chosen)
{
    chooser_ = std::make_unique<juce::FileChooser>(title, segnoFolder());
    chooser_->launchAsync(juce::FileBrowserComponent::openMode |
                              juce::FileBrowserComponent::canSelectDirectories,
                          [done = std::move(chosen)](const juce::FileChooser& fc)
                          {
                              if (const auto f = fc.getResult(); f != juce::File())
                                  done(f);
                          });
}

void SettingsView::addLibrary()
{
    auto path = [](const juce::File& f)
    {
        const auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory).getFullPathName();
        auto p = f.getFullPathName();
        return p.startsWith(home) ? "~" + p.substring(home.length()) : p;
    };
    {
        // The Library (ADR 0055): where it was found, or that it wasn't.
        auto& r = addRow("library.location", SettingsSection::Library);
        auto& where = static_cast<Readout&>(*components_.emplace_back(std::make_unique<Readout>()));
        content_->addAndMakeVisible(where);
        r.controls.push_back(&where);
        auto& b = toggle(r, juce::String::fromUTF8("Choose\xE2\x80\xA6"), false);
        b.onClick = [this]
        {
            chooseFolder("Where Segno's Library is",
                         [this](const juce::File& f)
                         {
                             change([&f](model::AppSettings& s)
                                    { s.library.location = f.getFullPathName().toStdString(); });
                         });
        };
        r.refresh = [this, &where, path]
        {
            const auto found = session_.libraryFolder();
            where.setText(found != juce::File() ? path(found)
                          : settings().library.location.empty()
                              ? juce::String("Not installed")
                              : path(juce::File(text(settings().library.location))) + " (not found)");
        };
        r.changed = [this] { return !settings().library.location.empty(); };
        r.reset = [this] { store_->change([](model::AppSettings& s) { s.library.location.clear(); }); };
    }
    {
        auto& r = addRow("library.folder", SettingsSection::Library);
        auto& where = static_cast<Readout&>(*components_.emplace_back(std::make_unique<Readout>()));
        content_->addAndMakeVisible(where);
        r.controls.push_back(&where);
        auto& b = toggle(r, juce::String::fromUTF8("Move\xE2\x80\xA6"), false);
        b.onClick = [this]
        { chooseFolder("Move the Segno folder", [this](const juce::File& f) { moveSegnoFolder(f); }); };
        r.refresh = [this, &where, path] { where.setText(path(segnoFolder())); };
        r.changed = [this] { return !settings().library.folder.empty(); };
        r.reset = [this]
        {
            moveSegnoFolder(
                juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Segno"));
        };
    }
    {
        auto& r = addRow("library.newProjects", SettingsSection::Library);
        auto& where = static_cast<Readout&>(*components_.emplace_back(std::make_unique<Readout>()));
        content_->addAndMakeVisible(where);
        r.controls.push_back(&where);
        auto& b = toggle(r, juce::String::fromUTF8("Choose\xE2\x80\xA6"), false);
        b.onClick = [this]
        {
            chooseFolder("Where new projects are saved",
                         [this](const juce::File& f)
                         {
                             change([&f](model::AppSettings& s)
                                    { s.library.newProjects = f.getFullPathName().toStdString(); });
                         });
        };
        r.refresh = [this, &where, path]
        {
            where.setText(settings().library.newProjects.empty()
                              ? juce::String("The Segno folder (default)")
                              : path(juce::File(text(settings().library.newProjects))));
        };
        r.changed = [this] { return !settings().library.newProjects.empty(); };
        r.reset = [this] { store_->change([](model::AppSettings& s) { s.library.newProjects.clear(); }); };
    }
    {
        // The browser's places (browser.md §9): each with Remove, then Add….
        const auto& sh = ui::Theme::current().shell;
        auto& r = addRow("library.places", SettingsSection::Library);
        const auto places = settings().browser.places;
        for (const auto& f : places)
        {
            auto& where = static_cast<Readout&>(*components_.emplace_back(std::make_unique<Readout>()));
            where.setText(path(juce::File(text(f))));
            content_->addAndMakeVisible(where);
            r.controls.push_back(&where);
            auto& remove = toggle(r, "Remove", false);
            remove.onClick = [this, f]
            { change([&f](model::AppSettings& st) { std::erase(st.browser.places, f); }); };
        }
        auto& add = toggle(r, juce::String::fromUTF8("Add\xE2\x80\xA6"), false);
        add.onClick = [this]
        {
            chooseFolder("A folder for the browser",
                         [this](const juce::File& f)
                         {
                             change(
                                 [&f](model::AppSettings& st)
                                 {
                                     const auto p = f.getFullPathName().toStdString();
                                     if (std::find(st.browser.places.begin(), st.browser.places.end(), p) ==
                                         st.browser.places.end())
                                         st.browser.places.push_back(p);
                                 });
                         });
        };
        r.lines = static_cast<int>(places.size()) + 1;
        r.place = [&r, &sh](juce::Rectangle<int> area)
        {
            for (size_t i = 0; i + 1 < r.controls.size(); i += 2)
            {
                auto line = area.removeFromTop(sh.fieldHeight);
                area.removeFromTop(sh.itemGap);
                auto* removeButton = static_cast<ui::Toggle*>(r.controls[i + 1]);
                removeButton->setBounds(
                    line.removeFromRight(ui::Toggle::widthFor(removeButton->getButtonText())));
                r.controls[i]->setBounds(line.withTrimmedRight(sh.itemGap));
            }
            auto* addButton = static_cast<ui::Toggle*>(r.controls.back());
            addButton->setBounds(area.removeFromTop(sh.fieldHeight)
                                     .withWidth(ui::Toggle::widthFor(addButton->getButtonText())));
        };
        r.changed = [this] { return !settings().browser.places.empty(); };
        r.reset = [this] { store_->change([](model::AppSettings& s) { s.browser.places.clear(); }); };
    }
}

// ---- Plugins -------------------------------------------------------------------------------

void SettingsView::addPlugins()
{
    const auto& s = ui::Theme::current().shell;
    auto lineOf = [&s](juce::Rectangle<int>& area)
    {
        auto line = area.removeFromTop(s.fieldHeight);
        area.removeFromTop(s.itemGap);
        return line;
    };
    {
        auto& r = addRow("plugins.folders", SettingsSection::Plugins);
        const auto folders = settings().plugins.folders;
        for (const auto& f : folders)
        {
            auto& where = static_cast<Readout&>(*components_.emplace_back(std::make_unique<Readout>()));
            where.setText(text(f));
            content_->addAndMakeVisible(where);
            r.controls.push_back(&where);
            auto& remove = toggle(r, "Remove", false);
            remove.onClick = [this, f]
            { change([&f](model::AppSettings& st) { std::erase(st.plugins.folders, f); }); };
        }
        auto& add = toggle(r, juce::String::fromUTF8("Add\xE2\x80\xA6"), false);
        add.onClick = [this]
        {
            chooseFolder("A folder with plugins",
                         [this](const juce::File& f)
                         {
                             change(
                                 [&f](model::AppSettings& st)
                                 {
                                     const auto path = f.getFullPathName().toStdString();
                                     if (std::find(st.plugins.folders.begin(), st.plugins.folders.end(),
                                                   path) == st.plugins.folders.end())
                                         st.plugins.folders.push_back(path);
                                 });
                         });
        };
        r.lines = static_cast<int>(folders.size()) + 1;
        r.place = [&r, lineOf](juce::Rectangle<int> area)
        {
            for (size_t i = 0; i + 1 < r.controls.size(); i += 2)
            {
                auto line = lineOf(area);
                auto* removeButton = static_cast<ui::Toggle*>(r.controls[i + 1]);
                removeButton->setBounds(
                    line.removeFromRight(ui::Toggle::widthFor(removeButton->getButtonText())));
                r.controls[i]->setBounds(line.withTrimmedRight(ui::Theme::current().shell.itemGap));
            }
            auto* addButton = static_cast<ui::Toggle*>(r.controls.back());
            addButton->setBounds(lineOf(area).withWidth(ui::Toggle::widthFor(addButton->getButtonText())));
        };
        r.changed = [this] { return !settings().plugins.folders.empty(); };
        r.reset = [this] { store_->change([](model::AppSettings& st) { st.plugins.folders.clear(); }); };
    }
    {
        auto& r = addRow("plugins.scan", SettingsSection::Plugins);
        auto& b = toggle(r, "Scan", false);
        b.onClick = [this] { actions_.perform("app.scanPlugins"); };
        auto& found = static_cast<Readout&>(*components_.emplace_back(std::make_unique<Readout>()));
        content_->addAndMakeVisible(found);
        r.controls.push_back(&found);
        r.refresh = [this, &found, &b]
        {
            const auto all = session_.availableDevices();
            const auto n = std::count_if(all.begin(), all.end(), [](const auto& d) { return !d.native; });
            found.setText(session_.isScanning()
                              ? juce::String::fromUTF8("Scanning\xE2\x80\xA6")
                              : juce::String(n) + (n == 1 ? " plugin found" : " plugins found"));
            b.setEnabled(!session_.isScanning());
        };
    }
    {
        auto& r = addRow("plugins.skipped", SettingsSection::Plugins);
        const auto skipped = session_.skippedPlugins();
        if (skipped.empty())
        {
            auto& none = static_cast<Readout&>(*components_.emplace_back(std::make_unique<Readout>()));
            none.setText("None");
            content_->addAndMakeVisible(none);
            r.controls.push_back(&none);
        }
        for (const auto& f : skipped)
        {
            auto& what = static_cast<Readout&>(*components_.emplace_back(std::make_unique<Readout>()));
            what.setText(juce::File(text(f)).getFileName());
            what.setTooltip(text(f));
            content_->addAndMakeVisible(what);
            r.controls.push_back(&what);
            auto& again = toggle(r, "Try again", false);
            again.setTooltip("Scan it again next time");
            again.onClick = [this, f]
            {
                session_.retrySkippedPlugin(f);
                refresh();
            };
        }
        r.lines = std::max(1, static_cast<int>(skipped.size()));
        if (!skipped.empty())
            r.place = [&r, lineOf](juce::Rectangle<int> area)
            {
                for (size_t i = 0; i + 1 < r.controls.size(); i += 2)
                {
                    auto line = lineOf(area);
                    auto* again = static_cast<ui::Toggle*>(r.controls[i + 1]);
                    again->setBounds(line.removeFromRight(ui::Toggle::widthFor(again->getButtonText())));
                    r.controls[i]->setBounds(line.withTrimmedRight(ui::Theme::current().shell.itemGap));
                }
            };
    }
}

// ---- Export --------------------------------------------------------------------------------

void SettingsView::addExport()
{
    using F = model::ExportFormat;
    static constexpr F formats[] = {F::Wav, F::Flac, F::Aiff};
    const model::ExportSettings defaults;
    {
        auto& r = addRow("export.format", SettingsSection::Export);
        auto& c = combo(r);
        for (int i = 0; i < 3; ++i)
            c.addItem(withDefault(text(model::formatName(formats[i])), formats[i] == defaults.format), i + 1);
        r.refresh = [this, &c]
        {
            for (int i = 0; i < 3; ++i)
                if (formats[i] == settings().exportDefaults.format)
                    c.setSelectedId(i + 1, juce::dontSendNotification);
        };
        c.onChange = [this, &c]
        {
            change(
                [&c](model::AppSettings& s)
                {
                    s.exportDefaults.format = formats[std::clamp(c.getSelectedId() - 1, 0, 2)];
                    s.exportDefaults = model::validated(s.exportDefaults);
                });
        };
        r.changed = [this, defaults] { return settings().exportDefaults.format != defaults.format; };
    }
    {
        auto& r = addRow("export.bitDepth", SettingsSection::Export);
        auto& c = combo(r);
        r.refresh = [this, &c, defaults, madeFor = std::optional<F>()]() mutable
        {
            if (madeFor != settings().exportDefaults.format)
            {
                madeFor = settings().exportDefaults.format;
                c.clear(juce::dontSendNotification);
                for (const int d : model::bitDepthsFor(*madeFor))
                    c.addItem(withDefault(juce::String(d) + (d == 32 ? "-bit float" : "-bit"),
                                          d == defaults.bitDepth),
                              d);
            }
            c.setSelectedId(settings().exportDefaults.bitDepth, juce::dontSendNotification);
        };
        c.onChange = [this, &c]
        {
            if (const int d = c.getSelectedId(); d > 0)
                change([d](model::AppSettings& s) { s.exportDefaults.bitDepth = d; });
        };
        r.changed = [this, defaults] { return settings().exportDefaults.bitDepth != defaults.bitDepth; };
    }
    {
        auto& r = addRow("export.sampleRate", SettingsSection::Export);
        auto& c = combo(r);
        c.addItem("The project's (default)", 1);
        for (int i = 0; i < static_cast<int>(std::size(model::kExportRates)); ++i)
            c.addItem(rateText(model::kExportRates[i]), i + 2);
        r.refresh = [this, &c]
        {
            int id = 1;
            for (int i = 0; i < static_cast<int>(std::size(model::kExportRates)); ++i)
                if (model::kExportRates[i] == settings().exportDefaults.sampleRate)
                    id = i + 2;
            c.setSelectedId(id, juce::dontSendNotification);
        };
        c.onChange = [this, &c]
        {
            const int i = c.getSelectedId() - 2;
            change(
                [i](model::AppSettings& s)
                {
                    s.exportDefaults.sampleRate = i >= 0 ? model::kExportRates[static_cast<size_t>(i)] : 0.0;
                });
        };
        r.changed = [this] { return settings().exportDefaults.sampleRate != 0.0; };
    }
    {
        auto& r = addRow("export.dither", SettingsSection::Export);
        auto& b = toggle(r, "On", true);
        b.onClick = [this, &b]
        { change([&b](model::AppSettings& s) { s.exportDefaults.dither = b.getToggleState(); }); };
        r.refresh = [this, &b]
        {
            b.setToggleState(settings().exportDefaults.dither, juce::dontSendNotification);
            b.setButtonText(settings().exportDefaults.dither ? "On" : "Off");
        };
        r.changed = [this, defaults] { return settings().exportDefaults.dither != defaults.dither; };
    }
    {
        using N = model::ExportNormalise;
        static constexpr std::pair<N, const char*> modes[] = {
            {N::Off, "Off"}, {N::Peak, "Peak, to -1 dBTP"}, {N::Loudness, "Loudness"}};
        auto& r = addRow("export.normalise", SettingsSection::Export);
        auto& c = combo(r);
        for (int i = 0; i < 3; ++i)
            c.addItem(withDefault(modes[i].second, modes[i].first == defaults.normalise), i + 1);
        r.refresh = [this, &c]
        {
            for (int i = 0; i < 3; ++i)
                if (modes[i].first == settings().exportDefaults.normalise)
                    c.setSelectedId(i + 1, juce::dontSendNotification);
        };
        c.onChange = [this, &c]
        {
            change([&c](model::AppSettings& s)
                   { s.exportDefaults.normalise = modes[std::clamp(c.getSelectedId() - 1, 0, 2)].first; });
        };
        r.changed = [this, defaults] { return settings().exportDefaults.normalise != defaults.normalise; };
        r.reset = [this] { store_->change([](model::AppSettings& s) { s.exportDefaults = {}; }); };
    }
}

// ---- Keys ----------------------------------------------------------------------------------

void SettingsView::addKeys()
{
    // Every action, grouped by its category, in the registry's order.
    std::vector<juce::String> categories;
    for (const auto& a : actions_.all())
        if (std::find(categories.begin(), categories.end(), a.category) == categories.end())
            categories.push_back(a.category);
    for (const auto& category : categories)
    {
        auto& h = addRow("heading:" + category.toStdString(), SettingsSection::Keys);
        h.heading = true;
        h.name = category;
        for (const auto& a : actions_.all())
        {
            if (a.category != category)
                continue;
            auto& r = addRow("key:" + a.id, SettingsSection::Keys);
            r.name = a.name;
            r.shortcut = true;
            r.place = [&r](juce::Rectangle<int> area)
            {
                r.controls.front()->setBounds(
                    area.removeFromRight(ui::Theme::current().shell.settingsKeyWidth));
            };
            auto& b =
                static_cast<KeyButton&>(*components_.emplace_back(std::make_unique<KeyButton>(*this, a.id)));
            b.setTooltip("Click, then press the new keys (Backspace: none)");
            content_->addAndMakeVisible(b);
            r.controls.push_back(&b);
            const auto defaults = a.keys;
            r.refresh = [this, &r, &b, id = a.id, category, defaults]
            {
                // A changed shortcut gives its default as its caption.
                r.description = !actions_.keysChanged(id) ? juce::String()
                                : defaults.empty()
                                    ? juce::String("Default: none")
                                    : "Default: " + ActionRegistry::shortcutText(defaults.front());
                r.searchText = r.name + " " + category + " " + actions_.shortcut(id);
                b.repaint();
            };
            r.changed = [this, id = a.id] { return actions_.keysChanged(id); };
        }
    }
    // The section's Reset: every shortcut.
    if (!categories.empty())
        for (auto& r : rows_)
            if (r->section == SettingsSection::Keys)
            {
                r->reset = [this] { actions_.resetAllKeys(); };
                break;
            }
}

void SettingsView::startKeyCapture(const std::string& actionId)
{
    capturing_ = actionId;
    conflict_.clear();
    if (isShowing())
        ui::takeFocus(*this); // the keys pressed next come here
    content_->repaint();
}

bool SettingsView::captureKey(const juce::KeyPress& key)
{
    if (capturing_.empty())
        return false;
    if (key == juce::KeyPress::escapeKey) // cancelled
    {
        capturing_.clear();
        conflict_.clear();
        refresh();
        return true;
    }
    if (key == juce::KeyPress::backspaceKey) // none
    {
        actions_.setKeys(capturing_, {});
        capturing_.clear();
        refresh();
        return true;
    }
    if (const auto* other = actions_.actionOn(key); other != nullptr && other->id != capturing_)
    {
        pending_ = key;
        conflict_ = ActionRegistry::shortcutText(key) + " is " + actions_.label(other->id) + "'s.";
        filter();
        return true;
    }
    actions_.setKeys(capturing_, {key});
    capturing_.clear();
    refresh();
    return true;
}

void SettingsView::answerKeyConflict(bool replace)
{
    if (conflict_.isEmpty())
        return;
    if (replace)
        actions_.setKeys(capturing_, {pending_});
    conflict_.clear();
    capturing_.clear();
    refresh();
}

// ---- Display and Assistants ----------------------------------------------------------------

void SettingsView::addDisplay()
{
    {
        auto& r = addRow("display.interfaceSize", SettingsSection::Display);
        auto& c = combo(r);
        c.addItem("Automatic (default)", 1);
        for (const int size : model::kInterfaceSizes)
            c.addItem(juce::String(size) + " %", size + 1);
        r.refresh = [this, &c]
        { c.setSelectedId(settings().display.interfaceSize + 1, juce::dontSendNotification); };
        c.onChange = [this, &c]
        {
            change([&c](model::AppSettings& s)
                   { s.display.interfaceSize = std::max(0, c.getSelectedId() - 1); });
        };
        r.changed = [this] { return settings().display.interfaceSize != 0; };
    }
    if (titleBarOption_)
    {
        auto& r = addRow("display.systemTitleBar", SettingsSection::Display);
        auto& b = toggle(r, "Off", true);
        b.onClick = [this, &b]
        { change([&b](model::AppSettings& s) { s.display.systemTitleBar = b.getToggleState(); }); };
        r.refresh = [this, &b]
        {
            b.setToggleState(settings().display.systemTitleBar, juce::dontSendNotification);
            b.setButtonText(settings().display.systemTitleBar ? "On" : "Off");
        };
        r.changed = [this] { return settings().display.systemTitleBar; };
    }
    rows_.back()->reset = [this] { store_->change([](model::AppSettings& s) { s.display = {}; }); };
}

void SettingsView::addAssistants()
{
    {
        auto& r = addRow("assistants.enabled", SettingsSection::Assistants);
        auto& b = toggle(r, "Off", true);
        b.onClick = [this, &b]
        { change([&b](model::AppSettings& s) { s.assistants.enabled = b.getToggleState(); }); };
        r.refresh = [this, &b]
        {
            b.setToggleState(settings().assistants.enabled, juce::dontSendNotification);
            b.setButtonText(settings().assistants.enabled ? "On" : "Off");
        };
        r.changed = [this] { return settings().assistants.enabled; };
    }
    {
        auto& r = addRow("assistants.readOnly", SettingsSection::Assistants);
        auto& b = toggle(r, "Off", true);
        b.onClick = [this, &b]
        { change([&b](model::AppSettings& s) { s.assistants.readOnly = b.getToggleState(); }); };
        r.refresh = [this, &b]
        {
            b.setToggleState(settings().assistants.readOnly, juce::dontSendNotification);
            b.setButtonText(settings().assistants.readOnly ? "On" : "Off");
        };
        r.changed = [this] { return settings().assistants.readOnly; };
        r.reset = [this]
        {
            store_->change(
                [](model::AppSettings& s)
                {
                    s.assistants.enabled = false;
                    s.assistants.readOnly = false;
                });
        };
    }
}

// ---- search, Changed only, reset -----------------------------------------------------------

void SettingsView::setChangedOnly(bool on)
{
    changedOnly_.setToggleState(on, juce::dontSendNotification);
    filter();
}

void SettingsView::filter()
{
    const auto query = search_.getText().trim().toStdString();
    const bool changedOnly = changedOnly_.getToggleState();
    const auto* keys = model::findSetting("keys");
    for (auto& r : rows_)
    {
        if (r->heading)
            continue;
        bool match = true;
        if (!query.empty())
        {
            if (const auto* info = model::findSetting(r->id))
                match = model::settingMatches(*info, query);
            else // a shortcut: its action, its category, its keys, or the section itself
                match = model::textMatches(r->searchText.toStdString(), query) ||
                        (keys != nullptr && model::settingMatches(*keys, query));
        }
        r->shown = match && (!changedOnly || (r->changed && r->changed()));
    }
    // A heading shows while a row under it does.
    for (size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i]->heading)
        {
            bool any = false;
            for (size_t j = i + 1;
                 j < rows_.size() && !rows_[j]->heading && rows_[j]->section == rows_[i]->section; ++j)
                any = any || rows_[j]->shown;
            rows_[i]->shown = any;
        }
    shownSections_.clear();
    for (const auto section : model::kSettingsSections)
        if (std::any_of(rows_.begin(), rows_.end(),
                        [&](const auto& r) { return r->section == section && r->shown; }))
            shownSections_.push_back(section);
    layoutContent();
    index_->repaint();
    content_->repaint();
    repaint(); // the empty state
}

std::vector<std::string> SettingsView::shownSettings() const
{
    std::vector<std::string> out;
    for (const auto& r : rows_)
        if (r->shown && !r->heading)
            out.push_back(r->id);
    return out;
}

bool SettingsView::resetShown(SettingsSection section) const
{
    const auto it = resets_.find(section);
    return it != resets_.end() && it->second->isVisible();
}

void SettingsView::resetSection(SettingsSection section)
{
    for (auto& r : rows_)
        if (r->section == section && r->reset)
            r->reset();
    refresh();
}

juce::Rectangle<int> SettingsView::rowBounds(const std::string& id) const
{
    for (const auto& r : rows_)
        if (r->id == id && r->shown)
            return r->bounds;
    return {};
}

juce::Component& SettingsView::page() noexcept
{
    return *content_;
}

juce::Rectangle<int> SettingsView::visibleArea() const
{
    return viewport_.getViewArea();
}

std::vector<juce::Component*> SettingsView::controlsOf(const std::string& id) const
{
    for (const auto& r : rows_)
        if (r->id == id)
            return r->controls;
    return {};
}

void SettingsView::showSetting(const std::string& id)
{
    search_.setText({}, false);
    changedOnly_.setToggleState(false, juce::dontSendNotification);
    filter();
    const auto& s = ui::Theme::current().shell;
    for (const auto& r : rows_)
        if (r->id == id)
        {
            // A section's first setting shows under its title.
            auto top = r->bounds.getY() - s.groupGap;
            for (size_t i = 0; i < shownSections_.size(); ++i)
                if (shownSections_[i] == r->section && std::find_if(
                                                           rows_.begin(), rows_.end(),
                                                           [&r](const auto& o)
                                                           {
                                                               return o->section == r->section;
                                                           })->get() == r.get())
                    top = stack_.titleArea(i).getY();
            viewport_.setViewPosition(0, std::max(0, top));
            flash_ = id;
            flashUntil_ = juce::Time::getMillisecondCounterHiRes() * 0.001 + 1.5;
            content_->repaint();
            return;
        }
    // A setting that is a whole section (Keys): its title.
    if (const auto* info = model::findSetting(id))
        for (size_t i = 0; i < shownSections_.size(); ++i)
            if (shownSections_[i] == info->section)
                viewport_.setViewPosition(0, stack_.titleArea(i).getY());
}

// ---- layout --------------------------------------------------------------------------------

bool SettingsView::sectionChanged(SettingsSection section) const
{
    return std::any_of(rows_.begin(), rows_.end(), [section](const auto& r)
                       { return r->section == section && r->changed && r->changed(); });
}

void SettingsView::layoutContent()
{
    const auto& s = ui::Theme::current().shell;
    const int width = std::max(1, viewport_.getMaximumVisibleWidth() - s.padding);
    // A row's height, and the line for a conflict under the shortcut being changed.
    const auto heightOf = [this, &s](const Row& r)
    {
        return r.height() +
               (r.id == "key:" + capturing_ && conflict_.isNotEmpty() ? s.itemGap + s.fieldHeight : 0);
    };
    // Between rows: a group gap; between shortcuts, and under their heading, a list's item gap.
    const auto gapBefore = [&s](const Row* previous, const Row& r)
    {
        if (previous == nullptr)
            return 0;
        return (previous->shortcut || previous->heading) && r.shortcut ? s.itemGap : s.groupGap;
    };
    std::vector<ui::SectionStack::Section> sections;
    for (const auto section : shownSections_)
    {
        int h = 0;
        const Row* previous = nullptr;
        for (const auto& r : rows_)
            if (r->section == section && r->shown)
            {
                h += gapBefore(previous, *r) + heightOf(*r);
                previous = r.get();
            }
        sections.push_back({juce::String(model::sectionName(section)), h});
    }
    const auto rest = stack_.layout({0, 0, width, 1 << 20}, sections);
    // What shows, set once at the end: hiding a control even for a moment would end typing in it.
    std::set<juce::Component*> shown;
    for (size_t i = 0; i < shownSections_.size(); ++i)
    {
        const auto section = shownSections_[i];
        auto area = stack_.content(i);
        const Row* previous = nullptr;
        for (auto& r : rows_)
        {
            if (r->section != section || !r->shown)
                continue;
            area.removeFromTop(gapBefore(previous, *r));
            previous = r.get();
            r->bounds = area.removeFromTop(heightOf(*r));
            if (r->heading)
                continue;
            const auto controlArea =
                r->bounds.withTrimmedLeft(s.settingsLabelWidth)
                    .withWidth(std::min(s.settingsControlWidth, r->bounds.getWidth() - s.settingsLabelWidth))
                    .withHeight(r->lines * s.fieldHeight + (r->lines - 1) * s.itemGap);
            shown.insert(r->controls.begin(), r->controls.end());
            if (r->place)
                r->place(controlArea);
            else
            {
                // Left to right: buttons as wide as their labels, a number as its widest value, a
                // readout the room that's left, a choice the whole width.
                const auto fixedWidth = [](juce::Component* c)
                {
                    if (auto* b =
                            dynamic_cast<ui::Toggle*>(c)) // an on/off switch keeps its width as it flips
                        return ui::Toggle::widthFor(b->getButtonText() == "On" ? juce::String("Off")
                                                                               : b->getButtonText());
                    if (auto* n = dynamic_cast<ui::NumberField*>(c))
                        return std::max(n->widthFor("-500 ms"), n->widthFor("0 ms (default)"));
                    return 0;
                };
                int fixed = 0, readouts = 0;
                for (auto* c : r->controls)
                {
                    fixed += fixedWidth(c);
                    readouts += dynamic_cast<Readout*>(c) != nullptr ? 1 : 0;
                }
                const int gaps = s.itemGap * (static_cast<int>(r->controls.size()) - 1);
                auto line = controlArea;
                for (auto* c : r->controls)
                {
                    int w = fixedWidth(c);
                    if (dynamic_cast<Readout*>(c) != nullptr)
                        w = std::max(0, controlArea.getWidth() - fixed - gaps) / std::max(1, readouts);
                    else if (w == 0)
                        w = line.getWidth();
                    c->setBounds(line.removeFromLeft(std::min(w, line.getWidth())));
                    line.removeFromLeft(s.itemGap);
                }
            }
            if (r->id == "key:" + capturing_ && conflict_.isNotEmpty())
            {
                auto answers = controlArea.translated(0, s.fieldHeight + s.itemGap);
                cancel_.setBounds(answers.removeFromRight(ui::Toggle::widthFor(cancel_.getButtonText())));
                answers.removeFromRight(s.itemGap);
                replace_.setBounds(answers.removeFromRight(ui::Toggle::widthFor(replace_.getButtonText())));
                shown.insert(&replace_);
                shown.insert(&cancel_);
            }
        }
        // Reset, at the title row's right, only while something here differs from its defaults.
        auto& reset = *resets_.at(section);
        const auto title = stack_.titleArea(i);
        reset.setBounds(title.withLeft(title.getRight() - ui::Toggle::widthFor(reset.getButtonText())));
        if (sectionChanged(section))
            shown.insert(&reset);
    }
    for (auto& c : components_)
        c->setVisible(shown.contains(c.get()));
    for (auto& [section, reset] : resets_)
        reset->setVisible(shown.contains(reset.get()));
    replace_.setVisible(shown.contains(&replace_));
    cancel_.setVisible(shown.contains(&cancel_));
    content_->setSize(width, std::max(rest.getY(), 1));
}

void SettingsView::paint(juce::Graphics& g)
{
    g.fillAll(ui::Theme::current().surface1);
    if (shownSections_.empty())
        ui::paintEmptyState(g, viewport_.getBounds(), "No setting matches");
}

void SettingsView::resized()
{
    const auto& s = ui::Theme::current().shell;
    auto area = getLocalBounds().reduced(s.padding);
    auto header = area.removeFromTop(s.fieldHeight);
    close_.setBounds(header.removeFromRight(s.fieldHeight));
    header.removeFromRight(s.groupGap);
    area.removeFromTop(s.groupGap);
    index_->setBounds(
        area.removeFromLeft(s.settingsIndexWidth)
            .withHeight(static_cast<int>(std::size(model::kSettingsSections)) * s.listRowHeight));
    area.removeFromLeft(s.groupGap);
    // The column, at most so wide; the search and Changed only span the page in it.
    const auto column = area.withWidth(std::min(area.getWidth(), s.settingsColumnWidth + s.padding));
    viewport_.setBounds(column);
    const int pageRight = column.getX() + std::max(1, viewport_.getMaximumVisibleWidth() - s.padding);
    auto bar = header.withLeft(column.getX())
                   .withRight(std::max(column.getX() + 1, std::min(header.getRight(), pageRight)));
    changedOnly_.setBounds(bar.removeFromRight(changedOnly_.preferredWidth()));
    bar.removeFromRight(s.groupGap);
    search_.setBounds(bar);
    layoutContent();
}

bool SettingsView::keyPressed(const juce::KeyPress& key)
{
    if (captureKey(key))
        return true;
    if (key == juce::KeyPress::escapeKey && onClose)
    {
        onClose();
        return true;
    }
    return false;
}

void SettingsView::textEditorTextChanged(juce::TextEditor&)
{
    filter();
}

void SettingsView::textEditorEscapeKeyPressed(juce::TextEditor&)
{
    if (search_.getText().isNotEmpty())
        search_.setText({}, true);
    else if (onClose)
        onClose();
}

void SettingsView::visibilityChanged()
{
    if (!isVisible())
    {
        stopTimer();
        return;
    }
    xruns_ = session_.deviceManager().getXRunCount(); // dropouts count from here
    refresh();
    startTimer(500);
}

void SettingsView::handleAsyncUpdate()
{
    rebuild();
}

void SettingsView::timerCallback()
{
    if (!isShowing())
        return;
    refresh(); // what changed elsewhere (an assistant, the Record button's menu, a device plugged in)
    if (juce::Time::getMillisecondCounterHiRes() * 0.001 >= flashUntil_ && !flash_.empty())
    {
        flash_.clear();
        content_->repaint();
    }
}
} // namespace fd::app
