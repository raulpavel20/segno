#include "ui/BrowserPanel.h"

#include "ui/BrowserDrops.h"

#include <fd/device/Registry.h>

namespace fd::app
{
namespace
{
using Kind = model::BrowserKind;

juce::String text(const std::string& s)
{
    return juce::String::fromUTF8(s.c_str());
}

std::string str(const juce::File& f)
{
    return f.getFullPathName().toStdString();
}

juce::File fileOf(const std::string& path)
{
    return juce::File(text(path));
}

juce::String lengthText(double seconds)
{
    if (seconds < 60.0)
        return juce::String(seconds, seconds < 10.0 ? 1 : 0) + " s";
    const int s = juce::roundToInt(seconds);
    return juce::String(s / 60) + ":" + juce::String(s % 60).paddedLeft('0', 2);
}

juce::String rateText(double rate)
{
    return juce::String(rate / 1000.0, std::fmod(rate, 1000.0) == 0.0 ? 0 : 1) + " kHz";
}

const juce::String& dot()
{
    static const auto d = juce::String::fromUTF8(" \xC2\xB7 ");
    return d;
}

} // namespace

// ---- the list ------------------------------------------------------------------------------

/// The rows, drawn on the shell's compact row height; only those in view are painted.
class BrowserPanel::List final : public juce::Component, public juce::TooltipClient
{
public:
    explicit List(BrowserPanel& owner) : owner_(owner)
    {
        setWantsKeyboardFocus(true);
        setMouseClickGrabsKeyboardFocus(true);
    }

    [[nodiscard]] int rowAt(int y) const
    {
        const int i = y / ui::Theme::current().shell.rowHeight;
        return juce::isPositiveAndBelow(i, static_cast<int>(owner_.rows_.size())) ? i : -1;
    }

    /// A row as the shared tree row draws it (ui-foundations.md, `TreeRow`).
    [[nodiscard]] ui::TreeItem itemOf(int index) const
    {
        const auto& row = owner_.rows_[static_cast<size_t>(index)];
        ui::TreeItem item;
        item.name = row.name;
        item.caption = row.caption;
        item.depth = row.depth;
        item.flat = row.flat;
        item.mark = row.type == Row::Type::AddPlace ? ui::TreeItem::Mark::Add
                    : row.opens ? (row.open ? ui::TreeItem::Mark::Open : ui::TreeItem::Mark::Closed)
                                : ui::TreeItem::Mark::None;
        item.selected = index == owner_.selected_;
        item.hovered = index == hovered_ && !item.selected;
        item.dim = row.missing || row.type == Row::Type::More;
        item.secondary = row.type == Row::Type::AddPlace;
        item.starrable = !row.item.empty();
        item.starred = item.starrable && owner_.isFavourite(row.item);
        return item;
    }
    [[nodiscard]] juce::Rectangle<int> rowArea(int index) const
    {
        const auto& s = ui::Theme::current().shell;
        return {0, index * s.rowHeight, getWidth(), s.rowHeight};
    }
    [[nodiscard]] juce::Rectangle<int> chevronArea(int index) const
    {
        return ui::treeRowMark(rowArea(index), itemOf(index));
    }
    [[nodiscard]] juce::Rectangle<int> starArea(int index) const { return ui::treeRowStar(rowArea(index)); }

    void paint(juce::Graphics& g) override
    {
        const auto& s = ui::Theme::current().shell;
        if (owner_.rows_.size() == 1 && owner_.rows_.front().type == Row::Type::Empty)
        {
            ui::paintEmptyState(g, getLocalBounds().withHeight(std::max(getHeight(), 4 * s.rowHeight)),
                                owner_.rows_.front().name);
            return;
        }
        const auto clip = g.getClipBounds();
        const int first = std::max(0, clip.getY() / s.rowHeight);
        const int last = std::min(static_cast<int>(owner_.rows_.size()), clip.getBottom() / s.rowHeight + 1);
        for (int i = first; i < last; ++i)
        {
            const auto& row = owner_.rows_[static_cast<size_t>(i)];
            if (row.type == Row::Type::Heading || row.type == Row::Type::Group)
                ui::paintCapsLabel(g, row.name, rowArea(i).withTrimmedLeft(s.listTextInset).toFloat());
            else
                ui::paintTreeRow(g, rowArea(i), itemOf(i));
        }
    }

    /// The whole name, for one cut short.
    juce::String getTooltip() override
    {
        if (!juce::isPositiveAndBelow(hovered_, static_cast<int>(owner_.rows_.size())))
            return {};
        const auto& row = owner_.rows_[static_cast<size_t>(hovered_)];
        if (row.type == Row::Type::Heading || row.type == Row::Type::Group)
            return {};
        // The caption too, when the name took its room.
        const bool captionHidden =
            row.caption.isNotEmpty() && ui::treeRowCaption(rowArea(hovered_), itemOf(hovered_)).isEmpty();
        return captionHidden ? row.name + dot() + row.caption : row.name;
    }

    void mouseMove(const juce::MouseEvent& e) override { setHovered(rowAt(e.y)); }
    void mouseExit(const juce::MouseEvent&) override { setHovered(-1); }

    void mouseDown(const juce::MouseEvent& e) override
    {
        dragging_ = false;
        draggedOut_ = false;
        const int i = rowAt(e.y);
        if (i < 0)
            return;
        const auto row = owner_.rows_[static_cast<size_t>(i)];
        if (e.mods.isPopupMenu())
        {
            owner_.selectIndex(i, false);
            owner_.showMenu(row);
            return;
        }
        if (!row.item.empty() && starArea(i).contains(e.getPosition()))
        {
            owner_.setFavourite(row.item, !owner_.isFavourite(row.item));
            return;
        }
        if (row.type == Row::Type::Heading || row.type == Row::Type::Group || row.type == Row::Type::Empty)
            return;
        if (row.type == Row::Type::AddPlace)
        {
            owner_.addPlace();
            return;
        }
        if (row.type == Row::Type::More)
        {
            owner_.activate(row);
            return;
        }
        owner_.selectIndex(i, true);
        // A folder opens from its name; one of ours (a module) from its disclosure only, since
        // the name is the module.
        const bool folder = row.type == Row::Type::Category || row.type == Row::Type::PresetFolder ||
                            row.type == Row::Type::Maker || row.type == Row::Type::Place ||
                            row.type == Row::Type::Folder;
        if (row.opens && (folder || chevronArea(i).contains(e.getPosition())))
            owner_.toggle(row.id);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        const int i = rowAt(e.getMouseDownY());
        if (i < 0 || dragging_ || e.getDistanceFromDragStart() < ui::Theme::current().input.dragThreshold)
        {
            if (dragging_ && !draggedOut_ &&
                !owner_.getLocalBounds().contains(e.getEventRelativeTo(&owner_).getPosition()))
            {
                draggedOut_ = true;
                if (owner_.onDragOut)
                    owner_.onDragOut();
            }
            return;
        }
        const auto& row = owner_.rows_[static_cast<size_t>(i)];
        if (owner_.dragDescription(row.id).isVoid())
            return;
        dragging_ = true;
        owner_.startDrag(row);
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        if (const int i = rowAt(e.y); i >= 0 && !e.mods.isPopupMenu())
            owner_.activate(owner_.rows_[static_cast<size_t>(i)]);
    }

