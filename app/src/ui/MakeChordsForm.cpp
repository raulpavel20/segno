#include "ui/MakeChordsForm.h"

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

/// Text widths, measured once: chord names repeat from row to row.
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

constexpr float kChevron = 8.0f; // a ViewChoice's
} // namespace

/// One row: a note's label and place, its chord and its voicing, each with a chevron that opens
/// its menu; hovering it plays its chord. The All row (no index) opens the kinds and voicings for
/// every note.
class MakeChordsForm::Row final : public juce::Component, public juce::SettableTooltipClient
{
public:
    Row(MakeChordsForm& owner, std::optional<std::size_t> index, juce::String label)
        : owner_(owner), index_(index), label_(std::move(label))
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        setWantsKeyboardFocus(false);
    }

    [[nodiscard]] const juce::String& label() const noexcept { return label_; }

    void paint(juce::Graphics& g) override
    {
        const auto& t = ui::Theme::current();
        const auto& s = t.shell;
        g.setColour(t.lift(t.surface2, isMouseOver(true), menuOpen_));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), t.radiusControl);
        auto area = getLocalBounds().reduced(s.buttonTextPadding, 0);
        ui::paintCapsLabel(g, label_, area.removeFromLeft(owner_.labelWidth()).toFloat());
        area.removeFromLeft(s.groupGap); // the note, then what it becomes
        const auto chordArea = area.removeFromLeft(owner_.chordWidth());
        area.removeFromLeft(s.itemGap);
        if (!index_)
        {
            const auto kind = owner_.allKind();
            const auto voicing = owner_.allVoicing();
            paintChoice(g, chordArea, kind ? juce::String(model::chordShapeName(*kind)) : "Mixed",
                        ui::font(ui::TextStyle::Control), kind ? t.textPrimary : t.textTertiary);
            paintChoice(g, area, voicing ? juce::String(model::voicingName(*voicing)) : "Mixed",
                        ui::font(ui::TextStyle::Control), voicing ? t.textSecondary : t.textTertiary);
            return;
        }
        const auto& choice = owner_.choices_[*index_];
        const bool leave = choice.mode == model::NoteChord::Mode::Leave;
        paintChoice(g, chordArea, owner_.chordText(*index_), ui::font(ui::TextStyle::Name),
                    leave ? t.textTertiary : t.textPrimary);
        if (!leave)
            paintChoice(g, area, voicingText(choice), ui::font(ui::TextStyle::Control), t.textSecondary);
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
        // The voicing's menu right of the chord's column, the chord's from the rest.
        const auto& s = ui::Theme::current().shell;
        const int voicingFrom =
            s.buttonTextPadding + owner_.labelWidth() + s.groupGap + owner_.chordWidth() + s.itemGap;
        const bool leave = index_ && owner_.choices_[*index_].mode == model::NoteChord::Mode::Leave;
        const bool voicing = e.x >= voicingFrom && !leave;
        auto menu = !index_ ? (voicing ? owner_.allVoicingsMenu() : owner_.allKindsMenu())
                            : (voicing ? owner_.voicingMenu(*index_) : owner_.chordMenu(*index_));
        menuOpen_ = true;
        repaint();
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                           [safe = juce::Component::SafePointer<Row>(this)](int)
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
        const int gap = ui::Theme::current().shell.itemGap;
        const int w = std::min(area.getWidth() - gap - static_cast<int>(kChevron), widthOf(font, text));
        g.setColour(colour);
        g.setFont(font);
        g.drawText(text, area.withWidth(w), juce::Justification::centredLeft, true);
        const auto c =
            juce::Rectangle<float>(static_cast<float>(area.getX() + w + gap), static_cast<float>(area.getY()),
                                   kChevron, static_cast<float>(area.getHeight()))
                .withSizeKeepingCentre(kChevron, kChevron);
        ui::drawIcon(g, ui::Icon::ChevronDown, c, ui::Theme::current().textSecondary);
    }

    MakeChordsForm& owner_;
    std::optional<std::size_t> index_;
    juce::String label_;
    bool menuOpen_ = false;
};

MakeChordsForm::MakeChordsForm(PianoRoll& roll) : roll_(roll)
{
    apply_.setClickingTogglesState(false);
    apply_.setTooltip("Keep these chords (Return); Escape puts the notes back");
    apply_.onClick = [this] { apply(); };
    addAndMakeVisible(apply_);
    all_ = std::make_unique<Row>(*this, std::nullopt, "All");
    all_->setTooltip("Every note's chord kind, and its voicing");
    addAndMakeVisible(*all_);
    viewport_.setViewedComponent(&holder_, false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);
}

MakeChordsForm::~MakeChordsForm()
{
    roll_.auditionOff();
}

const music::Key* MakeChordsForm::key() const noexcept
{
    return roll_.key() ? &*roll_.key() : nullptr;
}

juce::String MakeChordsForm::nameOf(const music::Chord& chord) const
{
    return juce::String::fromUTF8(music::chordName(chord, key()).c_str());
}

