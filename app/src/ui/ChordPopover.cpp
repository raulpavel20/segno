#include "ui/ChordPopover.h"

#include "model/MusicalTime.h"

namespace fd::app
{
namespace
{
constexpr std::array shapes{music::ChordShape::Triad, music::ChordShape::Seventh, music::ChordShape::Ninth,
                            music::ChordShape::Sus2,  music::ChordShape::Sus4,    music::ChordShape::Add9};
constexpr std::array voicings{music::Voicing::Smooth, music::Voicing::Close, music::Voicing::Open,
                              music::Voicing::Drop2,  music::Voicing::Drop3, music::Voicing::Spread};
constexpr std::array inversions{music::Inversion::Auto, music::Inversion::Root, music::Inversion::First,
                                music::Inversion::Second, music::Inversion::Third};

int textWidth(const juce::Font& font, const juce::String& text)
{
    return static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(font, text)));
}

template <typename E, std::size_t N> int indexOf(const std::array<E, N>& values, E value)
{
    for (std::size_t i = 0; i < N; ++i)
        if (values[i] == value)
            return static_cast<int>(i);
    return 0;
}

const music::Key cMajor{0, "major"};
} // namespace

/// A chord to write: a command's look; hovering it is heard.
class ChordPopover::Chip final : public ui::Toggle
{
public:
    Chip(const juce::String& name, music::Chord chord) : ui::Toggle(name), chord_(chord)
    {
        setClickingTogglesState(false);
        setWantsKeyboardFocus(false);
    }

    std::function<void(bool)> onHover;
    [[nodiscard]] const music::Chord& chord() const noexcept { return chord_; }

    void mouseEnter(const juce::MouseEvent& e) override
    {
        ui::Toggle::mouseEnter(e);
        if (onHover)
            onHover(true);
    }
    void mouseExit(const juce::MouseEvent& e) override
    {
        ui::Toggle::mouseExit(e);
        if (onHover)
            onHover(false);
    }

private:
    music::Chord chord_;
};

ChordPopover::ChordPopover(PianoRoll& roll) : roll_(roll), outside_(*this, [this] { close(); })
{
    juce::StringArray kinds, voicingNames, inversionNames;
    for (auto s : shapes)
        kinds.add(model::chordShapeName(s));
    for (auto v : voicings)
        voicingNames.add(model::voicingName(v));
    for (auto i : inversions)
        inversionNames.add(model::inversionName(i));
    kind_.setItems(kinds);
    voicing_.setItems(voicingNames);
    inversion_.setItems(inversionNames);
    kind_.onChange = [this](int) { styleChanged(true); };
    voicing_.onChange = [this](int) { styleChanged(false); };
    inversion_.onChange = [this](int) { styleChanged(false); };
    addAndMakeVisible(kind_);
    addAndMakeVisible(voicing_);
    addAndMakeVisible(inversion_);
    setVisible(false);
    setWantsKeyboardFocus(true);
    addKeyListener(this);
}

ChordPopover::~ChordPopover()
{
    outside_.detach();
}

void ChordPopover::open(juce::Component& parent, juce::Rectangle<int> anchor,
                        const PianoRoll::ChordPlace& place)
{
    place_ = place;
    roll_.setChordTarget(place);
    show(parent, anchor);
}

void ChordPopover::show(juce::Component& parent, juce::Rectangle<int> anchor)
{
    parent_ = &parent;
    anchor_ = anchor;
    if (getParentComponent() != &parent)
        parent.addChildComponent(this);
    // Its choices start as the chord there is (§20.4): its kind, voiced smoothly; they're its own,
    // not what Draw uses.
    const auto current = place_ ? roll_.chordAt(*place_) : std::nullopt;
    style_ = {.shape = current ? model::shapeOf(*current) : music::ChordShape::Triad};
    kind_.setSelected(indexOf(shapes, style_.shape));
    voicing_.setSelected(indexOf(voicings, style_.voicing));
    inversion_.setSelected(indexOf(inversions, style_.inversion));
    rebuild();
    setVisible(true);
    toFront(true);
    grabKeyboardFocus(); // Escape closes it
    outside_.attach();
}

void ChordPopover::close()
{
    outside_.detach();
    if (!isVisible())
        return;
    setVisible(false);
    roll_.previewChord(place_.value_or(PianoRoll::ChordPlace{}), std::nullopt, style_);
    roll_.setChordTarget(std::nullopt);
    place_.reset();
    if (auto* parent = getParentComponent(); parent != nullptr && parent->isShowing())
        parent->grabKeyboardFocus();
}

bool ChordPopover::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    if (key == juce::KeyPress::escapeKey)
    {
        close();
        return true;
    }
    return false;
}

juce::String ChordPopover::nameOf(const music::Chord& chord) const
{
    const auto& key = roll_.key();
    return juce::String::fromUTF8(music::chordName(chord, key ? &*key : nullptr).c_str());
}