    bool keyPressed(const juce::KeyPress& key) override { return owner_.keyPressed(key); }

private:
    void setHovered(int i)
    {
        if (i == hovered_)
            return;
        hovered_ = i;
        repaint();
    }

    BrowserPanel& owner_;
    int hovered_ = -1;
    bool dragging_ = false, draggedOut_ = false;
};

// ---- the preview strip ---------------------------------------------------------------------

/// What's selected and how it previews: its name, play and stop, the volume; a sample's waveform
/// with the playhead; what it is.
class BrowserPanel::Strip final : public juce::Component
{
public:
    explicit Strip(BrowserPanel& owner) : owner_(owner)
    {
        play_.setTooltip("Preview (Shift+Enter)");
        play_.onClick = [this]
        {
            if (owner_.session_.isPreviewing())
                owner_.session_.stopPreview();
            else if (juce::isPositiveAndBelow(owner_.selected_, static_cast<int>(owner_.rows_.size())))
                owner_.preview(owner_.rows_[static_cast<size_t>(owner_.selected_)]);
            update();
        };
        volume_.setRange(-60.0, 6.0, 0.5);
        volume_.setDragSensitivity(0.25);
        volume_.setTooltip("The previews' volume");
        volume_.setFontSize(ui::Theme::current().textXs); // a readout's size, as in the inspector
        volume_.setJustification(juce::Justification::centredRight);
        volume_.textFromValue = [](double v) { return volumeText(v); };
        volume_.valueFromText = [](const juce::String& s) -> std::optional<double>
        {
            const auto t =
                s.replace(juce::String::fromUTF8("\xE2\x88\x92"), "-").retainCharacters("+-0123456789.");
            return t.isEmpty() ? std::nullopt : std::optional(t.getDoubleValue());
        };
        volume_.onValueChange = [this](double v)
        { owner_.store_->change([v](model::AppSettings& st) { st.browser.previewVolume = v; }); };
        for (auto* c : {static_cast<juce::Component*>(&play_), static_cast<juce::Component*>(&volume_)})
        {
            c->setWantsKeyboardFocus(false);
            addAndMakeVisible(c);
        }
    }

    /// What the strip shows for the selected row (nothing if it doesn't preview).
    void show(const Row* row)
    {
        row_ = row != nullptr ? std::optional(*row) : std::nullopt;
        if (row_ && row_->kind == Kind::Sample)
            owner_.waveforms_.request(str(row_->file));
        volume_.setValue(owner_.settings().browser.previewVolume, juce::dontSendNotification);
        update();
    }
    [[nodiscard]] bool showsSample() const
    {
        return row_ && row_->type == Row::Type::File && row_->kind == Kind::Sample;
    }
    [[nodiscard]] bool shown() const { return row_.has_value(); }

    void update()
    {
        play_.setIcon(owner_.session_.isPreviewing() ? ui::Icon::Stop : ui::Icon::Play);
        repaint();
    }

    /// The name and the volume; a sample's waveform; play and what it is.
    [[nodiscard]] int preferredHeight() const
    {
        const auto& s = ui::Theme::current().shell;
        return s.padding + s.fieldHeight + s.itemGap + (showsSample() ? s.browserWaveHeight + s.itemGap : 0) +
               s.fieldHeight + s.barPadding;
    }

    void resized() override
    {
        const auto& s = ui::Theme::current().shell;
        auto area = getLocalBounds().withTrimmedTop(s.padding);
        auto top = area.removeFromTop(s.fieldHeight);
        volume_.setBounds(top.removeFromRight(volume_.widthFor(volumeText(-60.0))));
        top.removeFromRight(s.itemGap);
        nameArea_ = top;
        area.removeFromTop(s.itemGap);
        waveArea_ = showsSample() ? area.removeFromTop(s.browserWaveHeight) : juce::Rectangle<int>();
        if (showsSample())
            area.removeFromTop(s.itemGap);
        auto bottom = area.removeFromTop(s.fieldHeight);
        play_.setBounds(bottom.removeFromLeft(s.fieldHeight));
        bottom.removeFromLeft(s.itemGap);
        captionArea_ = bottom;
    }

    static juce::String volumeText(double db)
    {
        return juce::String(db, 1).replace("-", juce::String::fromUTF8("\xE2\x88\x92")) + " dB";
    }

    void paint(juce::Graphics& g) override
    {
        if (!row_)
            return;
        const auto& t = ui::Theme::current();
        g.setColour(t.line);
        g.fillRect(getLocalBounds().removeFromTop(1));
        g.setFont(ui::font(ui::TextStyle::Name));
        g.setColour(t.textPrimary);
        g.drawText(row_->name, nameArea_, juce::Justification::centredLeft, true);
        g.setFont(ui::font(ui::TextStyle::Caption));
        g.setColour(t.textTertiary);
        g.drawText(caption(), captionArea_, juce::Justification::centredLeft, true);
        if (waveArea_.isEmpty())
            return;
        g.setColour(t.surface2);
        g.fillRoundedRectangle(waveArea_.toFloat(), t.radiusControl);
        if (const auto* peaks = owner_.waveforms_.find(str(row_->file));
            peaks != nullptr && peaks->frames > 0)
        {
            g.setColour(t.textSecondary);
            const float mid = static_cast<float>(waveArea_.getCentreY()),
                        half = static_cast<float>(waveArea_.getHeight()) * 0.5f;
            for (int x = 0; x < waveArea_.getWidth(); ++x)
            {
                const auto from = peaks->frames * x / waveArea_.getWidth(),
                           to = peaks->frames * (x + 1) / waveArea_.getWidth();
                const auto [lo, hi] = peaks->range(from, std::max(to, from + 1));
                g.drawVerticalLine(waveArea_.getX() + x, mid - hi * half, mid - lo * half + 1.0f);
            }
        }
        if (owner_.session_.isPreviewing() && owner_.session_.previewLength() > 0.0)
        {
            const double at = std::fmod(owner_.session_.previewPosition(), owner_.session_.previewLength());
            const int x = waveArea_.getX() +
                          juce::roundToInt(at / owner_.session_.previewLength() * waveArea_.getWidth());
            g.setColour(t.roles.playhead);
            g.fillRect(x, waveArea_.getY(), 1, waveArea_.getHeight());
        }
    }

private:
    [[nodiscard]] juce::String caption() const
    {
        if (row_->type == Row::Type::Preset)
            return "A short phrase" + dot() + row_->caption;
        if (row_->kind == Kind::Wavetable)
            return juce::String("On a held C3, through a Wavetable of its own");
        if (row_->kind == Kind::Midi)
            return owner_.selectedTrack && owner_.selectedTrack() &&
                           owner_.session_.instrumentOf(*owner_.selectedTrack())
                       ? juce::String("On the selected track's instrument")
                       : juce::String("Select a track with an instrument to hear it");
        const auto& places = owner_.session_.browserIndex().places;
        for (const auto& [place, files] : places)
            for (const auto& f : files)
                if (f.path == str(row_->file))
                {
                    // What matters most first, so a narrow panel cuts the least of it.
                    auto out = lengthText(f.seconds);
                    if (f.tempo > 0.0)
                        out += dot() + juce::String(juce::roundToInt(f.tempo)) + " BPM";
                    return out + dot() + rateText(f.sampleRate);
                }
        return row_->file.getParentDirectory().getFileName();
    }

