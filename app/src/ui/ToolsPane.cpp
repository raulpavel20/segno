#include "ui/ToolsPane.h"

#include "model/AudioToolkit.h"
#include "model/MusicalTime.h"

namespace fd::app
{
namespace
{
constexpr std::string_view kMakeChords = "makeChords";

ui::Icon iconOf(std::string_view id)
{
    using I = ui::Icon;
    static const std::pair<std::string_view, I> icons[] = {
        {"quantise", I::Quantise},   {"humanize", I::Humanize},
        {"strum", I::Strum},         {"arpeggiate", I::Arpeggiate},
        {"chop", I::Chop},           {"ornament", I::Ornament},
        {"velocity", I::Velocity},   {"span", I::Span},
        {"randomise", I::Randomise}, {"rhythm", I::Rhythm},
        {"melody", I::Melody},       {"chords", I::Chords},
        {"bassline", I::Bassline},   {kMakeChords, I::MakeChords},
        {"normalise", I::Normalise}, {"tempo", I::Tempo}};
    for (const auto& [name, icon] : icons)
        if (name == id)
            return icon;
    return I::Transform;
}

juce::String utf8(std::string_view s)
{
    return juce::String::fromUTF8(std::string(s).c_str());
}

/// The piano roll's notes as the tools see them (piano-roll.md §25).
class RollTools final : public ToolHost
{
public:
    RollTools(PianoRoll& roll, engine::Session& session) : roll_(roll), session_(session) {}

    model::ToolValues toolValues(const model::ToolSpec& tool) const override
    {
        return roll_.toolValues(tool);
    }
    void setToolValues(const model::ToolSpec& tool, const model::ToolValues& values) override
    {
        roll_.setToolValues(tool, values);
    }
    void preview(const model::ToolSpec& tool, const model::ToolValues& values) override
    {
        const auto r = run(tool, values);
        roll_.previewEdit(r.why.empty() ? std::optional(r.edit) : std::nullopt);
    }
    void cancel() override { roll_.previewEdit(std::nullopt); }
    void apply(const model::ToolSpec& tool, const model::ToolValues& values) override
    {
        const auto r = run(tool, values);
        if (r.why.empty())
        {
            const engine::Session::EditGroup group(session_, std::string(tool.name)); // the undo step's name
            roll_.applyEdit(r.edit);
        }
        else
            roll_.previewEdit(std::nullopt);
    }
    juce::String scope(const model::ToolSpec& tool) const override
    {
        if (tool.kind == model::ToolKind::Transform)
        {
            const auto n =
                static_cast<int>(roll_.hasSelection() ? roll_.selection().size() : roll_.notes().size());
            return juce::String(n) + (n == 1 ? " note" : " notes");
        }
        const auto span = roll_.toolContext().span;
        return juce::String(model::lengthText(span.second - span.first, roll_.tempo()));
    }
    juce::String widestScope() const override { return "9999 notes"; }
    const music::Key* key() const override { return roll_.key() ? &*roll_.key() : nullptr; }
    juce::String unavailable(const model::ToolSpec& tool) const override
    {
        if (tool.kind == model::ToolKind::Transform)
            return roll_.notes().empty() ? "The clip has no notes yet" : "";
        // Of the generators, the bassline needs chords to follow (the chord lane's, kept by the roll).
        if (tool.id == "bassline" && roll_.chordSpans().empty())
            return "The bassline follows the chord lane: write chords first";
        return {};
    }
    std::uint64_t version() const override { return roll_.editVersion(); }
    juce::String what() const override { return "the notes"; }

private:
    [[nodiscard]] model::ToolRun run(const model::ToolSpec& tool, const model::ToolValues& values) const
    {
        return model::runNoteTool(tool, roll_.notes(), roll_.selection(), values, roll_.toolContext());
    }

