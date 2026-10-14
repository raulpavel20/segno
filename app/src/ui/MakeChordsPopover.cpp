#include "ui/MakeChordsPopover.h"

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
constexpr int kMostPerRow = 8; // cards side by side before they wrap (§20.3)
constexpr int kMostRows = 3;   // rows shown before they scroll

/// Text widths, measured once: chord names repeat from card to card.
int widthOf(const juce::Font& font, const juce::String& text)
{
    static std::map<juce::String, int> kept;
    const auto key = font.toString() + "|" + text;
    if (const auto it = kept.find(key); it != kept.end())
        return it->second;
    const int w = static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(font, text)));
    kept.emplace(key, w);
    return w;
}

juce::String voicingText(const model::NoteChord& c)
{
    juce::String text = model::voicingName(c.voicing);
    if (c.inversion != music::Inversion::Auto)
        text << juce::String::fromUTF8(" · ") << model::inversionName(c.inversion);
    return text;
}
} // namespace

/// One note's card: its note and place, its chord and its voicing, each with a chevron that opens
/// its menu; hovering it plays its chord.
class MakeChordsPopover::Card final : public juce::Component
{
public:
    Card(MakeChordsPopover& owner, std::size_t index, juce::String label)
        : owner_(owner), index_(index), label_(std::move(label))
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        setWantsKeyboardFocus(false);
    }

    [[nodiscard]] const juce::String& label() const noexcept { return label_; }
    /// Its chord's row and its voicing's row.
    [[nodiscard]] juce::Rectangle<int> row(int n) const
    {
        const auto& s = ui::Theme::current().shell;
        auto area = getLocalBounds().reduced(s.buttonTextPadding, s.itemGap);
        return area.withY(area.getY() + n * s.rowHeight).withHeight(s.rowHeight);
    }

    void paint(juce::Graphics& g) override
    {
        const auto& t = ui::Theme::current();
        g.setColour(t.lift(t.surface2, isMouseOver(true), menuOpen_));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), t.radiusControl);
        ui::paintCapsLabel(g, label_, row(0).toFloat());
        const auto& choice = owner_.choices_[index_];
        const bool leave = choice.mode == model::NoteChord::Mode::Leave;
        paintChoice(g, row(1), owner_.chordText(index_), ui::font(ui::TextStyle::Name),
                    leave ? t.textTertiary : t.textPrimary);
        if (!leave)
            paintChoice(g, row(2), voicingText(choice), ui::font(ui::TextStyle::Control), t.textSecondary);
    }

    void mouseEnter(const juce::MouseEvent&) override
    {
        owner_.hoverCard(index_);
        repaint();
    }
    void mouseExit(const juce::MouseEvent&) override
    {
        owner_.hoverCard(std::nullopt);
        repaint();
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        // The chord's menu from its row (and the label's), the voicing's from its own.
        const bool voicing =
            e.y >= row(2).getY() && owner_.choices_[index_].mode != model::NoteChord::Mode::Leave;
        auto menu = voicing ? owner_.voicingMenu(index_) : owner_.chordMenu(index_);
        menuOpen_ = true;
        repaint();
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                           [safe = juce::Component::SafePointer<Card>(this)](int)
                           {
                               if (safe != nullptr)
                               {
                                   safe->menuOpen_ = false;
                                   safe->repaint();
                               }
                           });
    }

private:
    static void paintChoice(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text,
                            const juce::Font& font, juce::Colour colour)
    {
        constexpr float chevron = 8.0f; // a ViewChoice's
        const int gap = ui::Theme::current().shell.itemGap;
        const int w = std::min(area.getWidth() - gap - static_cast<int>(chevron), widthOf(font, text));
        g.setColour(colour);
        g.setFont(font);
        g.drawText(text, area.withWidth(w), juce::Justification::centredLeft, true);
        const auto c =
            juce::Rectangle<float>(static_cast<float>(area.getX() + w + gap), static_cast<float>(area.getY()),
                                   chevron, static_cast<float>(area.getHeight()))
                .withSizeKeepingCentre(chevron, chevron);
        ui::drawIcon(g, ui::Icon::ChevronDown, c, ui::Theme::current().textSecondary);
    }

    MakeChordsPopover& owner_;
    std::size_t index_;
    juce::String label_;
    bool menuOpen_ = false;
};

