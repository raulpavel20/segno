#include "ui/LaneAssignments.h"

namespace fd::app
{
LaneAssignments::LaneAssignments(engine::Session& session) : session_(session)
{
    assign_.setClickingTogglesState(false);
    assign_.setTooltip("Make this lane drive another parameter, on any track (Ctrl+Alt+Shift+L)");
    assign_.onClick = [this] {
        if (lane_ && onAssign)
            onAssign(lane_->id);
    };
    addChildComponent(assign_);
}

void LaneAssignments::setLane(const std::optional<model::AutomationLaneInfo>& lane)
{
    auto targets = [](const std::optional<model::AutomationLaneInfo>& l) {
        std::vector<std::pair<std::uint64_t, std::string>> out;
        if (l)
            for (const auto& a : l->assignments)
                out.emplace_back(a.channel, a.key);
        return out;
    };
    const bool rebuild = lane.has_value() != lane_.has_value() || (lane && lane_ && lane->id != lane_->id) ||
                         targets(lane) != targets(lane_);
    lane_ = lane;
    assign_.setVisible(lane_.has_value());
    if (rebuild)
    {
        rows_.clear();
        if (lane_)
            build(*lane_);
        resized();
    }
    if (!lane_)
        return;
    // Values only: the fields and switches keep their place while the lane changes.
    for (size_t i = 0; i < rows_.size() && i < lane_->assignments.size(); ++i)
    {
        const auto& a = lane_->assignments[i];
        auto& r = *rows_[i];
        r.on.setToggleState(a.on, juce::dontSendNotification);
        if (!r.gesture)
        {
            r.from.setValue(a.range.from, juce::dontSendNotification);
            r.to.setValue(a.range.to, juce::dontSendNotification);
        }
    }
    repaint();
}

void LaneAssignments::build(const model::AutomationLaneInfo& lane)
{
    for (const auto& a : lane.assignments)
    {
        auto r = std::make_unique<Row>();
        r->target = {a.channel, a.key};
        r->module = a.device;
        r->label = juce::String::fromUTF8((a.channelName + " \xE2\x80\xBA " + a.name).c_str());
        r->on.setClickingTogglesState(false); // the snapshot says what it is
        r->on.setWantsKeyboardFocus(false);
        r->on.setTooltip("On: it follows the lane. Off: it keeps the value set by hand");
        r->on.onClick = [this, row = r.get()] { session_.setAssignmentOn(row->target, !row->on.getToggleState()); };
        r->remove.setTooltip("Take it off this lane: it keeps its value now");
        r->remove.onClick = [this, target = r->target] { session_.unassign(target); };
        setupRange(*r, r->from, true);
        setupRange(*r, r->to, false);
        for (juce::Component* c : {static_cast<juce::Component*>(&r->on), static_cast<juce::Component*>(&r->remove),
                                   static_cast<juce::Component*>(&r->from), static_cast<juce::Component*>(&r->to)})
            addAndMakeVisible(c);
        rows_.push_back(std::move(r));
    }
}

void LaneAssignments::setupRange(Row& row, ui::NumberField& field, bool isFrom)
{
    field.setRange(0.0, 1.0, 0.0);
    field.setDragSensitivity(0.005);
    field.setTooltip(isFrom ? "Its value at the lane's bottom: drag or type" : "Its value at the lane's top: drag or type");
    field.textFromValue = [this, target = row.target](double v) {
        return juce::String::fromUTF8(session_.paramText(target, static_cast<float>(v)).c_str());
    };
    field.valueFromText = [this, target = row.target](const juce::String& text) -> std::optional<double> {
        if (const auto v = session_.paramFromText(target, text.toStdString()))
            return *v;
        return std::nullopt;
    };
    field.onGestureStart = [this, r = &row] { r->gesture.emplace(session_, "Assignment range"); };
    field.onGestureEnd = [r = &row] { r->gesture.reset(); };
    field.onValueChange = [this, r = &row, isFrom](double v) {
        if (!lane_)
            return;
        const auto* a = [&]() -> const model::AssignmentInfo* {
            for (const auto& x : lane_->assignments)
                if (x.channel == r->target.channel && x.key == r->target.key)
                    return &x;
            return nullptr;
        }();
        if (a == nullptr)
            return;
        auto range = a->range;
        (isFrom ? range.from : range.to) = static_cast<float>(v);
        session_.setAssignmentRange(r->target, range);
    };
}

int LaneAssignments::preferredHeight() const
{
    if (!lane_)
        return 0;
    const auto& s = ui::Theme::current().shell;
    // Two rows each (the parameter, its range), a gap between them, then the assign button
    // (under a hint while there are none).
    return rows_.empty() ? 2 * s.rowHeight : static_cast<int>(rows_.size()) * (2 * s.rowHeight + s.itemGap) + s.rowHeight;
}

void LaneAssignments::resized()
{
    const auto& s = ui::Theme::current().shell;
    auto r = getLocalBounds();
    for (auto& row : rows_)
    {
        auto top = r.removeFromTop(s.rowHeight);
        row->on.setBounds(top.removeFromLeft(s.rowHeight).reduced(4));
        top.removeFromLeft(s.itemGap);
        row->remove.setBounds(top.removeFromRight(s.rowHeight));
        top.removeFromRight(s.itemGap);
        row->labelArea = top;
        // The range under the name: "10 Hz – 30.0 kHz".
        auto range = r.removeFromTop(s.rowHeight).withTrimmedLeft(s.rowHeight + s.itemGap);
        const int dash = 12;
        const int w = (range.getWidth() - dash) / 2;
        row->from.setBounds(range.removeFromLeft(w));
        range.removeFromLeft(dash);
        row->to.setBounds(range);
        r.removeFromTop(s.itemGap);
    }
    if (rows_.empty())
        hintArea_ = r.removeFromTop(s.rowHeight);
    assign_.setBounds(r.removeFromTop(s.rowHeight));
}

void LaneAssignments::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    for (const auto& row : rows_)
    {
        g.setColour(row->on.getToggleState() ? t.textPrimary : t.textSecondary);
        g.setFont(ui::font(ui::TextStyle::Body));
        g.drawText(row->label, row->labelArea, juce::Justification::centredLeft, true);
        g.setColour(t.textTertiary);
        g.setFont(ui::font(ui::TextStyle::Caption));
        g.drawText(juce::String::fromUTF8("\xE2\x80\x93"), row->from.getBounds().withX(row->from.getRight()).withWidth(12),
                   juce::Justification::centred, false);
    }
    if (lane_ && rows_.empty())
    {
        g.setColour(t.textTertiary);
        g.setFont(ui::font(ui::TextStyle::Caption));
        g.drawText("Not assigned yet: it drives nothing", hintArea_, juce::Justification::centredLeft, true);
    }
}

void LaneAssignments::mouseDown(const juce::MouseEvent& e)
{
    for (const auto& row : rows_)
        if (row->labelArea.contains(e.getPosition()) && onShowParameter)
            onShowParameter(row->target.channel, row->module);
}
} // namespace fd::app