    PianoRoll& roll_;
    engine::Session& session_;
};
} // namespace

ToolsPane::ToolsPane(PianoRoll& roll, engine::Session& session)
    : roll_(roll), session_(session), notes_(std::make_unique<RollTools>(roll, session)), form_(*notes_),
      makeChords_(roll)
{
    tabs_.setTitle("The Tools pane's tabs");
    tabs_.setSelected(0);
    tabs_.onChange = [this](int index)
    {
        const auto kind = index == 1 ? model::ToolKind::Generate : model::ToolKind::Transform;
        setTab(kind);
        if (onTabChanged)
            onTabChanged(kind);
    };
    addAndMakeVisible(tabs_);
    // Every tool a tile: the transforms (Make chords after them), then the generators.
    const auto add = [this](std::string id, model::ToolKind kind, const juce::String& name)
    {
        auto tile = std::make_unique<ui::Tile>(iconOf(id), name);
        tile->onClick = [this, id] { choose(chosen_ == id ? std::string_view{} : std::string_view(id)); };
        addChildComponent(*tile);
        tiles_.push_back({std::move(id), kind, std::move(tile)});
    };
    for (const auto& t : model::noteTools())
        if (t.kind == model::ToolKind::Transform)
            add(std::string(t.id), t.kind, utf8(t.name));
    add(std::string(kMakeChords), model::ToolKind::Transform, "Make chords");
    for (const auto& t : model::noteTools())
        if (t.kind == model::ToolKind::Generate)
            add(std::string(t.id), t.kind, utf8(t.name));
    // The audio's (audio-editor.md §11), shown instead while the clip view shows an audio clip.
    for (const auto& t : model::audioTools())
    {
        add(std::string(t.id), t.kind, utf8(t.name));
        tiles_.back().audio = true;
    }
    addChildComponent(form_);
    addChildComponent(makeChords_);
    form_.onLayoutChanged = [this] { resized(); };
    // Their Apply buttons apply as Return does: the tool let go after.
    form_.onApply = [this] { apply(); };
    makeChords_.onApply = [this] { apply(); };
    setTab(model::ToolKind::Transform);
    setWantsKeyboardFocus(true);
}

ToolsPane::~ToolsPane() = default;

bool ToolsPane::shows(const TileItem& item) const noexcept
{
    return audio_ != nullptr ? item.audio : !item.audio && item.kind == tab_;
}

const std::vector<model::ToolSpec>& ToolsPane::catalogue() const
{
    return audio_ != nullptr ? model::audioTools() : model::noteTools();
}

const model::ToolSpec* ToolsPane::findTool(std::string_view id) const
{
    return audio_ != nullptr ? model::findAudioTool(id) : model::findNoteTool(id);
}

ToolHost& ToolsPane::host() const
{
    return audio_ != nullptr ? *audio_ : *notes_;
}

void ToolsPane::setAudio(ToolHost* host)
{
    if (host == audio_)
        return;
    if (!chosen_.empty())
        disarm(); // what the tool showed goes back first
    audio_ = host;
    form_.setHost(this->host());
    tabs_.setVisible(audio_ == nullptr);
    for (auto& item : tiles_)
        item.tile->setVisible(shows(item));
    refresh();
    resized();
    repaint();
}

void ToolsPane::setTab(model::ToolKind tab)
{
    if (!chosen_.empty())
        disarm();
    tab_ = tab;
    tabs_.setSelected(tab == model::ToolKind::Generate ? 1 : 0);
    for (auto& item : tiles_)
        item.tile->setVisible(shows(item));
    refresh();
    resized();
    repaint();
}

void ToolsPane::choose(std::string_view id)
{
    // The tool shown now lets go of the notes first.
    if (chosen_ == kMakeChords)
        makeChords_.cancel();
    else if (!chosen_.empty())
        form_.cancel();
    chosen_.clear();
    form_.setTool(nullptr);
    if (!id.empty())
    {
        const auto* tool = findTool(id);
        const auto kind = id == kMakeChords ? model::ToolKind::Transform
                          : tool != nullptr ? tool->kind
                                            : tab_;
        if (audio_ == nullptr && kind != tab_)
            setTab(kind);
        chosen_ = std::string(id);
        if (id == kMakeChords)
            makeChords_.start();
        else if (tool != nullptr)
            form_.setTool(tool);
        seen_ = host().version();
        if (isShowing())
            grabKeyboardFocus(); // Return applies, Escape lets go
    }
    for (auto& item : tiles_)
        item.tile->setToggleState(item.id == chosen_, juce::dontSendNotification);
    form_.setVisible(!chosen_.empty() && chosen_ != kMakeChords);
    makeChords_.setVisible(chosen_ == kMakeChords);
    resized();
    repaint();
    if (onChosen)
        onChosen();
}

void ToolsPane::apply()
{
    if (chosen_.empty())
        return;
    if (chosen_ == kMakeChords)
    {
        const engine::Session::EditGroup group(session_, "Make chords"); // the undo step's name
        makeChords_.apply();
    }
    else
        form_.apply();
    chosen_.clear();
    form_.setTool(nullptr);
    for (auto& item : tiles_)
        item.tile->setToggleState(false, juce::dontSendNotification);
    form_.setVisible(false);
    makeChords_.setVisible(false);
    resized();
    repaint();
    if (onChosen)
        onChosen();
}

juce::String ToolsPane::unavailable(const TileItem& item) const
{
    if (item.id == kMakeChords)
        return roll_.notes().empty() ? "The clip has no notes yet" : "";
    if (item.audio)
        return audio_ != nullptr ? audio_->unavailable(*model::findAudioTool(item.id)) : juce::String();
    const auto* tool = model::findNoteTool(item.id);
    return tool != nullptr ? notes_->unavailable(*tool) : juce::String();
}

juce::String ToolsPane::tipFor(const TileItem& item) const
{
    if (const auto why = unavailable(item); why.isNotEmpty())
        return why;
    const auto* tool = item.audio ? model::findAudioTool(item.id) : model::findNoteTool(item.id);
    const auto about =
        tool != nullptr ? utf8(tool->about)
                        : juce::String("Each note becomes a chord: its own kind and voicing, or all at once");
    return compact_ ? item.tile->getButtonText() + ": " + about : about;
}

void ToolsPane::refresh()
{
    for (auto& item : tiles_)
    {
        const bool can = unavailable(item).isEmpty();
        if (item.tile->isEnabled() != can)
            item.tile->setEnabled(can);
        if (const auto tip = tipFor(item); item.tile->getTooltip() != tip)
            item.tile->setTooltip(tip);
    }
    // The notes or the selection changed under the chosen tool (an undo, an assistant, Select
    // all): shown again on them, or let go when it can't run on them.
    if (!chosen_.empty() && host().version() != seen_)
    {
        const auto chosen = chosen_;
        const auto it =
            std::find_if(tiles_.begin(), tiles_.end(), [&](const auto& t) { return t.id == chosen; });
        if (it != tiles_.end() && unavailable(*it).isNotEmpty())
            disarm();
        else if (chosen == kMakeChords)
            makeChords_.start();
        else
            form_.preview();
        seen_ = host().version();
    }
}

void ToolsPane::visibilityChanged()
{
    if (isVisible())
        refresh(); // its tiles as the notes allow now, not as they were when it closed
}

ui::Tile* ToolsPane::tile(std::string_view id) const
{
    for (const auto& item : tiles_)
        if (item.id == id)
            return item.tile.get();
    return nullptr;
}

bool ToolsPane::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        if (!chosen_.empty())
            disarm();
        else if (onEscape)
            onEscape();
        return true;
    }
    if (key == juce::KeyPress::returnKey && !chosen_.empty())
    {
        apply();
        return true;
    }
    return false;
}

