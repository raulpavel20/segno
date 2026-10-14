namespace fd::ui
{
namespace
{
constexpr std::array<int, 7> whiteKeys{0, 2, 4, 5, 7, 9, 11};
/// Each black key's pitch class and the white key it follows (its seam is after that one).
constexpr std::array<std::pair<int, int>, 5> blackKeys{{{1, 0}, {3, 1}, {6, 3}, {8, 4}, {10, 5}}};

bool isBlack(int pc) noexcept
{
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}
} // namespace

OctaveKeys::OctaveKeys()
{
    setWantsKeyboardFocus(false);
    setRepaintsOnMouseActivity(false);
}

void OctaveKeys::setNotes(int root, std::uint16_t notes)
{
    if (root == root_ && notes == notes_)
        return;
    root_ = root;
    notes_ = notes;
    repaint();
}

juce::Point<int> OctaveKeys::preferredSize()
{
    const auto& s = Theme::current().shell;
    return {7 * s.octaveKeyWidth, s.octaveKeyHeight};
}

juce::Rectangle<float> OctaveKeys::keyArea(int pitchClass) const
{
    const auto& s = Theme::current().shell;
    const auto w = static_cast<float>(s.octaveKeyWidth);
    for (size_t i = 0; i < whiteKeys.size(); ++i)
        if (whiteKeys[i] == pitchClass)
            return {static_cast<float>(i) * w, 0.0f, w, static_cast<float>(s.octaveKeyHeight)};
    for (const auto& [pc, after] : blackKeys)
        if (pc == pitchClass)
        {
            const auto bw = static_cast<float>(s.octaveBlackKeyWidth);
            return {static_cast<float>(after + 1) * w - bw * 0.5f, 0.0f, bw,
                    static_cast<float>(s.octaveBlackKeyHeight)};
        }
    return {};
}

int OctaveKeys::keyAt(juce::Point<float> p) const
{
    for (const auto& [pc, after] : blackKeys)
        if (keyArea(pc).contains(p))
            return pc;
    for (int pc : whiteKeys)
        if (keyArea(pc).contains(p))
            return pc;
    return -1;
}

void OctaveKeys::paint(juce::Graphics& g)
{
    const auto& t = Theme::current();
    const auto& s = t.shell;
    const auto dot = static_cast<float>(s.lightSize);
    auto paintKey = [&](int pc)
    {
        const auto area = keyArea(pc);
        const bool black = isBlack(pc);
        g.setColour(pc == root_ ? t.active : black ? t.roles.keyBlack : t.roles.keyWhite);
        g.fillRect(area);
        if (pc == hovered_ && pc != root_)
        {
            g.setColour(t.roles.hoverWash);
            g.fillRect(area);
        }
        // The key's other notes: a dot near the key's end, where a finger plays it.
        if (pc != root_ && (notes_ & (1u << pc)) != 0)
        {
            const auto centre =
                juce::Point<float>(area.getCentreX(), area.getBottom() - area.getWidth() * 0.5f);
            g.setColour(black ? t.roles.keyWhite : t.roles.keyBlack);
            g.fillEllipse(juce::Rectangle<float>(dot, dot).withCentre(centre));
        }
    };
    for (int pc : whiteKeys)
        paintKey(pc);
    // The seams between the white keys, then the black keys over them.
    g.setColour(t.roles.keySeam);
    for (size_t i = 1; i < whiteKeys.size(); ++i)
        g.fillRect(keyArea(whiteKeys[i]).withWidth(1.0f));
    for (const auto& [pc, after] : blackKeys)
        paintKey(pc);
}

void OctaveKeys::mouseDown(const juce::MouseEvent& e)
{
    if (const int pc = keyAt(e.position); pc >= 0 && onKey)
        onKey(pc);
}

void OctaveKeys::mouseMove(const juce::MouseEvent& e)
{
    setHovered(keyAt(e.position));
}

void OctaveKeys::mouseExit(const juce::MouseEvent&)
{
    setHovered(-1);
}

void OctaveKeys::setHovered(int pitchClass)
{
    if (hovered_ == pitchClass)
        return;
    hovered_ = pitchClass;
    repaint();
}
} // namespace fd::ui