MakeChordsPopover::MakeChordsPopover(PianoRoll& roll) : roll_(roll), outside_(*this, [this] { close(); })
{
    juce::StringArray kinds, voicingNames;
    for (auto s : shapes)
        kinds.add(model::chordShapeName(s));
    for (auto v : voicings)
        voicingNames.add(model::voicingName(v));
    allKinds_.setItems(kinds);
    allVoicings_.setItems(voicingNames);
    allKinds_.onChange = [this](int index) { setAllKinds(shapes[static_cast<std::size_t>(index)]); };
    allVoicings_.onChange = [this](int index) { setAllVoicings(voicings[static_cast<std::size_t>(index)]); };
    addAndMakeVisible(allKinds_);
    addAndMakeVisible(allVoicings_);
    apply_.setClickingTogglesState(false);
    apply_.setTooltip("Keep these chords (Return); Escape puts the notes back");
    apply_.onClick = [this] { apply(); };
    addAndMakeVisible(apply_);
    viewport_.setViewedComponent(&holder_, false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);
    setVisible(false);
    setWantsKeyboardFocus(true);
    addKeyListener(this);
}

MakeChordsPopover::~MakeChordsPopover()
{
    outside_.detach();
}

const music::Key* MakeChordsPopover::key() const noexcept
{
    return roll_.key() ? &*roll_.key() : nullptr;
}

juce::String MakeChordsPopover::nameOf(const music::Chord& chord) const
{
    return juce::String::fromUTF8(music::chordName(chord, key()).c_str());
}

void MakeChordsPopover::open(juce::Component& parent, juce::Rectangle<int> anchor)
{
    parent_ = &parent;
    anchor_ = anchor;
    if (getParentComponent() != &parent)
        parent.addChildComponent(this);
    // A card per note, each the key's triad on it, voiced smoothly, to start with (§20.3).
    const auto& notes = roll_.notes();
    order_ = model::notesInTimeOrder(notes, roll_.selection());
    choices_ = model::startingChords(notes, order_, key(), music::ChordShape::Triad);
    for (auto& c : cards_)
        holder_.removeChildComponent(c.get());
    cards_.clear();
    for (std::size_t k = 0; k < order_.size(); ++k)
    {
        const auto& n = notes[order_[k]];
        const auto label = juce::String::fromUTF8(music::noteName(key(), n.pitch).c_str()) +
                           juce::String::fromUTF8(" · ") +
                           juce::String(model::positionText(roll_.songBeat(n.start), roll_.tempo()));
        cards_.push_back(std::make_unique<Card>(*this, k, label));
        holder_.addAndMakeVisible(*cards_.back());
    }
    changed();
    reposition();
    resized(); // the cards are new: laid out even when the size stays the same (9 notes, then 10)
    viewport_.setViewPosition(0, 0);
    setVisible(true);
    toFront(true);
    grabKeyboardFocus(); // Return applies, Escape closes
    outside_.attach();
}

void MakeChordsPopover::apply()
{
    if (!isVisible())
        return;
    roll_.auditionOff();
    roll_.applyEdit(model::makeChords(roll_.notes(), order_, choices_));
    outside_.detach();
    setVisible(false);
}

void MakeChordsPopover::close()
{
    outside_.detach();
    if (!isVisible())
        return;
    setVisible(false);
    roll_.auditionOff();
    roll_.previewEdit(std::nullopt);
    if (auto* parent = getParentComponent(); parent != nullptr && parent->isShowing())
        parent->grabKeyboardFocus();
}

bool MakeChordsPopover::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    if (key == juce::KeyPress::escapeKey)
    {
        close();
        return true;
    }
    if (key == juce::KeyPress::returnKey)
    {
        apply();
        return true;
    }
    return false;
}

void MakeChordsPopover::setChoice(std::size_t card, const model::NoteChord& choice)
{
    if (card >= choices_.size())
        return;
    choices_[card] = choice;
    changed();
}