    BrowserPanel& owner_;
    std::optional<Row> row_;
    ui::IconButton play_{ui::Icon::Play, "preview"};
    ui::NumberField volume_;
    juce::Rectangle<int> nameArea_, waveArea_, captionArea_;
};

// ---- the panel -----------------------------------------------------------------------------

BrowserPanel::BrowserPanel(engine::Session& session, const ActionRegistry& actions)
    : session_(session), actions_(actions), list_(std::make_unique<List>(*this)),
      strip_(std::make_unique<Strip>(*this))
{
    const auto& t = ui::Theme::current();
    header_.setTitle("Browser");
    header_.addOption(previewOption_);
    previewOption_.setClickingTogglesState(true);
    previewOption_.setToggleState(true, juce::dontSendNotification);
    previewOption_.setTooltip("Play what's selected");
    previewOption_.onClick = [this]
    {
        const bool on = previewOption_.getToggleState();
        store_->change([on](model::AppSettings& s) { s.browser.preview = on; });
        if (!on)
            session_.stopPreview();
        strip_->update();
    };
    addAndMakeVisible(header_);

    search_.setFont(ui::font(ui::TextStyle::Field));
    search_.setTextToShowWhenEmpty("Search", t.textTertiary);
    search_.setIndents(t.shell.listTextInset, 0);
    search_.setJustification(juce::Justification::centredLeft);
    search_.setColour(juce::TextEditor::backgroundColourId, t.surface2);
    search_.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    search_.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    search_.addListener(this);
    addAndMakeVisible(search_);

    viewport_.setViewedComponent(list_.get(), false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);
    addChildComponent(*strip_);

    expand_.setWantsKeyboardFocus(false);
    expand_.onClick = [this] { actions_.perform("view.browser"); };
    addChildComponent(expand_);
    for (const auto& [part, icon, tip] : {std::tuple{Part::Search, ui::Icon::Search, "Search"},
                                          std::tuple{Part::Favourites, ui::Icon::StarFilled, "Favourites"},
                                          std::tuple{Part::Instruments, ui::Icon::Note, "Instruments"},
                                          std::tuple{Part::Effects, ui::Icon::Knob, "Audio effects"},
                                          std::tuple{Part::Plugins, ui::Icon::Plug, "Plugins"},
                                          std::tuple{Part::Places, ui::Icon::Folder, "Places"}})
    {
        auto button = std::make_unique<ui::IconButton>(icon, "browser part");
        button->setTooltip(tip);
        button->setWantsKeyboardFocus(false);
        button->onClick = [this, p = part]
        {
            if (onRailOpen)
                onRailOpen(p);
        };
        addChildComponent(*button);
        railButtons_.emplace_back(part, std::move(button));
    }

    waveforms_.onReady = [this] { strip_->repaint(); };
    session_.onBrowserIndexChanged = [this]
    {
        itemsDirty_ = true;
        listings_.clear();
        rebuildRows();
    };
    setStore(ownStore_);
}

BrowserPanel::~BrowserPanel()
{
    stopTimer();
    session_.onBrowserIndexChanged = nullptr;
    store_->removeListener(storeListener_);
    viewport_.setViewedComponent(nullptr, false);
}

void BrowserPanel::setStore(model::SettingsStore& store)
{
    store_->removeListener(storeListener_);
    store_ = &store;
    storeListener_ = store_->addListener(
        [this](const model::AppSettings& s)
        {
            previewOption_.setToggleState(s.browser.preview, juce::dontSendNotification);
            // What it finds changes with the stars (places change the index, which says so).
            if (s.browser.favourites != favourites_)
            {
                favourites_ = s.browser.favourites;
                itemsDirty_ = true;
            }
            rebuildRows();
        });
    previewOption_.setToggleState(settings().browser.preview, juce::dontSendNotification);
    refresh();
}

void BrowserPanel::refresh()
{
    session_.presetLibrary().rescan();
    listings_.clear();
    itemsDirty_ = true;
    rebuildRows();
}

void BrowserPanel::setRail(bool rail)
{
    rail_ = rail;
    for (auto* c : {static_cast<juce::Component*>(&header_), static_cast<juce::Component*>(&search_),
                    static_cast<juce::Component*>(&viewport_)})
        c->setVisible(!rail);
    strip_->setVisible(!rail && strip_->shown());
    for (auto& [part, b] : railButtons_)
        b->setVisible(rail);
    for (auto& t : tagButtons_)
        t->setVisible(!rail);
    expand_.setVisible(rail);
    expand_.setTooltip(actions_.tooltip("view.browser"));
    resized();
    repaint();
}

void BrowserPanel::setOverlay(bool overlay)
{
    if (overlay_ == overlay)
        return;
    overlay_ = overlay;
    repaint();
}

void BrowserPanel::focusSearch()
{
    ui::takeFocus(search_);
    search_.selectAll();
}

void BrowserPanel::showPart(Part part)
{
    if (part == Part::Search)
    {
        focusSearch();
        return;
    }
    search_.setText({}, false);
    tags_.clear();
    const std::string id = part == Part::Favourites    ? "cat:favourites"
                           : part == Part::Instruments ? "cat:instruments"
                           : part == Part::Effects     ? "cat:effects"
                           : part == Part::Plugins     ? "cat:plugins"
                                                       : "heading:places";
    if (part != Part::Places)
        setOpen(id, true);
    rebuildRows();
    if (const int i = indexOf(id); i >= 0)
    {
        viewport_.setViewPosition(0, i * ui::Theme::current().shell.rowHeight);
        selectIndex(part == Part::Places ? std::min(i + 1, static_cast<int>(rows_.size()) - 1) : i, false);
    }
    ui::takeFocus(*list_);
}

// ---- items and rows ------------------------------------------------------------------------

void BrowserPanel::rebuildItems()
{
    if (!itemsDirty_)
        return;
    itemsDirty_ = false;
    const auto& favs = settings().browser.favourites;
    search_model_.setItems(session_.browserItems({favs.begin(), favs.end()}));
}

bool BrowserPanel::isOpen(const std::string& id) const
{
    const auto& open = settings().browser.open;
    return std::find(open.begin(), open.end(), id) != open.end();
}

void BrowserPanel::setOpen(const std::string& id, bool open)
{
    if (isOpen(id) == open)
        return;
    store_->change(
        [&](model::AppSettings& s)
        {
            auto& list = s.browser.open;
            if (open)
                list.push_back(id);
            else
                std::erase(list, id);
        });
    if (open && id.starts_with("folder:"))
        session_.refreshBrowser(fileOf(id.substr(7))); // look again at what's there
}

bool BrowserPanel::searching() const
{
    return search_.getText().trim().isNotEmpty() || !tags_.empty();
}

void BrowserPanel::rebuildRows()
{
    rebuildItems();
    if (searching())
        buildResults();
    else
        buildTree();
    const auto& s = ui::Theme::current().shell;
    list_->setSize(std::max(1, viewport_.getMaximumVisibleWidth()),
                   std::max(1, static_cast<int>(rows_.size()) * s.rowHeight));
    // The selection stays on its row, wherever it moved.
    selected_ = indexOf(selectedId_);
    layoutTags();
    resized();
    list_->repaint();
}

void BrowserPanel::buildTree()
{
    rows_.clear();
    const auto category = [this](const std::string& id, const juce::String& name, const juce::String& caption)
    {
        Row r;
        r.type = Row::Type::Category;
        r.id = id;
        r.name = name;
        r.caption = caption;
        r.opens = true;
        r.open = isOpen(id);
        rows_.push_back(r);
        return r.open;
    };
    const auto devices = session_.availableDevices();

    // Favourites: whatever you starred, by kind.
    std::vector<const model::BrowserItem*> favourites;
    for (const auto& item : search_model_.items())
        if (item.favourite)
            favourites.push_back(&item);
    if (category("cat:favourites", "Favourites",
                 favourites.empty() ? juce::String() : juce::String(favourites.size())))
    {
        std::stable_sort(favourites.begin(), favourites.end(), [](const auto* a, const auto* b)
                         { return a->kind != b->kind ? a->kind < b->kind : a->name < b->name; });
        for (const auto* item : favourites)
        {
            if (item->kind == Kind::Module)
            {
                if (const auto d = std::find_if(devices.begin(), devices.end(), [&](const auto& dv)
                                                { return "module:" + dv.key == item->id; });
                    d != devices.end())
                    addModuleRows(*d, 1);
                continue;
            }
            Row r;
            r.id = r.item = item->id;
            r.name = text(item->name);
            r.caption = text(item->caption);
            r.depth = 1;
            r.kind = item->kind;
            if (item->kind == Kind::Preset)
                r.type = Row::Type::Preset;
            else if (item->kind == Kind::Plugin)
                r.type = Row::Type::Plugin;
            else if (item->kind == Kind::Folder)
            {
                r.type = Row::Type::Folder;
                r.file = fileOf(item->id.substr(7));
                r.opens = true;
                r.open = isOpen(r.id);
            }
            else
            {
                r.type = Row::Type::File;
                r.file = fileOf(item->id.substr(5));
            }
            rows_.push_back(r);
            if (r.type == Row::Type::Folder && r.open)
                addFolderRows(r.file, 2, false);
        }
    }

    // Our modules, each a folder of its presets (§5).
    if (category("cat:instruments", "Instruments", {}))
        for (const auto& d : devices)
            if (d.native && d.instrument)
                addModuleRows(d, 1);
    if (category("cat:effects", "Audio effects", {}))
        for (const auto& d : devices)
            if (d.native && !d.instrument)
                addModuleRows(d, 1);

    // Plugins, by maker.
    std::map<std::string, std::vector<const engine::AvailableDevice*>> makers;
    for (const auto& d : devices)
        if (!d.native)
            makers[d.category].push_back(&d);
    if (!makers.empty() &&
        category("cat:plugins", "Plugins",
                 juce::String(devices.size() -
                              static_cast<size_t>(std::count_if(devices.begin(), devices.end(),
                                                                [](const auto& d) { return d.native; })))))
        for (const auto& [maker, plugins] : makers)
        {
            Row m;
            m.type = Row::Type::Maker;
            m.id = "maker:" + maker;
            m.name = text(maker);
            m.caption = juce::String(plugins.size());
            m.depth = 1;
            m.opens = true;
            m.open = isOpen(m.id);
            rows_.push_back(m);
            if (m.open)
                for (const auto* p : plugins)
                {
                    Row r;
                    r.type = Row::Type::Plugin;
                    r.id = r.item = "plugin:" + p->key;
                    r.kind = Kind::Plugin;
                    r.name = text(p->name);
                    r.caption = p->instrument ? "Instrument" : "Effect";
                    r.depth = 2;
                    rows_.push_back(r);
                }
        }

    // Places (§9): the project's folder, the Library (ADR 0055), the Segno folder as User library,
    // yours, and Add folder.
    Row heading;
    heading.type = Row::Type::Heading;
    heading.id = "heading:places";
    heading.name = "Places";
    rows_.push_back(heading);
    const auto place = [this](const juce::File& folder, const juce::String& name, bool segno, bool yours,
                              bool library = false)
    {
        Row r;
        r.type = Row::Type::Place;
        r.library = library;
        r.missing = !folder.isDirectory();
        r.id = library && r.missing ? std::string("place:library") : "place:" + str(folder);
        r.item = yours ? "folder:" + str(folder) : std::string();
        r.file = folder;
        r.name = name;
        r.caption = !r.missing ? juce::String()
                    : library  ? juce::String("Not installed")
                               : juce::String("Missing");
        r.opens = !r.missing;
        r.open = r.opens && isOpen(r.id);
        rows_.push_back(r);
        if (r.open)
            addFolderRows(folder, 1, segno);
    };
    if (const auto project = session_.projectFolderOnDisk())
        place(*project, "Project", false, false);
    place(session_.libraryFolder(), "Library", false, false, true);
    place(session_.segnoFolder(), "User library", true, false);
    for (const auto& folder : session_.browserPlaces())
        place(folder, folder.getFileName(), false, true);
    Row add;
    add.type = Row::Type::AddPlace;
    add.id = "addplace";
    add.name = juce::String::fromUTF8("Add folder\xE2\x80\xA6");
    rows_.push_back(add);
}

void BrowserPanel::addModuleRows(const engine::AvailableDevice& device, int depth)
{
    const auto* factory = findDevice(device.key.substr(3));
    if (factory == nullptr)
        return;
    const auto& info = factory->info();
    auto& library = session_.presetLibrary();
    std::vector<model::PresetEntry> entries;
    const auto moduleFolder = juce::File::createLegalFileName(text(std::string(info.name))).toStdString();
    for (const auto* p : library.presetsFor(info))
    {
        if (p->source == Preset::Source::Builtin)
            continue;
        auto folder = p->folder;
        if (folder == moduleFolder)
            folder.clear();
        else if (folder.starts_with(moduleFolder + "/"))
            folder = folder.substr(moduleFolder.size() + 1);
        const bool yours = p->isYours() || p->source == Preset::Source::Default;
        entries.push_back({p->key(),
                           p->source == Preset::Source::Default ? std::string("Your default") : p->name,
                           folder, p->tags, yours});
    }
    Row r;
    r.type = Row::Type::Module;
    r.id = r.item = "module:" + device.key;
    r.kind = Kind::Module;
    r.name = text(device.name);
    r.caption = entries.empty() ? juce::String() : juce::String(entries.size());
    r.depth = depth;
    r.opens = !entries.empty();
    r.open = r.opens && isOpen(r.id);
    rows_.push_back(r);
    if (!r.open)
        return;
    addPresetFolder(model::modulePresetTree(entries, device.instrument), entries, device.key, {}, depth + 1);
}

void BrowserPanel::addPresetFolder(const model::PresetFolder& folder,
                                   const std::vector<model::PresetEntry>& entries,
                                   const std::string& deviceKey, const std::string& path, int depth)
{
    for (const auto& sub : folder.folders)
    {
        Row r;
        r.type = Row::Type::PresetFolder;
        r.id = "pfolder:" + deviceKey + "|" + path + sub.name;
        r.name = text(sub.name);
        std::function<size_t(const model::PresetFolder&)> count = [&count](const model::PresetFolder& f)
        {
            size_t n = f.presets.size();
            for (const auto& s : f.folders)
                n += count(s);
            return n;
        };
        r.caption = juce::String(count(sub));
        r.depth = depth;
        r.opens = true;
        r.open = isOpen(r.id);
        rows_.push_back(r);
        if (r.open)
            addPresetFolder(sub, entries, deviceKey, path + sub.name + "/", depth + 1);
    }
    for (const auto i : folder.presets)
    {
        const auto& e = entries[i];
        Row r;
        r.type = Row::Type::Preset;
        r.id = r.item = "preset:" + deviceKey + "|" + e.key;
        r.kind = Kind::Preset;
        r.name = text(e.name);
        juce::StringArray tags;
        for (const auto& t : e.tags)
            tags.add(text(t));
        r.caption = tags.joinIntoString(", ");
        r.depth = depth;
        rows_.push_back(r);
    }
}

const std::vector<juce::File>& BrowserPanel::listing(const juce::File& folder)
{
    const auto key = str(folder);
    if (const auto it = listings_.find(key); it != listings_.end())
        return it->second;
    std::vector<juce::File> folders, files;
    for (const auto& entry :
         juce::RangedDirectoryIterator(folder, false, "*", juce::File::findFilesAndDirectories))
    {
        const auto& f = entry.getFile();
        if (f.getFileName().startsWithChar('.') || entry.isHidden())
            continue;
        if (entry.isDirectory())
            folders.push_back(f);
        else if (model::fileKind(str(f)))
            files.push_back(f);
    }
    const auto byName = [](const juce::File& a, const juce::File& b)
    { return a.getFileName().compareNatural(b.getFileName()) < 0; };
    std::sort(folders.begin(), folders.end(), byName);
    std::sort(files.begin(), files.end(), byName);
    folders.insert(folders.end(), files.begin(), files.end());
    return listings_[key] = std::move(folders);
}

void BrowserPanel::addFolderRows(const juce::File& folder, int depth, bool segno)
{
    std::map<std::string, const model::IndexedFile*> indexed;
    for (const auto& [place, files] : session_.browserIndex().places)
        for (const auto& f : files)
            indexed[f.path] = &f;
    for (const auto& f : listing(folder))
    {
        Row r;
        r.file = f;
        r.depth = depth;
        if (f.isDirectory())
        {
            // The Segno folder's presets are in their modules; its unsaved projects are the Project.
            if (segno && (f.getFileName() == "Presets" || f.getFileName() == "Unsaved"))
                continue;
            r.type = Row::Type::Folder;
            r.id = r.item = "folder:" + str(f);
            r.kind = Kind::Folder;
            r.name = f.getFileName();
            r.opens = true;
            r.open = isOpen(r.id);
            rows_.push_back(r);
            if (r.open)
                addFolderRows(f, depth + 1, false);
            continue;
        }
        r.type = Row::Type::File;
        r.id = r.item = "file:" + str(f);
        r.kind = *model::fileKind(str(f));
        r.name = f.getFileName();
        if (const auto it = indexed.find(str(f));
            it != indexed.end() && r.kind == Kind::Sample && it->second->seconds > 0.0)
            r.caption = lengthText(it->second->seconds);
        else if (it != indexed.end() && r.kind == Kind::Wavetable && it->second->frames > 0)
            r.caption = juce::String(it->second->frames) + (it->second->frames == 1 ? " frame" : " frames");
        rows_.push_back(r);
    }
}

void BrowserPanel::buildResults()
{
    rows_.clear();
    const auto groups = search_model_.search(search_.getText().trim().toStdString(), tags_);
    if (groups.empty())
    {
        Row r;
        r.type = Row::Type::Empty;
        r.id = "empty";
        r.name = "Nothing matches";
        rows_.push_back(r);
        return;
    }
    constexpr size_t kShown = 8; // a group's best, before "All…" (§6)
    for (const auto& g : groups)
    {
        const auto plural = model::kindPlural(g.kind);
        Row heading;
        heading.type = Row::Type::Group;
        heading.id = "group:" + model::groupTitle(g.kind);
        heading.name = text(model::groupTitle(g.kind));
        rows_.push_back(heading);
        const bool whole = expandedGroups_.contains(plural);
        for (size_t n = 0; n < g.items.size() && (whole || n < kShown); ++n)
        {
            const auto& item = search_model_.items()[g.items[n]];
            Row r;
            r.id = r.item = item.id;
            r.kind = item.kind;
            r.flat = true;
            r.name = text(item.name);
            r.caption = text(item.caption);
            r.type = item.kind == Kind::Module   ? Row::Type::Module
                     : item.kind == Kind::Preset ? Row::Type::Preset
                     : item.kind == Kind::Plugin ? Row::Type::Plugin
                     : item.kind == Kind::Folder ? Row::Type::Folder
                                                 : Row::Type::File;
            if (r.type == Row::Type::File)
                r.file = fileOf(item.id.substr(5));
            if (r.type == Row::Type::Folder)
                r.file = fileOf(item.id.substr(7));
            rows_.push_back(r);
        }
        if (!whole && g.items.size() > kShown)
        {
            Row more;
            more.flat = true;
            more.type = Row::Type::More;
            more.id = "more:" + plural;
            more.name = "All " + juce::String(g.items.size()) + " " + text(plural);
            rows_.push_back(more);
        }
    }
}

void BrowserPanel::layoutTags()
{
    std::vector<std::string> wanted;
    if (searching())
    {
        const auto groups = search_model_.search(search_.getText().trim().toStdString(), tags_);
        wanted = search_model_.tagsOf(groups);
        for (const auto& t : tags_) // the ones chosen stay, to be unchosen
            if (std::find(wanted.begin(), wanted.end(), t) == wanted.end())
                wanted.insert(wanted.begin(), t);
    }
    std::vector<std::string> now;
    for (const auto& b : tagButtons_)
        now.push_back(b->getButtonText().toStdString());
    if (now == wanted)
    {
        for (auto& b : tagButtons_)
            b->setToggleState(tags_.contains(b->getButtonText().toStdString()), juce::dontSendNotification);
        return;
    }
    tagButtons_.clear();
    for (const auto& tag : wanted)
    {
        auto b = std::make_unique<ui::Toggle>(text(tag));
        b->setClickingTogglesState(true);
        b->setWantsKeyboardFocus(false);
        b->setToggleState(tags_.contains(tag), juce::dontSendNotification);
        b->onClick = [this, tag, button = b.get()]
        {
            if (button->getToggleState())
                tags_.insert(tag);
            else
                tags_.erase(tag);
            rebuildRows();
        };
        addChildComponent(*b);
        tagButtons_.push_back(std::move(b));
    }
}

// ---- selection, previews, loading ----------------------------------------------------------

int BrowserPanel::indexOf(const std::string& id) const
{
    for (size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i].id == id)
            return static_cast<int>(i);
    return -1;
}