void ToolsPane::setFloating(bool floating)
{
    if (floating == floating_)
        return;
    floating_ = floating;
    resized();
    repaint();
}

// ---- layout --------------------------------------------------------------------------------------

int ToolsPane::preferredWidth() const
{
    const auto& s = ui::Theme::current().shell;
    int tile = 0;
    for (const auto& item : tiles_)
        if (item.audio == (audio_ != nullptr))
            tile = std::max(tile, item.tile->preferredWidth());
    int content = std::max(audio_ != nullptr ? 0 : tabs_.preferredWidth(), 2 * tile + s.itemGap);
    for (const auto& t : catalogue())
        content = std::max(content, form_.preferredWidth(t));
    if (chosen_ == kMakeChords)
        content = std::max(content, makeChords_.preferredWidth());
    const int margin = floating_ ? 2 * juce::roundToInt(s.shadowRadius) : 0;
    return content + 2 * s.padding + margin;
}

juce::Rectangle<int> ToolsPane::content() const
{
    const auto& s = ui::Theme::current().shell;
    auto area = getLocalBounds();
    if (floating_)
        area = area.reduced(juce::roundToInt(s.shadowRadius));
    return area.reduced(s.padding);
}

int ToolsPane::tilesHeight(bool compact) const
{
    const auto& s = ui::Theme::current().shell;
    int shown = 0;
    for (const auto& item : tiles_)
        shown += shows(item) ? 1 : 0;
    // Compact: square tiles, as many to a row as fit.
    const int perRow =
        compact ? std::max(1, (content().getWidth() + s.itemGap) / (s.fieldHeight + s.itemGap)) : 2;
    const int rows = (shown + perRow - 1) / perRow;
    return rows * s.fieldHeight + std::max(0, rows - 1) * s.itemGap;
}

