#include "ui/AssistantsPopover.h"

namespace fd::app
{
namespace
{
constexpr int kMaxClients = 4;

/// A text button's width: its label in the control style, with the bar's padding each side.
int buttonWidth(const juce::String& text)
{
    const auto& sh = ui::Theme::current().shell;
    return static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(ui::font(ui::TextStyle::Control), text))) +
           2 * sh.barPadding;
}

juce::String ago(double lastSeen)
{
    const double seconds = juce::Time::getMillisecondCounterHiRes() * 0.001 - lastSeen;
    if (seconds < 5.0)
        return "active now";
    if (seconds < 90.0)
        return juce::String(juce::roundToInt(seconds)) + " s ago";
    return juce::String(juce::roundToInt(seconds / 60.0)) + " min ago";
}
} // namespace

AssistantsPopover::AssistantsPopover(mcp::McpServer& server) : server_(server)
{
    setWantsKeyboardFocus(true);
    // The transport button toggles it itself.
    outside_.ignore = [](const juce::Component& c) {
        return dynamic_cast<const ui::IconButton*>(&c) != nullptr && c.getName() == "assistants";
    };
    on_.setClickingTogglesState(true);
    on_.setTooltip("Let assistants connect to this DAW (MCP)");
    on_.onClick = [this] {
        server_.setEnabled(on_.getToggleState());
        failed_ = on_.getToggleState() && !server_.isRunning();
        if (failed_ && onMessage)
            onMessage("The assistant server couldn't start: no free port from " +
                      juce::String(server_.settings().port) + " on");
        refresh();
    };
    readOnly_.setClickingTogglesState(true);
    readOnly_.setTooltip("Assistants can read, render and measure, but not change the project");
    readOnly_.onClick = [this] { server_.setReadOnly(readOnly_.getToggleState()); };
    for (auto* line : {&httpLine_, &stdioLine_})
    {
        line->setReadOnly(true);
        line->setCaretVisible(false);
        line->setFont(ui::font(ui::TextStyle::ValueSmall));
        line->setScrollbarsShown(false);
        addAndMakeVisible(line);
    }
    copyHttp_.setTooltip("Copy the command that adds this DAW to Claude Code");
    copyHttp_.onClick = [this] { juce::SystemClipboard::copyTextToClipboard(server_.httpSetupLine()); };
    copyStdio_.setTooltip("Copy the configuration for apps that start the DAW themselves (Claude Desktop)");
    copyStdio_.onClick = [this] { juce::SystemClipboard::copyTextToClipboard(server_.stdioSetupLine()); };
    newToken_.setTooltip("Disconnects every assistant: each must be set up again with the new line");
    newToken_.onClick = [this] {
        server_.regenerateToken();
        refresh();
    };
    for (juce::Component* c : {static_cast<juce::Component*>(&on_), static_cast<juce::Component*>(&readOnly_),
                               static_cast<juce::Component*>(&copyHttp_), static_cast<juce::Component*>(&copyStdio_),
                               static_cast<juce::Component*>(&newToken_)})
    {
        c->setWantsKeyboardFocus(false);
        addAndMakeVisible(c);
    }
    refresh();
}

AssistantsPopover::~AssistantsPopover() = default;

void AssistantsPopover::open(juce::Component& parent, juce::Rectangle<int> anchor)
{
    if (getParentComponent() != &parent)
        parent.addChildComponent(this);
    refresh();
    const int w = ui::Theme::current().shell.popoverWidth;
    setBounds(juce::Rectangle<int>(anchor.getRight() - w, anchor.getBottom() + ui::Theme::current().shell.itemGap, w,
                                   preferredHeight())
                  .constrainedWithin(parent.getLocalBounds()));
    setVisible(true);
    toFront(true);
    outside_.attach();
}

void AssistantsPopover::close()
{
    outside_.detach();
    if (!isVisible())
        return;
    setVisible(false);
    if (auto* parent = getParentComponent())
        parent->grabKeyboardFocus();
}

bool AssistantsPopover::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        close();
        return true;
    }
    return false;
}

void AssistantsPopover::refresh()
{
    const bool running = server_.isRunning();
    on_.setToggleState(server_.settings().enabled, juce::dontSendNotification);
    readOnly_.setToggleState(server_.settings().readOnly, juce::dontSendNotification);
    if (running)
        failed_ = false;
    httpLine_.setText(server_.httpSetupLine(), false);
    stdioLine_.setText(server_.stdioSetupLine(), false);
    httpLine_.setCaretPosition(0); // show where the line starts
    stdioLine_.setCaretPosition(0);
    for (auto* c : {static_cast<juce::Component*>(&httpLine_), static_cast<juce::Component*>(&stdioLine_),
                    static_cast<juce::Component*>(&copyHttp_), static_cast<juce::Component*>(&copyStdio_)})
        c->setEnabled(running);
    if (isVisible())
    {
        setSize(getWidth(), preferredHeight());
        resized();
    }
    repaint();
}

