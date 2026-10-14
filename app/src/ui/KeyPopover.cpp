#include "ui/KeyPopover.h"

#include <fd/music/Scales.h>

namespace fd::app
{
namespace
{
juce::String utf8(const std::string& s)
{
    return juce::String::fromUTF8(s.c_str());
}

juce::String utf8(std::string_view s)
{
    return juce::String::fromUTF8(s.data(), static_cast<int>(s.size()));
}

/// Whether a scale answers a search: its name, its other name or its ID, anywhere, any case.
bool matches(const music::Scale& scale, const juce::String& query)
{
    for (auto text : {scale.name, scale.alsoCalled, scale.id})
        if (!text.empty() && utf8(text).containsIgnoreCase(query))
            return true;
    return false;
}
} // namespace

// ---- the list ------------------------------------------------------------------------------

/// The scales under their groups' caps headings (a flat list while searching), the song's ticked;
/// hovering one previews it, a click chooses it.
class KeyPopover::Rows final : public juce::Component
{
public:
    explicit Rows(KeyPopover& popover) : popover_(popover) {}

    struct Row
    {
        std::string scale; // empty: a heading
        juce::String heading;
        juce::Rectangle<int> area;
    };

    void rebuild(const juce::String& query)
    {
        const auto& s = ui::Theme::current().shell;
        rows_.clear();
        int y = 0;
        std::string_view group;
        for (const auto& scale : music::scales())
        {
            if (query.isNotEmpty() && !matches(scale, query))
                continue;
            if (query.isEmpty() && scale.group != group)
            {
                group = scale.group;
                rows_.push_back({{}, utf8(group), {0, y, getWidth(), s.rowHeight}});
                y += s.rowHeight;
            }
            rows_.push_back({std::string(scale.id), {}, {0, y, getWidth(), s.listRowHeight}});
            y += s.listRowHeight;
        }
        if (scaleRows() == 0) // the "nothing found" row
            y = s.listRowHeight;
        height_ = y;
        highlighted_ = query.isNotEmpty() && scaleRows() > 0 ? firstScaleRow() : -1;
        repaint();
    }

    [[nodiscard]] int totalHeight() const noexcept { return height_; }
    [[nodiscard]] std::vector<std::string> scales() const
    {
        std::vector<std::string> out;
        for (const auto& r : rows_)
            if (!r.scale.empty())
                out.push_back(r.scale);
        return out;
    }
    /// The scale of the highlighted row (Return chooses it); empty for none.
    [[nodiscard]] std::string highlighted() const
    {
        return highlighted_ >= 0 ? rows_[static_cast<size_t>(highlighted_)].scale : std::string();
    }
    /// Moves the highlight by `step` scale rows, keeping it in view.
    void moveHighlight(int step)
    {
        std::vector<int> scaleIndices;
        for (int i = 0; i < static_cast<int>(rows_.size()); ++i)
            if (!rows_[static_cast<size_t>(i)].scale.empty())
                scaleIndices.push_back(i);
        if (scaleIndices.empty())
            return;
        const auto at = std::find(scaleIndices.begin(), scaleIndices.end(), highlighted_);
        int next = at == scaleIndices.end() ? (step > 0 ? 0 : static_cast<int>(scaleIndices.size()) - 1)
                                            : static_cast<int>(at - scaleIndices.begin()) + step;
        next = std::clamp(next, 0, static_cast<int>(scaleIndices.size()) - 1);
        highlighted_ = scaleIndices[static_cast<size_t>(next)];
        popover_.previewScale(highlighted());
        if (auto* view = findParentComponentOfClass<juce::Viewport>())
        {
            const auto area = rows_[static_cast<size_t>(highlighted_)].area;
            const int top = view->getViewPositionY(), bottom = top + view->getViewHeight();
            if (area.getY() < top)
                view->setViewPosition(0, area.getY());
            else if (area.getBottom() > bottom)
                view->setViewPosition(0, area.getBottom() - view->getViewHeight());
        }
        repaint();
    }
    /// A scale's row (empty when it isn't shown).
    [[nodiscard]] juce::Rectangle<int> areaOf(const std::string& scale) const
    {
        for (const auto& r : rows_)
            if (r.scale == scale)
                return r.area;
        return {};
    }
    /// The first row top at or below `y`, so a view scrolled to the end starts on a row.
    [[nodiscard]] int rowTopFrom(int y) const
    {
        for (const auto& r : rows_)
            if (r.area.getY() >= y)
                return r.area.getY();
        return rows_.empty() ? 0 : rows_.back().area.getY();
    }

    void resized() override
    {
        for (auto& r : rows_)
            r.area.setWidth(getWidth());
    }

