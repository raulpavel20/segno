namespace fd::ui
{
MidiEffectPanel::MidiEffectPanel(DeviceUiContext& context) : DevicePanel(context)
{
    setSidechainCaption(
        "Listen", juce::String::fromUTF8("The track whose notes it hears: what Follow \xE2\x96\xB8 Track "
                                         "follows, and what it answers"));
}

NoteDisplay& MidiEffectPanel::addNotesSection(int columns)
{
    auto& section = addSection("notes", "Notes", columns);
    auto& display = own(std::make_unique<NoteDisplay>(context().notes));
    section.place(display, 0, 0, columns, 6);
    displays_.push_back(&display);
    display.refresh();
    return display;
}

StepDisplay& MidiEffectPanel::addStepsSection(int columns, const juce::StringArray& lanes, StepFeed* steps)
{
    auto& section = addSection("steps", "Steps", columns);
    auto& chooser = own(std::make_unique<Selector>(lanes, lanes.size()));
    section.place(chooser, 0, 0, lanes.size(), 1, PanelSection::Kind::Line);
    auto& display = own(std::make_unique<StepDisplay>(context().notes, steps));
    section.place(display, 0, 1, columns, 5);
    chooser.onSelect = [&display](int index) { display.setLane(index); };
    displays_.push_back(&display);
    display.refresh();
    return display;
}

PanelSection& MidiEffectPanel::addHarmonySection(std::size_t first)
{
    auto& section = addSection("harmony", "Harmony", 3);
    auto& follow = section.combo(first + 0, 0, 0, 3);
    auto& key = section.combo(first + 1, 0, 1, 3);
    follow.setInlineName(true);
    key.setInlineName(true);
    auto& root = section.bar(first + 2, 0, 2, 3);
    auto& scale = section.combo(first + 3, 0, 3, 3);
    auto& chord = section.combo(first + 4, 0, 4, 3);
    chord.setInlineName(true);
    auto& split = section.bar(first + 5, 0, 5, 3);
    auto& params = context().params;
    const auto is = [&params](std::size_t p, int choice)
    { return [&params, p, choice] { return std::lround(params.getPlain(p)) == choice; }; };
    // Its own key while Key is Own; its own chord (and so the root) while Follow is Own; the split
    // while Follow is Played.
    const auto ownKey = is(first + 1, 1), ownChord = is(first + 0, 3), played = is(first + 0, 2);
    dimWhen({&root}, [ownKey, ownChord] { return !ownKey() && !ownChord(); });
    dimWhen({&scale}, [ownKey] { return !ownKey(); });
    dimWhen({&chord}, [ownChord] { return !ownChord(); });
    dimWhen({&split}, [played] { return !played(); });
    section.setCollapsedByDefault();
    // Listen, while what it hears is used.
    for (auto* c : headerControls())
        if (c->getName() == "sidechain")
            showHeaderWhen(*c, [this, followsTrack = is(first + 0, 1)]
                           { return followsTrack() || (listenAlso_ && listenAlso_()); });
    return section;
}

Toggle& MidiEffectPanel::addButton(PanelSection& section, const juce::String& text,
                                   const juce::String& tooltip, int column, int row, int columns)
{
    auto& b = own(std::make_unique<Toggle>(text));
    b.setClickingTogglesState(false);
    b.setTooltip(tooltip);
    section.place(b, column, row, columns, 1, PanelSection::Kind::Line);
    return b;
}

void MidiEffectPanel::tick()
{
    if (isShowing())
        for (auto* d : displays_)
            d->refresh();
}
} // namespace fd::ui