juce::String AssistantsPopover::status() const
{
    if (server_.isRunning())
    {
        const auto clients = server_.clients().size();
        return "Listening on 127.0.0.1:" + juce::String(server_.port()) +
               (server_.settings().readOnly ? ", read only" : juce::String()) + juce::String::fromUTF8(" \xc2\xb7 ") +
               (clients == 0 ? juce::String("no assistant yet") : juce::String(clients) + (clients == 1 ? " assistant" : " assistants"));
    }
    if (failed_)
        return "Couldn't start: no free port from " + juce::String(server_.settings().port) + " on";
    return "Off: assistants can't connect";
}

int AssistantsPopover::preferredHeight() const
{
    const auto& sh = ui::Theme::current().shell;
    const int clients = std::clamp(static_cast<int>(server_.clients().size()), 1, kMaxClients);
    return sh.padding * 2 + sh.buttonSize + sh.rowHeight          // title, status
           + 4 * (sh.groupGap + 1)                                // separators
           + sh.fieldHeight                                       // read only
           + 2 * (sh.rowHeight + sh.fieldHeight + sh.itemGap)     // setup lines
           + sh.rowHeight + clients * sh.listRowHeight            // clients
           + sh.fieldHeight;                                      // token
}

void AssistantsPopover::resized()
{
    const auto& sh = ui::Theme::current().shell;
    auto r = getLocalBounds().reduced(sh.padding);
    separators_.clear();
    const auto separator = [&] {
        r.removeFromTop(sh.groupGap / 2);
        separators_.push_back(r.getY());
        r.removeFromTop(1 + sh.groupGap - sh.groupGap / 2);
    };

    auto head = r.removeFromTop(sh.buttonSize);
    on_.setBounds(head.removeFromRight(buttonWidth("Off")).withSizeKeepingCentre(buttonWidth("Off"), sh.fieldHeight));
    title_ = head;
    status_ = r.removeFromTop(sh.rowHeight);
    separator();

    auto ro = r.removeFromTop(sh.fieldHeight);
    readOnly_.setBounds(ro.removeFromLeft(buttonWidth(readOnly_.getButtonText())));
    ro.removeFromLeft(sh.groupGap);
    readOnlyHint_ = ro;
    separator();

    const auto setupLine = [&](juce::Rectangle<int>& label, juce::TextEditor& line, juce::TextButton& copy) {
        label = r.removeFromTop(sh.rowHeight);
        auto row = r.removeFromTop(sh.fieldHeight);
        copy.setBounds(row.removeFromRight(buttonWidth(copy.getButtonText())));
        row.removeFromRight(sh.itemGap);
        line.setBounds(row);
        r.removeFromTop(sh.itemGap);
    };
    setupLine(httpLabel_, httpLine_, copyHttp_);
    setupLine(stdioLabel_, stdioLine_, copyStdio_);
    separator();

    clientsLabel_ = r.removeFromTop(sh.rowHeight);
    const int clients = std::clamp(static_cast<int>(server_.clients().size()), 1, kMaxClients);
    clients_ = r.removeFromTop(clients * sh.listRowHeight);
    separator();

    auto token = r.removeFromTop(sh.fieldHeight);
    newToken_.setBounds(token.removeFromLeft(buttonWidth(newToken_.getButtonText())));
    token.removeFromLeft(sh.groupGap);
    tokenHint_ = token;
}

void AssistantsPopover::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    ui::paintPopover(g, getLocalBounds());
    ui::paintCapsLabel(g, "Assistants", title_.toFloat());
    g.setFont(ui::font(ui::TextStyle::Caption));
    g.setColour(server_.isRunning() ? t.textSecondary : t.textTertiary);
    g.drawText(status(), status_, juce::Justification::centredLeft, true);

    g.setColour(t.textTertiary);
    g.drawText("Assistants can read, render and measure, but not edit", readOnlyHint_, juce::Justification::centredLeft, true);
    g.drawText("Every assistant must be set up again", tokenHint_, juce::Justification::centredLeft, true);

    ui::paintCapsLabel(g, "Claude Code (HTTP)", httpLabel_.toFloat());
    ui::paintCapsLabel(g, "Apps that start the DAW (stdio)", stdioLabel_.toFloat());
    ui::paintCapsLabel(g, "Connected", clientsLabel_.toFloat());

    const auto clients = server_.clients();
    auto rows = clients_;
    if (clients.empty())
        ui::paintListRow(g, rows.removeFromTop(t.shell.listRowHeight), {.name = "No assistant has connected", .enabled = false});
    for (size_t i = 0; i < clients.size() && i < static_cast<size_t>(kMaxClients); ++i)
        ui::paintListRow(g, rows.removeFromTop(t.shell.listRowHeight),
                         {.name = juce::String(clients[i].name),
                          .caption = juce::String(clients[i].version),
                          .shortcut = ago(clients[i].lastSeen),
                          .toggled = juce::Time::getMillisecondCounterHiRes() * 0.001 - clients[i].lastSeen < 5.0});

    g.setColour(t.line);
    for (int y : separators_)
        g.fillRect(t.shell.padding, y, getWidth() - 2 * t.shell.padding, 1);
}
} // namespace fd::app