void BrowserPanel::selectIndex(int index, bool previewIt)
{
    if (!juce::isPositiveAndBelow(index, static_cast<int>(rows_.size())))
        return;
    selected_ = index;
    selectedId_ = rows_[static_cast<size_t>(index)].id;
    const auto& row = rows_[static_cast<size_t>(index)];
    const bool shows = previewable(row);
    strip_->show(shows ? &row : nullptr);
    if (previewIt && shows && previewOption_.getToggleState())
        preview(row);
    else if (previewIt)
        session_.stopPreview();
    strip_->setVisible(!rail_ && shows);
    resized();
    scrollToRow(index);
    list_->repaint();
}

bool BrowserPanel::previewable(const Row& row) const
{
    if (row.type == Row::Type::File)
        return row.kind == Kind::Sample || row.kind == Kind::Wavetable || row.kind == Kind::Midi;
    if (row.type == Row::Type::Preset)
        if (const auto ref = browser::parse(row.item))
            if (const auto* f = findDevice(ref->key.substr(3)))
                return f->info().kind == DeviceKind::Instrument;
    return false;
}

void BrowserPanel::preview(const Row& row)
{
    if (row.type == Row::Type::File && row.kind == Kind::Sample)
        (void)session_.previewFile(row.file);
    else if (row.type == Row::Type::File && row.kind == Kind::Wavetable)
        (void)session_.previewWavetable(row.file); // on a held note (table-editor.md §7)
    else if (row.type == Row::Type::File && row.kind == Kind::Midi)
        (void)session_.previewMidiFile(row.file, selectedTrack ? selectedTrack().value_or(0) : 0);
    else if (const auto ref = browser::parse(row.item); ref && ref->kind == Kind::Preset)
        (void)session_.previewPreset(ref->key.substr(3), ref->preset);
    strip_->update();
    if (session_.isPreviewing())
        startTimerHz(30);
}