juce::String ChordPopover::title() const
{
    if (!place_)
        return "Chords";
    return "Chord at " + juce::String(model::positionText(roll_.songBeat(place_->start), roll_.tempo()));
}

std::vector<ui::Toggle*> ChordPopover::chordButtons() const
{
    std::vector<ui::Toggle*> out;
    for (const auto& c : chips_)
        out.push_back(c.get());
    return out;
}

void ChordPopover::rebuild()
{
    for (auto& c : chips_)
        removeChildComponent(c.get());
    chips_.clear();
    groups_.clear();
    if (place_)
    {
        const auto shape = style_.shape;
        const auto& songKey = roll_.key();
        const auto& key = songKey ? *songKey : cMajor;
        groups_.push_back({songKey ? "In the key" : "As in C major", music::keyChords(key, shape)});
        if (const auto before = roll_.chordBefore(*place_))
        {
            Group next{"Next, after " + nameOf(*before), {}};
            for (const auto& s : music::nextChords(key, *before, shape))
                next.chords.push_back(s.chord);
            if (!next.chords.empty())
                groups_.push_back(std::move(next));
        }
        if (songKey)
        {
            Group borrowed{"Borrowed", {}};
            for (const auto& s : music::borrowedChords(key, shape))
                borrowed.chords.push_back(s.chord);
            if (!borrowed.chords.empty())
                groups_.push_back(std::move(borrowed));
        }
        // The chord there now is lit, wherever it's offered.
        const auto current = roll_.chordAt(*place_);
        for (const auto& group : groups_)
            for (const auto& chord : group.chords)
            {
                auto chip = std::make_unique<Chip>(nameOf(chord), chord);
                chip->setToggleState(current && current->root == chord.root && current->kind == chord.kind &&
                                         current->tensions == chord.tensions,
                                     juce::dontSendNotification);
                chip->setTooltip(juce::String::fromUTF8(
                    (music::romanNumeral(chord, key) + " · " + std::string(music::kindOf(chord).name))
                        .c_str()));
                chip->onClick = [this, c = chord] { choose(c); };
                chip->onHover = [this, c = chord](bool on)
                { hover(on ? std::optional<music::Chord>(c) : std::nullopt); };
                addAndMakeVisible(*chip);
                chips_.push_back(std::move(chip));
            }
    }
    reposition();
    resized();
    repaint();
}

void ChordPopover::choose(const music::Chord& chord)
{
    if (!place_)
        return;
    place_ = roll_.writeChord(*place_, chord, style_);
    roll_.setChordTarget(place_);
    rebuild(); // the chord there now is lit
}

void ChordPopover::hover(const std::optional<music::Chord>& chord)
{
    if (place_)
        roll_.previewChord(*place_, chord, style_);
}

void ChordPopover::styleChanged(bool kindChanged)
{
    style_ = {.shape = shapes[static_cast<std::size_t>(std::max(0, kind_.selected()))],
              .voicing = voicings[static_cast<std::size_t>(std::max(0, voicing_.selected()))],
              .inversion = inversions[static_cast<std::size_t>(std::max(0, inversion_.selected()))]};
    // The chord there takes it (§20.4): the key's chord of the new kind on its root, or itself
    // re-voiced. Nothing else does.
    if (place_ && !place_->replacing.empty())
        if (const auto current = roll_.chordAt(*place_))
        {
            const auto& key = roll_.key();
            auto chord =
                kindChanged ? music::keyChord(key ? &*key : nullptr, current->root, style_.shape) : *current;
            chord.bass.reset(); // the inversion is the voicing's to choose now
            place_ = roll_.writeChord(*place_, chord, style_);
            roll_.setChordTarget(place_);
        }
    rebuild();
}

// ---- layout --------------------------------------------------------------------------------

juce::Rectangle<int> ChordPopover::inner() const
{
    return getLocalBounds().reduced(juce::roundToInt(ui::Theme::current().shell.shadowRadius));
}

int ChordPopover::preferredWidth() const
{
    // Its fixed content: the titles and the three choices under their labels; the chords wrap
    // within it, so the width stays as the kind changes.
    const auto& s = ui::Theme::current().shell;
    const auto caps = ui::capsLabelFont();
    int content = textWidth(caps, title().toUpperCase());
    for (const auto& g : groups_)
        content = std::max(content, textWidth(caps, g.title.toUpperCase()));
    for (const auto* segments : {&kind_, &voicing_, &inversion_})
        content = std::max(content, s.propertyLabelWidth + segments->preferredWidth());
    return content + 2 * s.padding;
}

int ChordPopover::chipRows(int width, std::vector<int>* rowsPerGroup) const
{
    const auto& s = ui::Theme::current().shell;
    int rows = 0;
    std::size_t chip = 0;
    for (const auto& group : groups_)
    {
        int count = 1, x = 0;
        for (std::size_t k = 0; k < group.chords.size(); ++k, ++chip)
        {
            const int w = ui::Toggle::widthFor(chips_[chip]->getButtonText());
            if (x > 0 && x + w > width)
            {
                ++count;
                x = 0;
            }
            x += w + s.itemGap;
        }
        if (rowsPerGroup != nullptr)
            rowsPerGroup->push_back(count);
        rows += count;
    }
    return rows;
}