    void paint(juce::Graphics& g) override
    {
        const auto& t = ui::Theme::current();
        const auto clip = g.getClipBounds();
        const auto current = popover_.key();
        for (int i = 0; i < static_cast<int>(rows_.size()); ++i)
        {
            const auto& r = rows_[static_cast<size_t>(i)];
            if (!r.area.intersects(clip))
                continue;
            if (r.scale.empty())
            {
                ui::paintCapsLabel(g, r.heading, r.area.withTrimmedLeft(t.shell.listTextInset).toFloat());
                continue;
            }
            const auto* scale = music::findScale(r.scale);
            ui::paintListRow(g, r.area,
                             {.name = utf8(scale->name),
                              .caption = utf8(scale->alsoCalled),
                              .toggled = current && current->scale == r.scale,
                              .selected = i == highlighted_});
        }
        if (scaleRows() == 0)
            ui::paintListRow(g, {0, 0, getWidth(), t.shell.listRowHeight},
                             {.name = "No scale by that name", .enabled = false});
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        const int i = rowAt(e.getPosition());
        if (i == highlighted_)
            return;
        highlighted_ = i;
        popover_.previewScale(highlighted());
        repaint();
    }
    void mouseExit(const juce::MouseEvent&) override
    {
        highlighted_ = -1;
        popover_.previewScale({});
        repaint();
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (const int i = rowAt(e.getPosition()); i >= 0)
            popover_.chooseScale(rows_[static_cast<size_t>(i)].scale);
    }

private:
    [[nodiscard]] int rowAt(juce::Point<int> p) const
    {
        for (int i = 0; i < static_cast<int>(rows_.size()); ++i)
            if (!rows_[static_cast<size_t>(i)].scale.empty() &&
                rows_[static_cast<size_t>(i)].area.contains(p))
                return i;
        return -1;
    }
    [[nodiscard]] int scaleRows() const
    {
        return static_cast<int>(
            std::count_if(rows_.begin(), rows_.end(), [](const Row& r) { return !r.scale.empty(); }));
    }
    [[nodiscard]] int firstScaleRow() const
    {
        for (int i = 0; i < static_cast<int>(rows_.size()); ++i)
            if (!rows_[static_cast<size_t>(i)].scale.empty())
                return i;
        return -1;
    }