void BrowserPanel::activate(const Row& row)
{
    using T = Row::Type;
    if (row.type == T::More)
    {
        expandedGroups_.insert(row.id.substr(5));
        rebuildRows();
        return;
    }
    if (row.type == T::Module || row.type == T::Preset || row.type == T::Plugin)
    {
        if (onLoad)
            onLoad(row.item);
        return;
    }
    if (row.type == T::File)
    {
        if (row.kind == Kind::Project)
        {
            if (onOpenProject)
                onOpenProject(row.file);
        }
        else if (onLoad)
            onLoad(row.item);
        return;
    }
    if (row.type == T::Folder && search_.getText().isNotEmpty())
    {
        // A folder found: shown in its place, open.
        search_.setText({}, false);
        tags_.clear();
        auto places = session_.browserPlaces();
        places.push_back(session_.segnoFolder());
        if (const auto library = session_.libraryFolder(); library != juce::File())
            places.push_back(library);
        if (const auto project = session_.projectFolderOnDisk())
            places.push_back(*project);
        for (const auto& place : places)
            if (row.file == place || row.file.isAChildOf(place))
            {
                setOpen("place:" + str(place), true);
                for (auto f = row.file; f != place && f.isAChildOf(place); f = f.getParentDirectory())
                    setOpen("folder:" + str(f), true);
                break;
            }
        selectedId_ = row.id;
        rebuildRows();
        scrollToRow(selected_);
        return;
    }
    if (row.opens)
        toggle(row.id);
}