void MakeChordsPopover::setAllKinds(music::ChordShape shape)
{
    const auto& notes = roll_.notes();
    for (std::size_t k = 0; k < choices_.size(); ++k)
    {
        choices_[k].mode = model::NoteChord::Mode::On;
        choices_[k].chord = music::keyChord(key(), notes[order_[k]].pitch, shape);
    }
    changed();
}

void MakeChordsPopover::setAllVoicings(music::Voicing voicing)
{
    for (auto& c : choices_)
        c.voicing = voicing;
    changed();
}

void MakeChordsPopover::changed()
{
    // The notes in the clip as the cards make them, and the foot showing what every card shares.
    roll_.previewEdit(model::makeChords(roll_.notes(), order_, choices_));
    int kind = -1, voicing = -1;
    if (!choices_.empty())
    {
        const auto& notes = roll_.notes();
        for (std::size_t s = 0; s < shapes.size() && kind < 0; ++s)
        {
            bool all = true;
            for (std::size_t k = 0; k < choices_.size() && all; ++k)
                all = choices_[k].mode == model::NoteChord::Mode::On &&
                      choices_[k].chord == music::keyChord(key(), notes[order_[k]].pitch, shapes[s]);
            if (all)
                kind = static_cast<int>(s);
        }
        for (std::size_t v = 0; v < voicings.size() && voicing < 0; ++v)
            if (std::all_of(choices_.begin(), choices_.end(),
                            [&](const auto& c) { return c.voicing == voicings[v]; }))
                voicing = static_cast<int>(v);
    }
    allKinds_.setSelected(kind);
    allVoicings_.setSelected(voicing);
    for (auto& c : cards_)
        c->repaint();
    repaint();
}

juce::String MakeChordsPopover::chordText(std::size_t card) const
{
    if (card >= choices_.size())
        return {};
    const auto& c = choices_[card];
    if (c.mode == model::NoteChord::Mode::Leave)
        return "No chord";
    return nameOf(c.chord);
}

juce::PopupMenu MakeChordsPopover::chordMenu(std::size_t card)
{
    juce::PopupMenu menu;
    if (card >= choices_.size())
        return menu;
    const int pitch = roll_.notes()[order_[card]].pitch;
    const auto noteName = juce::String::fromUTF8(music::pitchClassName(key(), pitch).c_str());
    const auto current = choices_[card];
    const juce::Component::SafePointer<MakeChordsPopover> safe(this);
    const auto item = [&](const juce::String& text, const model::NoteChord& choice)
    {
        menu.addItem(juce::PopupMenu::Item(text)
                         .setTicked(current.mode == choice.mode && current.chord == choice.chord)
                         .setAction(
                             [safe, card, choice]
                             {
                                 if (safe != nullptr)
                                     safe->setChoice(card, choice);
                             }));
    };
    menu.addSectionHeader("On " + noteName);
    for (auto shape : shapes)
    {
        const auto chord = music::keyChord(key(), pitch, shape);
        item(nameOf(chord), {.mode = model::NoteChord::Mode::On,
                             .chord = chord,
                             .voicing = current.voicing,
                             .inversion = current.inversion});
    }
    if (const auto under = model::chordsUnder(key(), pitch); !under.empty())
    {
        menu.addSectionHeader("Under " + noteName);
        for (const auto& chord : under)
            item(nameOf(chord), {.mode = model::NoteChord::Mode::Under,
                                 .chord = chord,
                                 .voicing = current.voicing,
                                 .inversion = current.inversion});
    }
    menu.addSeparator();
    menu.addItem(juce::PopupMenu::Item("Leave the note")
                     .setTicked(current.mode == model::NoteChord::Mode::Leave)
                     .setAction(
                         [safe, card, current]
                         {
                             if (safe == nullptr)
                                 return;
                             auto choice = current;
                             choice.mode = model::NoteChord::Mode::Leave;
                             safe->setChoice(card, choice);
                         }));
    return menu;
}