int ToolsPane::toolHeight() const
{
    const auto& s = ui::Theme::current().shell;
    int tallest = 0;
    for (const auto& item : tiles_)
    {
        if (!shows(item))
            continue;
        if (item.id == kMakeChords)
            tallest = std::max(tallest, s.rowHeight + 5 * (s.itemGap + s.rowHeight));
        else if (const auto* tool = findTool(item.id))
            tallest = std::max(tallest, form_.preferredHeight(*tool));
    }
    return tallest;
}

juce::Rectangle<int> ToolsPane::formArea() const
{
    const auto& s = ui::Theme::current().shell;
    auto area = content();
    if (audio_ == nullptr)
        area.removeFromTop(s.rowHeight + s.itemGap); // the tabs
    area.removeFromTop(tilesHeight(compact_));
    area.removeFromTop(s.groupGap); // a line in the middle of it
    return area;
}

void ToolsPane::resized()
{
    const auto& s = ui::Theme::current().shell;
    auto area = content();
    if (audio_ == nullptr)
    {
        tabs_.setBounds(area.removeFromTop(s.rowHeight).withWidth(tabs_.preferredWidth()));
        area.removeFromTop(s.itemGap);
    }
    // The tiles with their names where the tab's tallest tool fits under them, else icons alone.
    const bool compact = tilesHeight(false) + s.groupGap + toolHeight() > area.getHeight();
    if (compact != compact_)
    {
        compact_ = compact;
        for (auto& item : tiles_)
        {
            item.tile->setCompact(compact);
            item.tile->setTooltip(tipFor(item));
        }
    }
    const int column = (area.getWidth() - s.itemGap) / 2;
    int k = 0, x = area.getX(), y = area.getY();
    for (auto& item : tiles_)
    {
        if (!shows(item))
            continue;
        if (compact_)
        {
            if (x + s.fieldHeight > area.getRight())
            {
                x = area.getX();
                y += s.fieldHeight + s.itemGap;
            }
            item.tile->setBounds(x, y, s.fieldHeight, s.fieldHeight);
            x += s.fieldHeight + s.itemGap;
            continue;
        }
        const int row = k / 2, col = k % 2;
        item.tile->setBounds(area.getX() + col * (column + s.itemGap),
                             area.getY() + row * (s.fieldHeight + s.itemGap), column, s.fieldHeight);
        ++k;
    }
    const auto below = formArea();
    form_.setBounds(below.withHeight(std::min(below.getHeight(), form_.preferredHeight())));
    makeChords_.setBounds(below);
}

void ToolsPane::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    const auto& s = t.shell;
    if (floating_)
        ui::paintPopover(g, getLocalBounds().reduced(juce::roundToInt(s.shadowRadius)));
    else
    {
        g.setColour(t.surface1);
        g.fillRect(getLocalBounds());
        g.setColour(t.line);
        g.fillRect(getLocalBounds().removeFromLeft(1)); // where the notes end
    }
    // Between the tiles and the tool: a line, or what to do while nothing is chosen.
    auto below = formArea();
    g.setColour(t.line);
    g.fillRect(below.getX(), below.getY() - s.groupGap / 2, below.getWidth(), 1);
    if (chosen_.empty())
    {
        g.setColour(t.textTertiary);
        g.setFont(ui::font(ui::TextStyle::Caption));
        g.drawFittedText("Choose a tool: " + host().what() + " show it as you set it, and Apply keeps it.",
                         below.removeFromTop(3 * s.rowHeight), juce::Justification::topLeft, 3);
    }
}
} // namespace fd::app
