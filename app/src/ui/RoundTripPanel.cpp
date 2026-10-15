#include "ui/RoundTripPanel.h"

namespace fd::app
{
namespace
{
const juce::String kTitle = "Measure the round trip";
const juce::String kHint = "Connect an output to an input with a cable, and turn monitoring down.";
const juce::String kOutput = "Output", kInput = "Input";

int textWidth(ui::TextStyle style, const juce::String& text)
{
    return static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(ui::font(style), text)));
}

juce::String ms(double value)
{
    return juce::String(value, 1) + " ms";
}
} // namespace

RoundTripPanel::RoundTripPanel(engine::Session& session) : session_(session)
{
    for (auto* c : {&outputs_, &inputs_})
    {
        c->setWantsKeyboardFocus(false);
        addAndMakeVisible(*c);
    }
    outputs_.setTooltip("The output the cable leaves from");
    inputs_.setTooltip("The input the cable comes back into");
    for (auto* b : {&measure_, &use_, &cancel_})
    {
        b->setClickingTogglesState(false);
        b->setWantsKeyboardFocus(false);
        addAndMakeVisible(*b);
    }
    measure_.setTooltip(
        "Plays a short noise with clicks on the output three times, and finds it on the input");
    use_.setTooltip("Sets Audio offset to the offset measured");
    cancel_.setTooltip("Closes this, leaving Audio offset as it was");
    measure_.onClick = [this] { measure(); };
    use_.onClick = [this]
    {
        if (result_ && result_->ok())
            session_.setRecordingOffsets(result_->offsetMs, session_.recordingOffsets().second);
        if (onClose)
            onClose();
    };
    cancel_.onClick = [this]
    {
        session_.cancelRoundTrip();
        trying_ = 0;
        if (onClose)
            onClose();
    };
    opened();
}

RoundTripPanel::~RoundTripPanel()
{
    if (trying_ > 0)
        session_.cancelRoundTrip();
}

int RoundTripPanel::preferredHeight()
{
    const auto& s = ui::Theme::current().shell;
    return 2 * s.groupGap + 3 * s.rowHeight + 2 * s.fieldHeight + 2 * s.itemGap + s.groupGap;
}

void RoundTripPanel::opened()
{
    const auto channels = session_.activeChannels();
    const auto fill = [](juce::ComboBox& c, const std::vector<std::string>& names)
    {
        const int was = std::max(0, c.getSelectedItemIndex());
        c.clear(juce::dontSendNotification);
        for (size_t i = 0; i < names.size(); ++i)
            c.addItem(juce::String::fromUTF8(names[i].c_str()), static_cast<int>(i) + 1);
        c.setSelectedItemIndex(std::min(was, std::max(0, c.getNumItems() - 1)), juce::dontSendNotification);
    };
    fill(outputs_, channels.outputs);
    fill(inputs_, channels.inputs);
    result_.reset();
    update();
}

void RoundTripPanel::measure()
{
    if (trying_ > 0 || outputs_.getNumItems() == 0 || inputs_.getNumItems() == 0)
        return;
    result_.reset();
    trying_ = 1;
    juce::Component::SafePointer<RoundTripPanel> self(this);
    session_.measureRoundTrip(
        outputs_.getSelectedItemIndex(), inputs_.getSelectedItemIndex(),
        [self](int n)
        {
            if (self != nullptr)
            {
                self->trying_ = n;
                self->update();
            }
        },
        [self](const model::RoundTripResult& r)
        {
            if (self != nullptr)
            {
                self->trying_ = 0;
                self->result_ = r;
                self->update();
            }
        });
    update();
}

juce::String RoundTripPanel::resultText() const
{
    if (trying_ > 0)
        return juce::String::fromUTF8("Measuring\xe2\x80\xa6 ") + juce::String(trying_) + " of 3";
    if (result_)
        return juce::String::fromUTF8(model::roundTripText(*result_).c_str());
    // Before measuring: what the interface owns up to.
    const auto channels = session_.activeChannels();
    if (channels.outputs.empty() || channels.inputs.empty())
        return "No audio device with an output and an input";
    return "The interface reports " + ms(session_.reportedLatencyMs());
}

juce::String RoundTripPanel::offsetLine() const
{
    return result_ && result_->ok()
               ? "Offset " + juce::String::fromUTF8(model::offsetText(result_->offsetMs).c_str())
               : juce::String();
}

void RoundTripPanel::update()
{
    const bool running = trying_ > 0;
    const bool channels = outputs_.getNumItems() > 0 && inputs_.getNumItems() > 0;
    measure_.setEnabled(!running && channels);
    outputs_.setEnabled(!running);
    inputs_.setEnabled(!running);
    use_.setEnabled(!running && result_ && result_->ok());
    repaint();
}

void RoundTripPanel::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    g.setColour(t.surface2);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), t.radiusPanel);
    const auto text =
        [&g](const juce::String& s, juce::Rectangle<int> area, ui::TextStyle style, juce::Colour colour)
    {
        g.setFont(ui::font(style));
        g.setColour(colour);
        g.drawText(s, area, juce::Justification::centredLeft, true);
    };
    text(kTitle, title_, ui::TextStyle::Title, t.textPrimary);
    text(kHint, hint_, ui::TextStyle::Caption, t.textSecondary);
    text(kOutput, outputCaption_, ui::TextStyle::Caption, t.textSecondary);
    text(kInput, inputCaption_, ui::TextStyle::Caption, t.textSecondary);
    const bool failed = result_ && !result_->ok() && trying_ == 0;
    text(resultText(), resultArea_, ui::TextStyle::Body,
         failed ? t.roles.warning : (result_ && trying_ == 0 ? t.textPrimary : t.textSecondary));
    text(offsetLine(), offset_, ui::TextStyle::Body, t.textPrimary);
}

void RoundTripPanel::resized()
{
    const auto& s = ui::Theme::current().shell;
    auto area = getLocalBounds().reduced(s.groupGap);
    title_ = area.removeFromTop(s.rowHeight);
    hint_ = area.removeFromTop(s.rowHeight);
    area.removeFromTop(s.itemGap);
    // The channels: each a caption and its choice, Measure at the right.
    auto line = area.removeFromTop(s.fieldHeight);
    measure_.setBounds(line.removeFromRight(ui::Toggle::widthFor(measure_.getButtonText())));
    line.removeFromRight(s.groupGap);
    const int outputText = textWidth(ui::TextStyle::Caption, kOutput),
              inputText = textWidth(ui::TextStyle::Caption, kInput);
    const int choice =
        std::max(0, (line.getWidth() - outputText - inputText - 2 * s.itemGap - s.groupGap) / 2);
    outputCaption_ = line.removeFromLeft(outputText);
    line.removeFromLeft(s.itemGap);
    outputs_.setBounds(line.removeFromLeft(choice));
    line.removeFromLeft(s.groupGap);
    inputCaption_ = line.removeFromLeft(inputText);
    line.removeFromLeft(s.itemGap);
    inputs_.setBounds(line.removeFromLeft(choice));
    area.removeFromTop(s.groupGap);
    resultArea_ = area.removeFromTop(s.rowHeight);
    area.removeFromTop(s.itemGap);
    // The offset, Use and Cancel at the right.
    line = area.removeFromTop(s.fieldHeight);
    cancel_.setBounds(line.removeFromRight(ui::Toggle::widthFor(cancel_.getButtonText())));
    line.removeFromRight(s.itemGap);
    use_.setBounds(line.removeFromRight(ui::Toggle::widthFor(use_.getButtonText())));
    line.removeFromRight(s.groupGap);
    offset_ = line;
}
} // namespace fd::app
