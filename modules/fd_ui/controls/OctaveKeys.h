#pragma once

namespace fd::ui
{
/// One octave of keys, C to B: the Key popover's root (key-and-scale.md §6, ADR 0056). A piano's
/// shape at the shell's key sizes (`shell.octaveKey…`): the root filled in `active`, the set's
/// notes named on their keys as the key spells them (a dot where no name is given), a hovered key
/// under the hover wash. A click on a key calls onKey with its pitch class (0 = C … 11 = B).
class OctaveKeys final : public juce::Component
{
public:
    OctaveKeys();

    /// The root (−1: none) and the set's notes (bit n: pitch class n), each with its name ("B♭") by
    /// pitch class; an empty name shows a dot.
    void setNotes(int root, std::uint16_t notes, std::array<juce::String, 12> names = {});
    [[nodiscard]] int root() const noexcept { return root_; }
    [[nodiscard]] std::uint16_t notes() const noexcept { return notes_; }

    /// Its least size, at the shell's metrics: seven white keys. Laid out wider, its white keys
    /// take a seventh each, on whole pixels (a width that's a multiple of 7 fills it exactly).
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
    std::array<juce::String, 12> names_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OctaveKeys)
};
} // namespace fd::ui