void BrowserPanel::loadSelected()
{
    if (juce::isPositiveAndBelow(selected_, static_cast<int>(rows_.size())))
        activate(rows_[static_cast<size_t>(selected_)]);
}

void BrowserPanel::toggle(const std::string& id)
{
    const int i = indexOf(id);
    if (i < 0 || !rows_[static_cast<size_t>(i)].opens)
        return;
    setOpen(id, !rows_[static_cast<size_t>(i)].open);
    rebuildRows();
}

void BrowserPanel::scrollToRow(int index)
{
    const auto& s = ui::Theme::current().shell;
    const int top = index * s.rowHeight, bottom = top + s.rowHeight;
    const auto view = viewport_.getViewArea();
    if (top < view.getY())
        viewport_.setViewPosition(0, top);
    else if (bottom > view.getBottom())
        viewport_.setViewPosition(0, bottom - view.getHeight());
}

bool BrowserPanel::isFavourite(const std::string& item) const
{
    if (const auto ref = browser::parse(item); ref && ref->kind == Kind::Preset)
        if (const auto* f = findDevice(ref->key.substr(3)))
            return session_.presetLibrary().isFavourite(f->info().id, ref->preset);
    const auto& favs = settings().browser.favourites;
    return std::find(favs.begin(), favs.end(), item) != favs.end();
}