    KeyPopover& popover_;
    std::vector<Row> rows_;
    int height_ = 0, highlighted_ = -1;
};

// ---- the popover ---------------------------------------------------------------------------

KeyPopover::KeyPopover(engine::Session& session)
    : session_(session), rows_(std::make_unique<Rows>(*this)), outside_(*this, [this] { close(); })
{
    const auto& t = ui::Theme::current();
    noKey_.setClickingTogglesState(false);
    noKey_.setWantsKeyboardFocus(false);
    noKey_.setTooltip("The song has no key: the piano roll shows every row alike");
    noKey_.onClick = [this] { chooseKey(std::nullopt); };
    addAndMakeVisible(noKey_);

    keys_.onKey = [this](int pc) { chooseRoot(pc); };
    addAndMakeVisible(keys_);

    search_.setFont(ui::font(ui::TextStyle::Field));
    search_.setTextToShowWhenEmpty("Search scales", t.textTertiary);
    search_.setIndents(t.shell.fieldTextInset, 0);
    search_.setJustification(juce::Justification::centredLeft);
    search_.setColour(juce::TextEditor::backgroundColourId, t.surface2);
    search_.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    search_.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    search_.addListener(this);
    search_.addKeyListener(this);
    addAndMakeVisible(search_);

    viewport_.setViewedComponent(rows_.get(), false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);

    outside_.ignore = [this](const juce::Component& c) { return ignoreClicksOn && ignoreClicksOn(c); };
    setVisible(false);
    setWantsKeyboardFocus(false);
}

KeyPopover::~KeyPopover()
{
    outside_.detach();
}

void KeyPopover::open(juce::Component& parent, juce::Rectangle<int> anchor,
                      const std::vector<std::uint64_t>& clips)
{
    parent_ = &parent;
    anchor_ = anchor;
    if (getParentComponent() != &parent)
        parent.addChildComponent(this);
    if (choice_ == nullptr)
        choice_ = std::make_unique<engine::Session::KeyChoice>(session_);
    session_.updateSnapshotNow();
    key_ = session_.snapshot()->keys.first();
    preview_.clear();
    found_ = session_.detectKeys(clips);
    rebuildFound();
    search_.clear();
    rows_->rebuild({});
    showKey();
    place();
    setVisible(true);
    toFront(false);
    // The song's scale in view.
    viewport_.setViewPosition(0, 0);
    if (key_)
        if (const auto row = rows_->areaOf(key_->scale); row.getBottom() > viewport_.getViewHeight())
            viewport_.setViewPosition(0, rows_->rowTopFrom(row.getBottom() - viewport_.getViewHeight()));
    outside_.attach();
    ui::takeFocus(search_);
    if (onOpenChanged)
        onOpenChanged(true);
}

void KeyPopover::close()
{
    outside_.detach();
    if (!isVisible())
        return;
    setVisible(false);
    choice_.reset(); // the step closes, named after the key it ends on (or none at all)
    if (onOpenChanged)
        onOpenChanged(false);
    if (auto* parent = getParentComponent(); parent != nullptr && parent->isShowing())
        parent->grabKeyboardFocus();
}

void KeyPopover::chooseRoot(int pitchClass)
{
    chooseKey(music::Key{pitchClass, key_ ? key_->scale : std::string("major")});
}

void KeyPopover::chooseScale(const std::string& scaleId)
{
    if (music::findScale(scaleId) == nullptr)
        return;
    chooseKey(music::Key{key_ ? key_->root : 0, scaleId});
}

void KeyPopover::chooseKey(const std::optional<music::Key>& key)
{
    if (!session_.setKey(key))
        return;
    key_ = key;
    showKey();
    rows_->repaint();
}

void KeyPopover::previewScale(const std::string& scaleId)
{
    if (preview_ == scaleId)
        return;
    preview_ = scaleId;
    showKey();
}

std::vector<std::string> KeyPopover::shownScales() const
{
    return rows_->scales();
}

void KeyPopover::setQuery(const juce::String& text)
{
    search_.setText(text, false);
    textEditorTextChanged(search_); // at once: the editor's own notice comes later
}

std::optional<music::Key> KeyPopover::shownKey() const
{
    if (!preview_.empty())
        return music::Key{key_ ? key_->root : 0, preview_};
    return key_;
}

void KeyPopover::showKey()
{
    const auto shown = shownKey();
    keys_.setNotes(shown ? shown->root : -1, shown ? music::notes(*shown) : 0);
    noKey_.setVisible(key_.has_value()); // nothing to remove without a key
    repaint(spelledArea());
}

void KeyPopover::rebuildFound()
{
    foundButtons_.clear();
    for (const auto& c : found_)
    {
        auto b = std::make_unique<ui::Toggle>(utf8(music::keyName(c.key)));
        b->setClickingTogglesState(false);
        b->setWantsKeyboardFocus(false);
        b->setTooltip("Set the key to " + b->getButtonText() + " (it fits the notes " +
                      juce::String(juce::roundToInt(c.fit * 100.0)) + " %)");
        b->onClick = [this, key = c.key] { chooseKey(key); };
        addAndMakeVisible(*b);
        foundButtons_.push_back(std::move(b));
    }
}

void KeyPopover::textEditorTextChanged(juce::TextEditor&)
{
    rows_->rebuild(search_.getText().trim());
    resized();
    viewport_.setViewPosition(0, 0);
}

void KeyPopover::textEditorReturnKeyPressed(juce::TextEditor&)
{
    if (const auto scale = rows_->highlighted(); !scale.empty())
        chooseScale(scale);
}

bool KeyPopover::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    if (key == juce::KeyPress::downKey || key == juce::KeyPress::upKey)
    {
        rows_->moveHighlight(key == juce::KeyPress::downKey ? 1 : -1);
        return true;
    }
    return false;
}

// ---- layout --------------------------------------------------------------------------------

juce::Rectangle<int> KeyPopover::inner() const
{
    return getLocalBounds().reduced(juce::roundToInt(ui::Theme::current().shell.shadowRadius));
}

int KeyPopover::preferredHeight() const
{
    const auto& s = ui::Theme::current().shell;
    int h = 2 * s.padding;
    h += s.rowHeight + s.itemGap;                       // the title
    h += ui::OctaveKeys::preferredSize().y + s.itemGap; // the keys
    h += s.rowHeight;                                   // the spelled notes
    h += s.groupGap + s.fieldHeight + s.itemGap;        // the search
    h += s.presetListRows * s.listRowHeight;            // the list
    if (!found_.empty())
        h += s.groupGap + s.rowHeight; // the keys found
    return h;
}

void KeyPopover::place()
{
    if (parent_ == nullptr)
        return;
    const auto& s = ui::Theme::current().shell;
    const int margin = juce::roundToInt(s.shadowRadius);
    const int width = s.formPopoverWidth;
    const int height =
        std::min(preferredHeight(), parent_->getHeight() - anchor_.getBottom() - 2 * s.itemGap);
    const auto area = parent_->getLocalBounds();
    const int x = std::clamp(anchor_.getX() - s.padding, area.getX() + s.itemGap,
                             std::max(area.getX(), area.getRight() - width - s.itemGap));
    setBounds(juce::Rectangle<int>(x, anchor_.getBottom() + s.itemGap, width, height).expanded(margin));
}