juce::PopupMenu MakeChordsPopover::voicingMenu(std::size_t card)
{
    juce::PopupMenu menu;
    if (card >= choices_.size())
        return menu;
    const auto current = choices_[card];
    const juce::Component::SafePointer<MakeChordsPopover> safe(this);
    for (auto v : voicings)
    {
        auto choice = current;
        choice.voicing = v;
        menu.addItem(juce::PopupMenu::Item(model::voicingName(v))
                         .setTicked(current.voicing == v)
                         .setAction(
                             [safe, card, choice]
                             {
                                 if (safe != nullptr)
                                     safe->setChoice(card, choice);
                             }));
    }
    menu.addSeparator();
    for (auto i : inversions)
    {
        auto choice = current;
        choice.inversion = i;
        menu.addItem(juce::PopupMenu::Item(juce::String("Inversion: ") + model::inversionName(i))
                         .setTicked(current.inversion == i)
                         .setAction(
                             [safe, card, choice]
                             {
                                 if (safe != nullptr)
                                     safe->setChoice(card, choice);
                             }));
    }
    return menu;
}

void MakeChordsPopover::hoverCard(std::optional<std::size_t> card)
{
    if (!card || *card >= choices_.size())
    {
        roll_.auditionOff();
        return;
    }
    const auto made = model::madeChords(roll_.notes(), order_, choices_);
    roll_.audition(made[*card]);
}

juce::Component* MakeChordsPopover::card(std::size_t index) const
{
    return index < cards_.size() ? cards_[index].get() : nullptr;
}

// ---- layout --------------------------------------------------------------------------------

juce::Rectangle<int> MakeChordsPopover::inner() const
{
    return getLocalBounds().reduced(juce::roundToInt(ui::Theme::current().shell.shadowRadius));
}

juce::String MakeChordsPopover::title() const
{
    const auto n = static_cast<int>(choices_.size());
    return "Make chords" + juce::String::fromUTF8(" · ") + juce::String(n) + (n == 1 ? " note" : " notes");
}

int MakeChordsPopover::cardWidth() const
{
    // As wide as the widest thing any card can show: its label, a chord from its menu, a voicing.
    const auto& s = ui::Theme::current().shell;
    const auto name = ui::font(ui::TextStyle::Name), control = ui::font(ui::TextStyle::Control);
    constexpr int chevron = 8;
    int content = 0;
    for (const auto& c : cards_)
        content = std::max(content, widthOf(ui::capsLabelFont(), c->label().toUpperCase()));
    std::set<int> pitchClasses;
    for (auto i : order_)
        pitchClasses.insert(roll_.notes()[i].pitch % 12);
    for (int pc : pitchClasses)
    {
        for (auto shape : shapes)
            content = std::max(content, widthOf(name, nameOf(music::keyChord(key(), pc, shape))) + s.itemGap +
                                            chevron);
        for (const auto& chord : model::chordsUnder(key(), pc))
            content = std::max(content, widthOf(name, nameOf(chord)) + s.itemGap + chevron);
    }
    content = std::max(content, widthOf(name, "No chord") + s.itemGap + chevron);
    for (auto v : voicings)
        content = std::max(content, widthOf(control, juce::String(model::voicingName(v)) +
                                                         juce::String::fromUTF8(" · ") +
                                                         model::inversionName(music::Inversion::Second)) +
                                        s.itemGap + chevron);
    return content + 2 * s.buttonTextPadding;
}

int MakeChordsPopover::cardHeight() const
{
    const auto& s = ui::Theme::current().shell;
    return 3 * s.rowHeight + 2 * s.itemGap;
}

int MakeChordsPopover::cardsPerRow() const
{
    return std::max(1, std::min(kMostPerRow, static_cast<int>(cards_.size())));
}

int MakeChordsPopover::cardsHeight() const
{
    const auto& s = ui::Theme::current().shell;
    const int rows = (static_cast<int>(cards_.size()) + cardsPerRow() - 1) / cardsPerRow();
    const int shown = std::min(rows, kMostRows);
    return shown * cardHeight() + std::max(0, shown - 1) * s.itemGap;
}