void BrowserPanel::setFavourite(const std::string& item, bool favourite)
{
    if (const auto ref = browser::parse(item); ref && ref->kind == Kind::Preset)
    {
        if (const auto* f = findDevice(ref->key.substr(3)))
            session_.presetLibrary().setFavourite(f->info().id, ref->preset, favourite);
        itemsDirty_ = true;
        rebuildRows();
        return;
    }
    store_->change(
        [&](model::AppSettings& s)
        {
            auto& list = s.browser.favourites;
            std::erase(list, item);
            if (favourite)
                list.push_back(item);
        });
}

juce::var BrowserPanel::dragDescription(const std::string& id) const
{
    const int i = indexOf(id);
    if (i < 0)
        return {};
    const auto& row = rows_[static_cast<size_t>(i)];
    const bool puts = row.type == Row::Type::Module || row.type == Row::Type::Preset ||
                      row.type == Row::Type::Plugin ||
                      (row.type == Row::Type::File &&
                       (row.kind == Kind::Sample || row.kind == Kind::Wavetable || row.kind == Kind::Midi));
    return puts ? juce::var(juce::String(std::string(browser::dragPrefix)) + text(row.item)) : juce::var();
}

void BrowserPanel::startDrag(const Row& row)
{
    auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this);
    if (container == nullptr)
        return;
    // The drag image: the row's name on a control's surface.
    const auto& t = ui::Theme::current();
    const auto f = ui::font(ui::TextStyle::Name);
    const float scale = 2.0f;
    const int w = juce::GlyphArrangement::getStringWidthInt(f, row.name) + 2 * t.shell.listTextInset;
    const int h = t.shell.rowHeight;
    juce::Image image(juce::Image::ARGB, juce::roundToInt(static_cast<float>(w) * scale),
                      juce::roundToInt(static_cast<float>(h) * scale), true);
    {
        juce::Graphics g(image);
        g.addTransform(juce::AffineTransform::scale(scale));
        g.setColour(t.surface3);
        g.fillRoundedRectangle(juce::Rectangle<float>(static_cast<float>(w), static_cast<float>(h)),
                               t.radiusControl);
        g.setColour(t.textPrimary);
        g.setFont(f);
        g.drawText(row.name, juce::Rectangle<int>(w, h).reduced(t.shell.listTextInset, 0),
                   juce::Justification::centredLeft, true);
    }
    session_.stopPreview();
    container->startDragging(dragDescription(row.id), list_.get(), juce::ScaledImage(image, scale));
}

void BrowserPanel::showMenu(const Row& row)
{
    juce::PopupMenu menu;
    const bool puts = !dragDescription(row.id).isVoid();
    if (puts)
        menu.addItem("Put on the selected track",
                     [this, item = row.item]
                     {
                         if (onLoad)
                             onLoad(item);
                     });
    if (previewable(row))
        menu.addItem("Preview", [this, row] { preview(row); });
    if (!row.item.empty())
    {
        const bool fav = isFavourite(row.item);
        menu.addItem(fav ? "Remove from favourites" : "Add to favourites",
                     [this, item = row.item, fav] { setFavourite(item, !fav); });
    }
    if (row.file != juce::File() && row.file.exists())
        menu.addItem("Show in folder", [file = row.file] { file.revealToUser(); });
    if (row.library && row.missing)
    {
        // Not found where it's installed: pointed to as Settings > Library does.
        menu.addSeparator();
        menu.addItem(juce::String::fromUTF8("Locate\xE2\x80\xA6"),
                     [this]
                     {
                         chooser_ = std::make_unique<juce::FileChooser>(
                             "Where is Segno's Library?", session_.segnoFolder().getParentDirectory());
                         chooser_->launchAsync(juce::FileBrowserComponent::openMode |
                                                   juce::FileBrowserComponent::canSelectDirectories,
                                               [this](const juce::FileChooser& fc)
                                               {
                                                   if (const auto f = fc.getResult(); f.isDirectory())
                                                       store_->change([&](model::AppSettings& s)
                                                                      { s.library.location = str(f); });
                                               });
                     });
    }
    if (row.type == Row::Type::Place && !row.item.empty())
    {
        menu.addSeparator();
        if (row.missing)
            menu.addItem(juce::String::fromUTF8("Locate\xE2\x80\xA6"),
                         [this, row]
                         {
                             chooser_ = std::make_unique<juce::FileChooser>("Where is " + row.name + "?",
                                                                            row.file.getParentDirectory());
                             chooser_->launchAsync(juce::FileBrowserComponent::openMode |
                                                       juce::FileBrowserComponent::canSelectDirectories,
                                                   [this, old = str(row.file)](const juce::FileChooser& fc)
                                                   {
                                                       if (const auto f = fc.getResult(); f != juce::File())
                                                           store_->change(
                                                               [&](model::AppSettings& s)
                                                               {
                                                                   std::replace(s.browser.places.begin(),
                                                                                s.browser.places.end(), old,
                                                                                str(f));
                                                               });
                                                   });
                         });
        menu.addItem("Remove from places", [this, path = str(row.file)]
                     { store_->change([&](model::AppSettings& s) { std::erase(s.browser.places, path); }); });
    }
    if (menu.getNumItems() > 0)
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(list_.get()).withMousePosition());
}

void BrowserPanel::addPlace()
{
    chooser_ = std::make_unique<juce::FileChooser>("A folder for the browser", session_.segnoFolder());
    chooser_->launchAsync(juce::FileBrowserComponent::openMode |
                              juce::FileBrowserComponent::canSelectDirectories,
                          [this](const juce::FileChooser& fc)
                          {
                              if (const auto f = fc.getResult(); f.isDirectory())
                                  store_->change(
                                      [&](model::AppSettings& s)
                                      {
                                          if (std::find(s.browser.places.begin(), s.browser.places.end(),
                                                        str(f)) == s.browser.places.end())
                                              s.browser.places.push_back(str(f));
                                      });
                          });
}

// ---- keys, search, timer -------------------------------------------------------------------