juce::Rectangle<int> KeyPopover::spelledArea() const
{
    const auto& s = ui::Theme::current().shell;
    auto area = inner().reduced(s.padding);
    area.removeFromTop(s.rowHeight + s.itemGap + ui::OctaveKeys::preferredSize().y + s.itemGap);
    return area.removeFromTop(s.rowHeight);
}

juce::Rectangle<int> KeyPopover::foundArea() const
{
    if (found_.empty())
        return {};
    const auto& s = ui::Theme::current().shell;
    return inner().reduced(s.padding).removeFromBottom(s.rowHeight);
}

void KeyPopover::resized()
{
    const auto& s = ui::Theme::current().shell;
    auto area = inner().reduced(s.padding);
    auto title = area.removeFromTop(s.rowHeight);
    noKey_.setBounds(title.removeFromRight(ui::Toggle::widthFor(noKey_.getButtonText())));
    area.removeFromTop(s.itemGap);
    const auto keys = ui::OctaveKeys::preferredSize();
    keys_.setBounds(area.removeFromTop(keys.y).withWidth(keys.x));
    area.removeFromTop(s.itemGap + s.rowHeight); // the spelled notes
    area.removeFromTop(s.groupGap);
    search_.setBounds(area.removeFromTop(s.fieldHeight));
    area.removeFromTop(s.itemGap);

    if (!found_.empty())
    {
        auto row = area.removeFromBottom(s.rowHeight);
        area.removeFromBottom(s.groupGap);
        row.removeFromLeft(juce::roundToInt(std::ceil(juce::GlyphArrangement::getStringWidth(
                               ui::capsLabelFont(), "FROM YOUR NOTES"))) +
                           s.groupGap);
        for (auto& b : foundButtons_)
        {
            const int w = ui::Toggle::widthFor(b->getButtonText());
            b->setVisible(w <= row.getWidth());
            if (b->isVisible())
            {
                b->setBounds(row.removeFromLeft(w));
                row.removeFromLeft(s.itemGap);
            }
            else
                row.setWidth(0); // the rest wait: best first
        }
    }
    viewport_.setBounds(area);
    // Scrolled to the end, the view still starts on a row's top.
    const int view = viewport_.getHeight(), total = rows_->totalHeight();
    const int content = total > view ? rows_->rowTopFrom(total - view) + view : total;
    rows_->setSize(viewport_.getMaximumVisibleWidth(), std::max(content, 1));
}

void KeyPopover::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    const auto& s = t.shell;
    ui::paintPopover(g, inner());
    auto area = inner().reduced(s.padding);
    ui::paintCapsLabel(g, "Key", area.removeFromTop(s.rowHeight).toFloat());

    // The key shown (the hovered scale's while previewing): its name, then its notes spelled.
    auto spelled = spelledArea();
    if (const auto shown = shownKey())
    {
        const auto name = utf8(music::keyName(*shown));
        g.setFont(ui::font(ui::TextStyle::Name));
        g.setColour(preview_.empty() ? t.textPrimary : t.textSecondary);
        const int nameWidth =
            juce::roundToInt(std::ceil(juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), name)));
        g.drawText(name, spelled.removeFromLeft(nameWidth), juce::Justification::centredLeft, false);
        spelled.removeFromLeft(s.groupGap);
        juce::String notes;
        for (const auto& n : music::spelledNotes(*shown))
            notes << (notes.isEmpty() ? "" : "  ") << utf8(n);
        g.setFont(ui::font(ui::TextStyle::Value));
        g.setColour(t.textSecondary);
        g.drawFittedText(notes, spelled, juce::Justification::centredLeft, 1, 1.0f);
    }
    else
    {
        g.setFont(ui::font(ui::TextStyle::Body));
        g.setColour(t.textTertiary);
        g.drawText("No key: choose a root or a scale", spelled, juce::Justification::centredLeft, true);
    }

    // A line between the key and the scales, and above the keys found.
    g.setColour(t.line);
    const int lineY = spelledArea().getBottom() + s.groupGap / 2;
    g.fillRect(area.getX(), lineY, area.getWidth(), 1);
    if (const auto found = foundArea(); !found.isEmpty())
    {
        g.fillRect(found.getX(), found.getY() - s.groupGap / 2 - 1, found.getWidth(), 1);
        ui::paintCapsLabel(g, "From your notes", found.toFloat());
    }
}
} // namespace fd::app