void MakeChordsForm::start()
{
    // A row per note, each the key's triad on it, voiced smoothly, to start with (§20.3).
    const auto& notes = roll_.notes();
    order_ = model::notesInTimeOrder(notes, roll_.selection());
    choices_ = model::startingChords(notes, order_, key(), music::ChordShape::Triad);
    for (auto& r : rows_)
        holder_.removeChildComponent(r.get());
    rows_.clear();
    for (std::size_t k = 0; k < order_.size(); ++k)
    {
        const auto& n = notes[order_[k]];
        const auto label = juce::String::fromUTF8(music::noteName(key(), n.pitch).c_str()) +
                           juce::String::fromUTF8(" · ") +
                           juce::String(model::positionText(roll_.songBeat(n.start), roll_.tempo()));
        rows_.push_back(std::make_unique<Row>(*this, k, label));
        holder_.addAndMakeVisible(*rows_.back());
    }
    changed();
    resized(); // the rows are new: laid out even when the size stays the same
    viewport_.setViewPosition(0, 0);
}

void MakeChordsForm::apply()
{
    roll_.auditionOff();
    if (!order_.empty())
    {
        roll_.applyEdit(model::makeChords(roll_.notes(), order_, choices_));
        order_.clear();
        choices_.clear();
    }
}

void MakeChordsForm::cancel()
{
    roll_.auditionOff();
    roll_.previewEdit(std::nullopt);
    order_.clear();
    choices_.clear();
}

void MakeChordsForm::setChoice(std::size_t card, const model::NoteChord& choice)
{
    if (card >= choices_.size())
        return;
    choices_[card] = choice;
    changed();
}

void MakeChordsForm::setAllKinds(music::ChordShape shape)
{
    const auto& notes = roll_.notes();
    for (std::size_t k = 0; k < choices_.size(); ++k)
    {
        choices_[k].mode = model::NoteChord::Mode::On;
        choices_[k].chord = music::keyChord(key(), notes[order_[k]].pitch, shape);
    }
    changed();
}

void MakeChordsForm::setAllVoicings(music::Voicing voicing)
{
    for (auto& c : choices_)
        c.voicing = voicing;
    changed();
}

std::optional<music::ChordShape> MakeChordsForm::allKind() const
{
    if (choices_.empty())
        return std::nullopt;
    const auto& notes = roll_.notes();
    for (auto s : shapes)
    {
        bool all = true;
        for (std::size_t k = 0; k < choices_.size() && all; ++k)
            all = choices_[k].mode == model::NoteChord::Mode::On &&
                  choices_[k].chord == music::keyChord(key(), notes[order_[k]].pitch, s);
        if (all)
            return s;
    }
    return std::nullopt;
}

std::optional<music::Voicing> MakeChordsForm::allVoicing() const
{
    if (choices_.empty())
        return std::nullopt;
    for (auto v : voicings)
        if (std::all_of(choices_.begin(), choices_.end(), [v](const auto& c) { return c.voicing == v; }))
            return v;
    return std::nullopt;
}

void MakeChordsForm::changed()
{
    // The notes in the clip as the rows make them.
    if (!order_.empty())
        roll_.previewEdit(model::makeChords(roll_.notes(), order_, choices_));
    all_->repaint();
    for (auto& r : rows_)
        r->repaint();
    repaint();
}

juce::String MakeChordsForm::chordText(std::size_t card) const
{
    if (card >= choices_.size())
        return {};
    const auto& c = choices_[card];
    if (c.mode == model::NoteChord::Mode::Leave)
        return "No chord";
    return nameOf(c.chord);
}

