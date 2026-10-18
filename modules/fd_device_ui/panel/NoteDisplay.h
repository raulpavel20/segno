#pragma once

namespace fd::ui
{
/// A MIDI effect's note display (docs/design/midi-effects.md §12): the bars just played and the
/// one coming, as a small piano roll. The notes that came in are faint, the ones it played in the
/// value colour; the playhead, bar and beat lines, and the end of the section in sight. Its label
/// line names the chord sounding and the next one (or why there's none), and what it follows. Fed
/// by the host's NoteFeed; with none, or nothing played, it says what it will show.
class NoteDisplay : public juce::Component, public ParamControl, public juce::TooltipClient
{
public:
    static constexpr double barsBefore = 1.5, barsAfter = 0.5;

    explicit NoteDisplay(NoteFeed* feed) : feed_(feed) {}

    /// Every frame on screen: what the feed has now.
    void refresh() override;
    void paint(juce::Graphics& g) override;
    juce::String getTooltip() override;

    /// The plot, below the label line.
    [[nodiscard]] juce::Rectangle<float> plotArea() const;

protected:
    /// Where the notes are drawn: the whole plot, unless a display shares it.
    [[nodiscard]] virtual juce::Rectangle<float> rollArea() const { return plotArea(); }
    /// Draws over the plot, after the notes (a display that's also a control: lanes, steps).
    virtual void paintOver(juce::Graphics&, juce::Rectangle<float> /*plot*/, const NoteFeed::View&) {}

    NoteFeed* feed_ = nullptr;
    NoteFeed::View view_;
};

/// A step sequence's lanes, as its host holds them (the Arpeggiator's, Melody's; midi-effects.md
/// §6.1, §6.11): each lane's values and length, and the step each plays now.
class StepFeed
{
public:
    struct Lane
    {
        juce::String name;
        std::vector<float> values; // every step's, the lane's length or more
        int length = 16;
        float min = 0.0f, max = 1.0f, neutral = 1.0f; // a step at `neutral` changes nothing
        bool pitched = false; // values are rows (steps of the key): drawn as notes, not bars
        std::function<juce::String(float)> text; // a value as the tooltip writes it
    };
    virtual ~StepFeed() = default;
    [[nodiscard]] virtual Lane lane(int index) const = 0;
    /// The step `lane` plays now, or −1 while nothing plays.
    [[nodiscard]] virtual int playing(int lane) const = 0;
    /// A step drawn, and a lane's length dragged: through the host binding, an undo step a gesture.
    virtual void setStep(int lane, int step, float value) = 0;
    virtual void setLength(int lane, int length) = 0;
};

/// The note display with a step sequence over it (the Arpeggiator's and Melody's STEPS): the lane
/// chosen, its steps as bars (or notes, for a pitch lane) drawn by dragging across them, its length
/// a handle after its last step and the steps past it shaded, the step playing lit; under them the
/// notes going out, and above, the chord as every note display has it.
class StepDisplay final : public NoteDisplay
{
public:
    static constexpr int maxSteps = 16; // shown at once
    StepDisplay(NoteFeed* notes, StepFeed* steps) : NoteDisplay(notes), steps_(steps) {}

    void setLane(int index);
    [[nodiscard]] int lane() const noexcept { return lane_; }
    void refresh() override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    juce::String getTooltip() override;

    /// Where the lane's steps are drawn: the plot above the notes.
    [[nodiscard]] juce::Rectangle<float> stepArea() const;

protected:
    [[nodiscard]] juce::Rectangle<float> rollArea() const override;
    void paintOver(juce::Graphics& g, juce::Rectangle<float> plot, const NoteFeed::View& view) override;

private:
    [[nodiscard]] int stepAt(float x) const;
    void edit(juce::Point<float> at);

    StepFeed* steps_ = nullptr;
    int lane_ = 0;
    bool draggingLength_ = false;
};
} // namespace fd::ui