bool BrowserPanel::keyPressed(const juce::KeyPress& key)
{
    const auto step = [this](int direction)
    {
        for (int i = selected_ + direction; juce::isPositiveAndBelow(i, static_cast<int>(rows_.size()));
             i += direction)
        {
            const auto type = rows_[static_cast<size_t>(i)].type;
            if (type != Row::Type::Heading && type != Row::Type::Group && type != Row::Type::Empty)
            {
                selectIndex(i, true);
                return;
            }
        }
    };
    const auto* row = juce::isPositiveAndBelow(selected_, static_cast<int>(rows_.size()))
                          ? &rows_[static_cast<size_t>(selected_)]
                          : nullptr;
    if (key == juce::KeyPress::downKey)
    {
        step(1);
        return true;
    }
    if (key == juce::KeyPress::upKey)
    {
        if (selected_ <= 0 || (row != nullptr && indexOf(row->id) == 0))
            ui::takeFocus(search_);
        else
            step(-1);
        return true;
    }
    if (key == juce::KeyPress::rightKey && row != nullptr)
    {
        if (row->opens && !row->open)
            toggle(row->id);
        else if (row->opens)
            step(1);
        else if (previewable(*row))
            preview(*row);
        return true;
    }
    if (key == juce::KeyPress::leftKey && row != nullptr)
    {
        if (row->opens && row->open)
            toggle(row->id);
        else
            for (int i = selected_ - 1; i >= 0; --i)
                if (rows_[static_cast<size_t>(i)].depth < row->depth)
                {
                    selectIndex(i, false);
                    break;
                }
        return true;
    }
    if (key == juce::KeyPress(juce::KeyPress::returnKey, juce::ModifierKeys::shiftModifier, 0) &&
        row != nullptr)
    {
        if (previewable(*row))
            preview(*row);
        return true;
    }
    if (key == juce::KeyPress::returnKey && row != nullptr)
    {
        activate(*row);
        return true;
    }
    if (key == juce::KeyPress::escapeKey)
    {
        if (search_.getText().isNotEmpty() || !tags_.empty())
        {
            search_.setText({}, false);
            tags_.clear();
            rebuildRows();
        }
        else if (onDone)
            onDone();
        return true;
    }
    // Typing goes to the search.
    const auto c = key.getTextCharacter();
    if (c > ' ' && !key.getModifiers().isCommandDown() && !key.getModifiers().isAltDown())
    {
        ui::takeFocus(search_);
        search_.insertTextAtCaret(juce::String::charToString(c));
        return true;
    }
    return false;
}

void BrowserPanel::textEditorTextChanged(juce::TextEditor&)
{
    expandedGroups_.clear();
    rebuildRows();
    if (!rows_.empty() && rows_.front().type != Row::Type::Empty)
        selected_ = -1;
    viewport_.setViewPosition(0, 0);
}

void BrowserPanel::textEditorReturnKeyPressed(juce::TextEditor&)
{
    // Enter in the search: the best result.
    for (size_t i = 0; i < rows_.size(); ++i)
        if (!rows_[i].item.empty())
        {
            selectIndex(static_cast<int>(i), false);
            activate(rows_[i]);
            return;
        }
}

void BrowserPanel::textEditorEscapeKeyPressed(juce::TextEditor&)
{
    (void)keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
}

void BrowserPanel::timerCallback()
{
    strip_->update();
    if (!session_.isPreviewing())
        stopTimer();
}

std::vector<std::string> BrowserPanel::rowIds() const
{
    std::vector<std::string> out;
    for (const auto& r : rows_)
        out.push_back(r.id);
    return out;
}

std::vector<juce::String> BrowserPanel::rowNames() const
{
    std::vector<juce::String> out;
    for (const auto& r : rows_)
        out.push_back(r.name);
    return out;
}

juce::Rectangle<int> BrowserPanel::rowBounds(const std::string& id) const
{
    const int i = indexOf(id);
    if (i < 0)
        return {};
    const auto& s = ui::Theme::current().shell;
    return getLocalArea(list_.get(),
                        juce::Rectangle<int>(0, i * s.rowHeight, list_->getWidth(), s.rowHeight));
}

bool BrowserPanel::select(const std::string& id)
{
    const int i = indexOf(id);
    if (i < 0)
        return false;
    selectIndex(i, true);
    return true;
}

std::string BrowserPanel::selected() const
{
    return juce::isPositiveAndBelow(selected_, static_cast<int>(rows_.size()))
               ? rows_[static_cast<size_t>(selected_)].id
               : std::string();
}

bool BrowserPanel::stripShown() const
{
    return strip_->isVisible();
}

std::vector<std::string> BrowserPanel::tagsShown() const
{
    std::vector<std::string> out;
    for (const auto& b : tagButtons_)
        if (b->isVisible())
            out.push_back(b->getButtonText().toStdString());
    return out;
}

void BrowserPanel::setTags(std::set<std::string> tags)
{
    tags_ = std::move(tags);
    rebuildRows();
}

// ---- layout --------------------------------------------------------------------------------

void BrowserPanel::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    g.fillAll(t.surface1);
    if (overlay_)
    {
        g.setColour(t.line);
        g.fillRect(getLocalBounds().removeFromRight(1));
    }
}

void BrowserPanel::resized()
{
    const auto& s = ui::Theme::current().shell;
    if (rail_)
    {
        auto area = getLocalBounds();
        expand_.setBounds(area.removeFromTop(s.railWidth).withSizeKeepingCentre(s.buttonSize, s.buttonSize));
        for (auto& [part, b] : railButtons_)
        {
            b->setBounds(area.removeFromTop(s.buttonSize).withSizeKeepingCentre(s.buttonSize, s.buttonSize));
            area.removeFromTop(s.itemGap);
        }
        return;
    }
    auto area = getLocalBounds();
    header_.setBounds(area.removeFromTop(s.panelHeaderHeight));
    area.reduce(s.barPadding, 0);
    area.removeFromTop(s.itemGap);
    search_.setBounds(area.removeFromTop(s.fieldHeight));
    area.removeFromTop(s.itemGap);
    // The results' tags: as many as fit on one line.
    if (!tagButtons_.empty())
    {
        auto line = area.removeFromTop(s.rowHeight);
        area.removeFromTop(s.itemGap);
        for (auto& b : tagButtons_)
        {
            const int w = ui::Toggle::widthFor(b->getButtonText());
            b->setVisible(w <= line.getWidth());
            if (w <= line.getWidth())
                b->setBounds(line.removeFromLeft(w));
            line.removeFromLeft(s.itemGap);
        }
    }
    if (strip_->isVisible())
    {
        strip_->setBounds(area.removeFromBottom(strip_->preferredHeight()));
        area.removeFromBottom(s.itemGap);
    }
    viewport_.setBounds(area);
    list_->setSize(std::max(1, viewport_.getMaximumVisibleWidth()), std::max(list_->getHeight(), 1));
}
} // namespace fd::app
