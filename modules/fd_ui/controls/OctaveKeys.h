#pragma once

namespace fd::ui
{
/// One octave of keys, C to B: the Key popover's root (key-and-scale.md §6, ADR 0056). A piano's
/// shape at the shell's key sizes (`shell.octaveKey…`): the root filled in `active`, the other
/// notes of a set marked with a dot, a hovered key under the hover wash. A click on a key calls
/// onKey with its pitch class (0 = C … 11 = B).
class OctaveKeys final : public juce::Component
{
public:
    OctaveKeys();

    /// The root (−1: none) and the set's notes (bit n: pitch class n).
    void setNotes(int root, std::uint16_t notes);
    [[nodiscard]] int root() const noexcept { return root_; }
    [[nodiscard]] std::uint16_t notes() const noexcept { return notes_; }

    /// Its size at the shell's metrics: seven white keys.
    [[nodiscard]] static juce::Point<int> preferredSize();
    /// The pitch class of the key under a point (black keys over white ones); −1 for none.
    [[nodiscard]] int keyAt(juce::Point<float> p) const;
    /// A key's area: black keys over the seams between the white ones.
    [[nodiscard]] juce::Rectangle<float> keyArea(int pitchClass) const;

    std::function<void(int pitchClass)> onKey;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

private:
    void setHovered(int pitchClass);

    int root_ = -1, hovered_ = -1;
    std::uint16_t notes_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OctaveKeys)
};
} // namespace fd::ui