int ChordPopover::preferredHeight() const
{
    const auto& s = ui::Theme::current().shell;
    int h = 2 * s.padding + s.rowHeight; // the title
    std::vector<int> rowsPerGroup;
    chipRows(preferredWidth() - 2 * s.padding, &rowsPerGroup);
    for (int rows : rowsPerGroup) // each group: its title, its chips' rows
        h += 2 * s.itemGap + s.rowHeight + rows * s.rowHeight + (rows - 1) * s.itemGap;
    h += groups_.empty() ? s.itemGap : s.groupGap; // the line before the choices
    h += 3 * s.rowHeight + 2 * s.itemGap;          // Kind, Voicing, Inversion
    return h;
}

void ChordPopover::reposition()
{
    if (parent_ == nullptr)
        return;
    const auto& s = ui::Theme::current().shell;
    const int margin = juce::roundToInt(s.shadowRadius);
    const int width = preferredWidth(), height = preferredHeight();
    const auto area = parent_->getLocalBounds().reduced(s.itemGap);
    int x = anchor_.getX();
    if (place_)
    {
        // Beside the place and the chord before it, under the lane, so both stay in view: right of
        // them where it fits, else left.
        x = anchor_.getRight() + s.itemGap;
        if (x + width > area.getRight())
            x = anchor_.getX() - s.itemGap - width;
    }
    x = std::clamp(x, area.getX(), std::max(area.getX(), area.getRight() - width));
    const int y = std::clamp(anchor_.getBottom() + s.itemGap, area.getY(),
                             std::max(area.getY(), area.getBottom() - height));
    setBounds(juce::Rectangle<int>(x, y, width, height).expanded(margin));
}

void ChordPopover::resized()
{
    const auto& s = ui::Theme::current().shell;
    auto area = inner().reduced(s.padding);
    area.removeFromTop(s.rowHeight); // the title
    std::vector<int> rowsPerGroup;
    chipRows(area.getWidth(), &rowsPerGroup);
    std::size_t chip = 0;
    for (std::size_t g = 0; g < groups_.size(); ++g)
    {
        area.removeFromTop(s.itemGap + s.rowHeight); // its title
        area.removeFromTop(s.itemGap);
        auto row = area.removeFromTop(s.rowHeight);
        int x = row.getX();
        for (std::size_t k = 0; k < groups_[g].chords.size(); ++k, ++chip)
        {
            const int w = ui::Toggle::widthFor(chips_[chip]->getButtonText());
            if (x > row.getX() && x + w > row.getRight())
            {
                area.removeFromTop(s.itemGap);
                row = area.removeFromTop(s.rowHeight);
                x = row.getX();
            }
            chips_[chip]->setBounds(x, row.getY(), w, s.rowHeight);
            x += w + s.itemGap;
        }
    }
    area.removeFromTop(groups_.empty() ? s.itemGap : s.groupGap);
    for (auto* segments : {&kind_, &voicing_, &inversion_})
    {
        auto row = area.removeFromTop(s.rowHeight);
        segments->setBounds(row.withTrimmedLeft(s.propertyLabelWidth).withWidth(segments->preferredWidth()));
        area.removeFromTop(s.itemGap);
    }
}

void ChordPopover::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    const auto& s = t.shell;
    ui::paintPopover(g, inner());
    auto area = inner().reduced(s.padding);
    ui::paintCapsLabel(g, title(), area.removeFromTop(s.rowHeight).toFloat());
    std::size_t chip = 0;
    for (const auto& group : groups_)
    {
        area.removeFromTop(s.itemGap);
        ui::paintCapsLabel(g, group.title, area.removeFromTop(s.rowHeight).toFloat());
        // Down to the bottom of its last chip.
        int bottom = area.getY();
        for (std::size_t k = 0; k < group.chords.size(); ++k, ++chip)
            bottom = std::max(bottom, chips_[chip]->getBottom());
        area.setTop(bottom);
    }
    if (!groups_.empty())
    {
        g.setColour(t.line);
        g.fillRect(area.getX(), area.getY() + s.groupGap / 2, area.getWidth(), 1);
    }
    g.setFont(ui::font(ui::TextStyle::Body));
    g.setColour(t.textSecondary);
    for (const auto& [label, segments] :
         {std::pair{"Kind", &kind_}, std::pair{"Voicing", &voicing_}, std::pair{"Inversion", &inversion_}})
        g.drawText(label, segments->getBounds().withX(area.getX()).withWidth(s.propertyLabelWidth),
                   juce::Justification::centredLeft, false);
}
} // namespace fd::app
