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

void OctaveKeys::setNotes(int root, std::uint16_t notes, std::array<juce::String, 12> names)
{
    if (root == root_ && notes == notes_ && names == names_)
        return;
    root_ = root;
    notes_ = notes;
    names_ = std::move(names);
    repaint();
}

juce::Point<int> OctaveKeys::preferredSize()
{
    const auto& s = Theme::current().shell;
    return {7 * s.octaveKeyWidth, s.octaveKeyHeight};
}

juce::Rectangle<float> OctaveKeys::keyArea(int pitchClass) const
{
    // As wide as it's laid out: a seventh each for the white keys, on whole pixels (the metric's
    // width at least); black keys in the metrics' proportion, an even width so they sit centred on
    // the seams.
    const auto& s = Theme::current().shell;
    const int w = std::max(s.octaveKeyWidth, getWidth() / 7);
    const int bw = 2 * juce::roundToInt(0.5f * static_cast<float>(w * s.octaveBlackKeyWidth) /
                                        static_cast<float>(s.octaveKeyWidth));
    for (size_t i = 0; i < whiteKeys.size(); ++i)
        if (whiteKeys[i] == pitchClass)
            return juce::Rectangle<int>(static_cast<int>(i) * w, 0, w, s.octaveKeyHeight).toFloat();
    for (const auto& [pc, after] : blackKeys)
        if (pc == pitchClass)
            return juce::Rectangle<int>((after + 1) * w - bw / 2, 0, bw, s.octaveBlackKeyHeight).toFloat();
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
        // The set's notes: named near the key's end, where a finger plays it (a dot without a name).
        if ((notes_ & (1u << pc)) == 0 && pc != root_)
            return;
        // A white key's name in the part below the black keys; a black key's at its end.
        const auto end = black ? area.withTop(area.getBottom() - area.getWidth())
                               : area.withTop(static_cast<float>(s.octaveBlackKeyHeight));
        g.setColour(pc == root_ ? t.onActive : black ? t.roles.keyWhite : t.roles.keyBlack);
        if (names_[static_cast<size_t>(pc)].isNotEmpty())
        {
            g.setFont(font(TextStyle::Micro));
            g.drawText(names_[static_cast<size_t>(pc)], end, juce::Justification::centred, false);
        }
        else if (pc != root_)
            g.fillEllipse(juce::Rectangle<float>(dot, dot).withCentre(end.getCentre()));
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
