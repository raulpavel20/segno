namespace fd::ui
{
class ModuleEditor::CapsLabel final : public juce::Component
{
public:
    explicit CapsLabel(juce::String text) : text_(std::move(text)) { setInterceptsMouseClicks(false, false); }
    void paint(juce::Graphics& g) override { paintCapsLabel(g, text_, getLocalBounds().toFloat()); }

private:
    juce::String text_;
};

ModuleEditor::ModuleEditor() = default;

ModuleEditor::~ModuleEditor() = default;

int ModuleEditor::addPane(juce::String title, int columns, int minColumns)
{
    Pane p;
    p.title = std::move(title);
    p.columns = std::max(0, columns);
    p.minColumns = columns > 0 ? columns : std::max(1, minColumns);
    panes_.push_back(std::move(p));
    return static_cast<int>(panes_.size()) - 1;
}

void ModuleEditor::setPaneTitle(int pane, juce::String title)
{
    auto& p = panes_.at(static_cast<size_t>(pane));
    if (p.title == title)
        return;
    p.title = std::move(title);
    layoutTitle(pane);
    repaint();
}

void ModuleEditor::placeInTitle(int pane, juce::Component& component, int width, bool right)
{
    panes_.at(static_cast<size_t>(pane)).titled.push_back({&component, width, right});
    addAndMakeVisible(component);
    layoutTitle(pane);
}

void ModuleEditor::place(int pane, juce::Component& component, int column, int row, int columns, int rows,
                         Kind kind)
{
    jassert(pane >= 0 && pane < paneCount());
    jassert(column >= 0);
    const Placement p{&component, kind, pane, column, row, columns, rows};
    placements_.push_back(p);
    addAndMakeVisible(component);
    layoutPlacement(p);
}

void ModuleEditor::label(int pane, juce::String text, int column, int row, int columns)
{
    auto& l = *labels_.emplace_back(std::make_unique<CapsLabel>(std::move(text)));
    place(pane, l, column, row, columns, 1, Kind::Label);
}

void ModuleEditor::clearPane(int pane)
{
    std::erase_if(placements_,
                  [this, pane](const Placement& p)
                  {
                      if (p.pane != pane)
                          return false;
                      p.component->setVisible(false);
                      std::erase_if(labels_, [&p](const auto& l) { return l.get() == p.component; });
                      return true;
                  });
}

int ModuleEditor::minimumHeight() const
{
    const auto& grid = Theme::current().deviceGrid;
    return grid.contentTop() + grid.height(minimumRows_) + grid.bodyBottom;
}

int ModuleEditor::minimumWidth() const
{
    const auto& grid = Theme::current().deviceGrid;
    int width = 0;
    for (const auto& p : panes_)
        width += grid.sectionWidth(p.minColumns);
    return width;
}

juce::Rectangle<int> ModuleEditor::paneBounds(int pane) const
{
    return panes_.at(static_cast<size_t>(pane)).bounds;
}

juce::Rectangle<int> ModuleEditor::paneContent(int pane) const
{
    return panes_.at(static_cast<size_t>(pane)).content;
}

juce::Rectangle<int> ModuleEditor::paneTitleArea(int pane) const
{
    const auto& grid = Theme::current().deviceGrid;
    const auto& content = panes_.at(static_cast<size_t>(pane)).content;
    return {content.getX(), grid.bodyTop, content.getWidth(), grid.titleHeight};
}

juce::Rectangle<int> ModuleEditor::paneTitleText(int pane) const
{
    const auto& p = panes_.at(static_cast<size_t>(pane));
    if (p.title.isEmpty())
        return {};
    const auto width = static_cast<int>(
        std::ceil(juce::GlyphArrangement::getStringWidth(capsLabelFont(), p.title.toUpperCase())));
    return paneTitleArea(pane).withWidth(width);
}

std::vector<juce::Component*> ModuleEditor::titleComponents(int pane) const
{
    std::vector<juce::Component*> out;
    for (const auto& t : panes_.at(static_cast<size_t>(pane)).titled)
        out.push_back(t.component);
    return out;
}

juce::Rectangle<int> ModuleEditor::cellArea(const Placement& p) const
{
    const auto& grid = Theme::current().deviceGrid;
    const auto& content = panes_.at(static_cast<size_t>(p.pane)).content;
    const int pitchX = grid.column + grid.columnGap, pitchY = grid.row + grid.rowGap;
    const int left = content.getX() + p.column * pitchX;
    const int right = p.columns > 0 ? left + grid.width(p.columns) : content.getRight() + p.columns * pitchX;
    // Rows from the top, or (negative) from the bottom edge.
    const int top =
        p.row >= 0 ? content.getY() + p.row * pitchY : content.getBottom() + p.row * pitchY + grid.rowGap;
    const int bottom = p.rows > 0 ? top + grid.height(p.rows) : content.getBottom() + p.rows * pitchY;
    return {left, top, std::max(0, right - left), std::max(0, bottom - top)};
}

void ModuleEditor::layoutPlacement(const Placement& p)
{
    p.component->setBounds(cellArea(p));
}

void ModuleEditor::layoutTitle(int pane)
{
    const auto& s = Theme::current().shell;
    const auto& p = panes_.at(static_cast<size_t>(pane));
    const auto area = paneTitleArea(pane);
    int right = area.getRight();
    int left = p.title.isEmpty() ? area.getX() : paneTitleText(pane).getRight() + s.groupGap;
    for (const auto& t : p.titled)
    {
        if (t.right)
        {
            t.component->setBounds(right - t.width, area.getY(), t.width, area.getHeight());
            right -= t.width + s.itemGap;
        }
        else
        {
            t.component->setBounds(left, area.getY(), t.width, area.getHeight());
            left += t.width + s.itemGap;
        }
    }
}

void ModuleEditor::resized()
{
    const auto& grid = Theme::current().deviceGrid;
    const int top = grid.contentTop(), bottom = getHeight() - grid.bodyBottom;
    rows_ = std::max(1, (bottom - top + grid.rowGap) / (grid.row + grid.rowGap));

    // The fixed panes at their widths; the flexible one takes what's left.
    int fixed = 0;
    for (const auto& p : panes_)
        fixed += p.columns > 0 ? grid.sectionWidth(p.columns) : 0;
    int x = 0;
    separators_.clear();
    for (auto& p : panes_)
    {
        const int width = p.columns > 0 ? grid.sectionWidth(p.columns)
                                        : std::max(grid.sectionWidth(p.minColumns), getWidth() - fixed);
        if (x > 0)
            separators_.push_back(x);
        p.bounds = {x, 0, width, getHeight()};
        p.content = {x + grid.sectionPadding, top, width - 2 * grid.sectionPadding,
                     std::max(0, bottom - top)};
        x += width;
    }
    for (int i = 0; i < paneCount(); ++i)
        layoutTitle(i);
    for (const auto& p : placements_)
        layoutPlacement(p);
}

void ModuleEditor::paint(juce::Graphics& g)
{
    const auto& t = Theme::current();
    const auto& grid = t.deviceGrid;
    // A card like the module's panel.
    g.setColour(t.surface2);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), t.radiusPanel);
    for (int i = 0; i < paneCount(); ++i)
        if (const auto& title = panes_[static_cast<size_t>(i)].title; title.isNotEmpty())
            paintCapsLabel(g, title, paneTitleText(i).toFloat());
    // Lines between panes, from the title row to the bottom of the content.
    g.setColour(t.line);
    for (const int x : separators_)
        g.fillRect(static_cast<float>(x), static_cast<float>(grid.bodyTop), 1.0f,
                   static_cast<float>(getHeight() - grid.bodyTop - grid.bodyBottom));
}
} // namespace fd::ui