juce::PopupMenu MakeChordsForm::chordMenu(std::size_t card)
{
    juce::PopupMenu menu;
    if (card >= choices_.size())
        return menu;
    const int pitch = roll_.notes()[order_[card]].pitch;
    const auto noteName = juce::String::fromUTF8(music::pitchClassName(key(), pitch).c_str());
    const auto current = choices_[card];
    const juce::Component::SafePointer<MakeChordsForm> safe(this);
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

juce::PopupMenu MakeChordsForm::voicingMenu(std::size_t card)
{
    juce::PopupMenu menu;
    if (card >= choices_.size())
        return menu;
    const auto current = choices_[card];
    const juce::Component::SafePointer<MakeChordsForm> safe(this);
    const auto item = [&](const juce::String& text, const model::NoteChord& choice, bool ticked)
    {
        menu.addItem(juce::PopupMenu::Item(text).setTicked(ticked).setAction(
            [safe, card, choice]
            {
                if (safe != nullptr)
                    safe->setChoice(card, choice);
            }));
    };
    for (auto v : voicings)
    {
        auto choice = current;
        choice.voicing = v;
        item(model::voicingName(v), choice, current.voicing == v);
    }
    menu.addSeparator();
    for (auto i : inversions)
    {
        auto choice = current;
        choice.inversion = i;
        item(juce::String("Inversion: ") + model::inversionName(i), choice, current.inversion == i);
    }
    return menu;
}

juce::PopupMenu MakeChordsForm::allKindsMenu()
{
    juce::PopupMenu menu;
    const juce::Component::SafePointer<MakeChordsForm> safe(this);
    const auto now = allKind();
    for (auto s : shapes)
        menu.addItem(juce::PopupMenu::Item(model::chordShapeName(s))
                         .setTicked(now == s)
                         .setAction(
                             [safe, s]
                             {
                                 if (safe != nullptr)
                                     safe->setAllKinds(s);
                             }));
    return menu;
}

juce::PopupMenu MakeChordsForm::allVoicingsMenu()
{
    juce::PopupMenu menu;
    const juce::Component::SafePointer<MakeChordsForm> safe(this);
    const auto now = allVoicing();
    for (auto v : voicings)
        menu.addItem(juce::PopupMenu::Item(model::voicingName(v))
                         .setTicked(now == v)
                         .setAction(
                             [safe, v]
                             {
                                 if (safe != nullptr)
                                     safe->setAllVoicings(v);
                             }));
    return menu;
}

void MakeChordsForm::hoverCard(std::optional<std::size_t> card)
{
    if (!card || *card >= choices_.size())
    {
        roll_.auditionOff();
        return;
    }
    const auto made = model::madeChords(roll_.notes(), order_, choices_);
    roll_.audition(made[*card]);
}

juce::Component* MakeChordsForm::card(std::size_t index) const
{
    return index < rows_.size() ? rows_[index].get() : nullptr;
}

juce::String MakeChordsForm::title() const
{
    const auto n = static_cast<int>(choices_.size());
    return "Make chords" + juce::String::fromUTF8(" · ") + juce::String(n) + (n == 1 ? " note" : " notes");
}

// ---- layout --------------------------------------------------------------------------------------

int MakeChordsForm::labelWidth() const
{
    int w = widthOf(ui::capsLabelFont(), "ALL");
    for (const auto& r : rows_)
        w = std::max(w, widthOf(ui::capsLabelFont(), r->label().toUpperCase()));
    return w;
}

int MakeChordsForm::chordWidth() const
{
    // As wide as the widest chord any row's menu offers, or "No chord", or the All row's kinds.
    const auto& s = ui::Theme::current().shell;
    const auto name = ui::font(ui::TextStyle::Name), control = ui::font(ui::TextStyle::Control);
    int content = std::max(widthOf(name, "No chord"), widthOf(control, "Mixed"));
    for (auto shape : shapes)
        content = std::max(content, widthOf(control, model::chordShapeName(shape)));
    std::set<int> classes;
    for (auto i : order_)
        classes.insert(roll_.notes()[i].pitch % 12);
    for (int pc : classes)
    {
        for (auto shape : shapes)
            content = std::max(content, widthOf(name, nameOf(music::keyChord(key(), pc, shape))));
        for (const auto& chord : model::chordsUnder(key(), pc))
            content = std::max(content, widthOf(name, nameOf(chord)));
    }
    return content + s.itemGap + static_cast<int>(kChevron);
}

int MakeChordsForm::voicingWidth() const
{
    const auto& s = ui::Theme::current().shell;
    const auto control = ui::font(ui::TextStyle::Control);
    int content = widthOf(control, "Mixed");
    for (auto v : voicings)
        content = std::max(content, widthOf(control, juce::String(model::voicingName(v)) +
                                                         juce::String::fromUTF8(" · ") +
                                                         model::inversionName(music::Inversion::Second)));
    return content + s.itemGap + static_cast<int>(kChevron);
}

int MakeChordsForm::preferredWidth() const
{
    const auto& s = ui::Theme::current().shell;
    const int row =
        2 * s.buttonTextPadding + labelWidth() + s.groupGap + chordWidth() + s.itemGap + voicingWidth();
    const int titleRow = widthOf(ui::capsLabelFont(), title().toUpperCase()) + s.groupGap +
                         ui::Toggle::widthFor(apply_.getButtonText());
    return std::max(row + viewport_.getScrollBarThickness() + s.itemGap, titleRow);
}

void MakeChordsForm::resized()
{
    const auto& s = ui::Theme::current().shell;
    auto area = getLocalBounds();
    auto titleRow = area.removeFromTop(s.rowHeight);
    apply_.setBounds(titleRow.removeFromRight(ui::Toggle::widthFor(apply_.getButtonText())));
    area.removeFromTop(s.itemGap);
    all_->setBounds(area.removeFromTop(s.rowHeight));
    area.removeFromTop(s.itemGap);
    viewport_.setBounds(area);
    const int rowsHeight = static_cast<int>(rows_.size()) * (s.rowHeight + s.itemGap);
    const bool scrolls = rowsHeight > area.getHeight();
    const int width = area.getWidth() - (scrolls ? viewport_.getScrollBarThickness() + s.itemGap : 0);
    for (std::size_t k = 0; k < rows_.size(); ++k)
        rows_[k]->setBounds(0, static_cast<int>(k) * (s.rowHeight + s.itemGap), width, s.rowHeight);
    holder_.setSize(width, std::max(0, rowsHeight - s.itemGap));
}

void MakeChordsForm::paint(juce::Graphics& g)
{
    const auto& s = ui::Theme::current().shell;
    ui::paintCapsLabel(g, title(), getLocalBounds().removeFromTop(s.rowHeight).toFloat());
}
} // namespace fd::app