int MakeChordsPopover::preferredWidth() const
{
    const auto& s = ui::Theme::current().shell;
    const int rows = (static_cast<int>(cards_.size()) + cardsPerRow() - 1) / cardsPerRow();
    int content = cardsPerRow() * cardWidth() + (cardsPerRow() - 1) * s.itemGap +
                  (rows > kMostRows ? viewport_.getScrollBarThickness() + s.itemGap : 0);
    content = std::max(content, widthOf(ui::capsLabelFont(), title().toUpperCase()) + s.groupGap +
                                    ui::Toggle::widthFor(apply_.getButtonText()));
    for (const auto* segments : {&allKinds_, &allVoicings_})
        content = std::max(content, s.propertyLabelWidth + segments->preferredWidth());
    return content + 2 * s.padding;
}

int MakeChordsPopover::preferredHeight() const
{
    const auto& s = ui::Theme::current().shell;
    return 2 * s.padding + s.rowHeight + s.itemGap + cardsHeight() + s.groupGap + 2 * s.rowHeight + s.itemGap;
}

void MakeChordsPopover::reposition()
{
    if (parent_ == nullptr)
        return;
    const auto& s = ui::Theme::current().shell;
    const int margin = juce::roundToInt(s.shadowRadius);
    const int width = preferredWidth(), height = preferredHeight();
    const auto area = parent_->getLocalBounds().reduced(s.itemGap);
    // Beside the notes, under the lane: right of them where it fits, else left (§20.3).
    int x = anchor_.getRight() + s.itemGap;
    if (x + width > area.getRight())
        x = anchor_.getX() - s.itemGap - width;
    x = std::clamp(x, area.getX(), std::max(area.getX(), area.getRight() - width));
    const int y = std::clamp(anchor_.getBottom() + s.itemGap, area.getY(),
                             std::max(area.getY(), area.getBottom() - height));
    setBounds(juce::Rectangle<int>(x, y, width, height).expanded(margin));
}

void MakeChordsPopover::resized()
{
    const auto& s = ui::Theme::current().shell;
    auto area = inner().reduced(s.padding);
    auto titleRow = area.removeFromTop(s.rowHeight);
    apply_.setBounds(titleRow.removeFromRight(ui::Toggle::widthFor(apply_.getButtonText())));
    area.removeFromTop(s.itemGap);
    viewport_.setBounds(area.removeFromTop(cardsHeight()));
    const int w = cardWidth(), h = cardHeight(), perRow = cardsPerRow();
    for (std::size_t k = 0; k < cards_.size(); ++k)
    {
        const int column = static_cast<int>(k) % perRow, row = static_cast<int>(k) / perRow;
        cards_[k]->setBounds(column * (w + s.itemGap), row * (h + s.itemGap), w, h);
    }
    const int rows = (static_cast<int>(cards_.size()) + perRow - 1) / perRow;
    holder_.setSize(perRow * w + (perRow - 1) * s.itemGap, rows * h + std::max(0, rows - 1) * s.itemGap);
    area.removeFromTop(s.groupGap);
    for (auto* segments : {&allKinds_, &allVoicings_})
    {
        segments->setBounds(area.removeFromTop(s.rowHeight)
                                .withTrimmedLeft(s.propertyLabelWidth)
                                .withWidth(segments->preferredWidth()));
        area.removeFromTop(s.itemGap);
    }
}

void MakeChordsPopover::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    const auto& s = t.shell;
    ui::paintPopover(g, inner());
    auto area = inner().reduced(s.padding);
    ui::paintCapsLabel(g, title(), area.removeFromTop(s.rowHeight).toFloat());
    g.setColour(t.line);
    g.fillRect(area.getX(), viewport_.getBottom() + s.groupGap / 2, area.getWidth(), 1);
    g.setFont(ui::font(ui::TextStyle::Body));
    g.setColour(t.textSecondary);
    for (const auto& [label, segments] :
         {std::pair{"All: kind", &allKinds_}, std::pair{"All: voicing", &allVoicings_}})
        g.drawText(label, segments->getBounds().withX(area.getX()).withWidth(s.propertyLabelWidth),
                   juce::Justification::centredLeft, false);
}
} // namespace fd::app
