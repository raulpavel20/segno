namespace fd::ui
{
namespace
{
juce::String text(const std::string& s)
{
    return juce::String::fromUTF8(s.c_str());
}

std::vector<std::string> splitTags(const juce::String& s)
{
    std::vector<std::string> out;
    for (auto tag : juce::StringArray::fromTokens(s, ",", ""))
        if (tag = tag.trim(); tag.isNotEmpty())
            out.push_back(tag.toStdString());
    return out;
}

juce::String joinTags(const std::vector<std::string>& tags)
{
    juce::StringArray all;
    for (const auto& t : tags)
        all.add(text(t));
    return all.joinIntoString(", ");
}

/// The heading of a preset's group in the list: "Factory", "Yours", "Yours · Drums".
juce::String groupOf(const Preset& p, std::string_view module)
{
    if (!p.isYours())
        return "Factory";
    auto folder = p.folder;
    if (const auto prefix = std::string(module) + "/"; folder.starts_with(prefix))
        folder = folder.substr(prefix.size());
    else if (folder == module)
        folder.clear();
    return folder.empty() ? juce::String("Yours")
                          : "Yours" + juce::String::fromUTF8(" \xC2\xB7 ") + text(folder);
}
} // namespace

juce::File defaultPresetsFolder()
{
    if (const auto custom = juce::SystemStats::getEnvironmentVariable("SEGNO_PRESETS_FOLDER", {});
        custom.isNotEmpty())
        return juce::File(custom);
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("Segno")
        .getChildFile("Presets");
}

// ---- the field -----------------------------------------------------------------------------

PresetField::PresetField()
{
    setName("preset"); // its text is a preset's name: the layout lint doesn't fit it
    setWantsKeyboardFocus(false);
}

void PresetField::setPreset(const juce::String& name, bool modified)
{
    if (name == name_ && modified == modified_)
        return;
    name_ = name;
    modified_ = modified;
    setTooltip(name + (modified ? juce::String(", changed") : juce::String()) +
               ": click for the presets, right-click for the commands");
    repaint();
}

void PresetField::showPopup()
{
    hidePopup(); // no menu of its own: the preset list
    if (onOpen)
        onOpen();
}

void PresetField::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (onMenu)
            onMenu();
        return;
    }
    juce::ComboBox::mouseDown(e);
}

void PresetField::paintOverChildren(juce::Graphics& g)
{
    // Where a combo's text goes (its label, left empty).
    auto area = getLocalBounds();
    for (auto* child : getChildren())
        if (dynamic_cast<juce::Label*>(child) != nullptr)
            area = child->getBounds();
    g.setFont(getLookAndFeel().getComboBoxFont(*this));
    g.setColour(Theme::current().textPrimary);
    paintNameWithMark(g, name_, modified_, area);
}

// ---- the list's rows -----------------------------------------------------------------------

class PresetList::Rows final : public juce::Component
{
public:
    explicit Rows(PresetList& list) : list_(list), renamer_(*this) {}

    struct Row
    {
        int preset = -1; // an index into shown(); -1: a heading
        juce::String heading;
        juce::Rectangle<int> area;
    };

    /// Rows for the presets shown: grouped under headings unless they're search results.
    void build()
    {
        const auto& s = Theme::current().shell;
        rows_.clear();
        const auto module = std::string(list_.bar_.context().info.name);
        const bool grouped = list_.search_.getText().trim().isEmpty();
        juce::String group;
        int y = 0;
        for (size_t i = 0; i < list_.shown_.size(); ++i)
        {
            if (grouped)
                if (const auto g = groupOf(*list_.shown_[i], module); g != group)
                {
                    group = g;
                    rows_.push_back({-1, g, {0, y, getWidth(), s.rowHeight}});
                    y += s.rowHeight;
                }
            rows_.push_back({static_cast<int>(i), {}, {0, y, getWidth(), s.listRowHeight}});
            y += s.listRowHeight;
        }
        height_ = y;
        repaint();
    }
    [[nodiscard]] int totalHeight() const noexcept { return height_; }
    [[nodiscard]] juce::Rectangle<int> areaOf(int preset) const
    {
        for (const auto& r : rows_)
            if (r.preset == preset)
                return r.area;
        return {};
    }
    void resized() override
    {
        for (auto& r : rows_)
            r.area.setWidth(getWidth());
    }

    void paint(juce::Graphics& g) override
    {
        const auto& t = Theme::current();
        const auto clip = g.getClipBounds();
        for (const auto& r : rows_)
        {
            if (!r.area.intersects(clip))
                continue;
            if (r.preset < 0)
            {
                paintCapsLabel(g, r.heading, r.area.withTrimmedLeft(t.shell.listTextInset).toFloat());
                continue;
            }
            const auto& p = *list_.shown_[static_cast<size_t>(r.preset)];
            const bool tooNew = presetTooNew(list_.bar_.context().info, p);
            ListItem item{.name = text(p.name),
                          .caption =
                              tooNew ? juce::String("Needs a newer Segno") : text(PresetList::tagCaption(p)),
                          .enabled = !tooNew,
                          .selected = r.preset == list_.highlighted_,
                          .trailing = t.shell.listIconSize};
            const auto star = paintListRow(g, r.area, item);
            const bool favourite =
                list_.bar_.controller().library().isFavourite(list_.bar_.context().info.id, p.key());
            drawIcon(g, favourite ? Icon::StarFilled : Icon::Star, star.toFloat(),
                     favourite ? t.active : t.textTertiary);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const auto* r = rowAt(e.getPosition());
        if (r == nullptr || r->preset < 0)
            return;
        if (e.mods.isPopupMenu())
        {
            list_.showRowMenu(r->preset);
            return;
        }
        if (starArea(*r).contains(e.getPosition()))
        {
            const auto& p = *list_.shown_[static_cast<size_t>(r->preset)];
            list_.setFavourite(r->preset, !list_.bar_.controller().library().isFavourite(
                                              list_.bar_.context().info.id, p.key()));
            return;
        }
        list_.highlight(r->preset);
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        if (const auto* r = rowAt(e.getPosition());
            r != nullptr && r->preset >= 0 && !starArea(*r).contains(e.getPosition()))
            list_.close(true);
    }

    /// Renames a row in place.
    void startRename(int preset)
    {
        const auto area = areaOf(preset);
        if (area.isEmpty())
            return;
        const auto& p = *list_.shown_[static_cast<size_t>(preset)];
        const auto& s = Theme::current().shell;
        renamer_.show(
            area.withTrimmedLeft(s.listTextInset)
                .withTrimmedRight(s.listTextInset + s.listIconSize)
                .withSizeKeepingCentre(area.getWidth() - 2 * s.listTextInset - s.listIconSize, s.fieldHeight),
            text(p.name), TextStyle::Name,
            [this, preset](const juce::String& name) { list_.rename(preset, name); });
    }
    [[nodiscard]] NameEditor& renamer() noexcept { return renamer_; }

private:
    [[nodiscard]] const Row* rowAt(juce::Point<int> p) const
    {
        for (const auto& r : rows_)
            if (r.area.contains(p))
                return &r;
        return nullptr;
    }
    /// The star's area in a row, where paintListRow keeps room for it.
    [[nodiscard]] static juce::Rectangle<int> starArea(const Row& r)
    {
        const auto& s = Theme::current().shell;
        return r.area.withTrimmedRight(s.listTextInset).removeFromRight(s.listIconSize);
    }

    PresetList& list_;
    std::vector<Row> rows_;
    int height_ = 0;
    NameEditor renamer_;
};

// ---- the list ------------------------------------------------------------------------------

std::string PresetList::tagCaption(const Preset& p)
{
    std::string out;
    for (const auto& t : p.tags)
        out += (out.empty() ? "" : ", ") + t;
    return out;
}

PresetList::PresetList(PresetBar& bar)
    : bar_(bar), rows_(std::make_unique<Rows>(*this)), outside_(*this, [this] { close(true); })
{
    const auto& t = Theme::current();
    auto field = [&](juce::TextEditor& f, const juce::String& empty)
    {
        f.setFont(font(TextStyle::Field));
        f.setTextToShowWhenEmpty(empty, t.textTertiary);
        f.setIndents(t.shell.fieldTextInset, 0);
        f.setJustification(juce::Justification::centredLeft);
        f.setColour(juce::TextEditor::backgroundColourId, t.surface2);
        f.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        f.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        f.addListener(this);
        f.addKeyListener(this);
        addChildComponent(f);
    };
    field(search_, "Search presets");
    field(name_, "Its name");
    field(tags_, "Bus, Subtle");
    field(description_, "What it's for, in a line");
    field(author_, "Who made it");
    search_.setVisible(true);

    favourites_.setClickingTogglesState(true);
    favourites_.setTooltip("Only the starred presets");
    favourites_.onClick = [this] { setFavouritesOnly(favourites_.getToggleState()); };
    addAndMakeVisible(favourites_);

    viewport_.setViewedComponent(rows_.get(), false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);

    for (auto* b : {&save_, &saveAs_, &cancel_, &confirm_})
    {
        b->setClickingTogglesState(false);
        b->setWantsKeyboardFocus(false);
        addChildComponent(*b);
    }
    save_.setVisible(true);
    saveAs_.setVisible(true);
    save_.setTooltip("Write over this preset of yours; a factory preset is saved as one of yours");
    saveAs_.setTooltip("Save this module's settings as a preset of yours");
    save_.onClick = [this]
    {
        if (!bar_.controller().canSave())
        {
            showSaveForm();
            return;
        }
        bar_.controller().save();
        reload();
        bar_.refresh();
    };
    saveAs_.onClick = [this] { showSaveForm(); };
    cancel_.onClick = [this]
    {
        if (formOnly_)
            close(true);
        else
            endForm();
    };
    confirm_.onClick = [this] { confirmForm(); };

    // The field toggles the list itself.
    outside_.ignore = [this](const juce::Component& c)
    { return &c == &bar_.field() || bar_.field().isParentOf(&c); };
    setWantsKeyboardFocus(false);
}

PresetList::~PresetList()
{
    outside_.detach();
    viewport_.setViewedComponent(nullptr, false);
}

void PresetList::show(juce::Component& top, juce::Rectangle<int> anchor)
{
    top_ = &top;
    anchor_ = anchor;
    form_ = formOnly_ = false;
    search_.setText(bar_.filter.query, false);
    favourites_.setToggleState(bar_.filter.favourites, juce::dontSendNotification);
    if (getParentComponent() != &top)
        top.addChildComponent(this);
    endForm();
    setVisible(true);
    toFront(false);
    outside_.attach();
    browsing_ = true;
    bar_.context().presets->beginBrowsing();
    takeFocus(search_);
}

void PresetList::close(bool keep)
{
    if (!isVisible())
        return;
    outside_.detach();
    rows_->renamer().cancel();
    if (std::exchange(browsing_, false))
        bar_.context().presets->endBrowsing(keep);
    setVisible(false);
    if (auto* parent = getParentComponent())
        parent->removeChildComponent(this);
    if (bar_.context().popoverRoom)
        bar_.context().popoverRoom(0);
    top_ = nullptr;
    bar_.refresh();
}

void PresetList::reload()
{
    auto& controller = bar_.controller();
    controller.library().rescan();
    all_ = controller.list();
    tagNames_.clear();
    for (const auto& tag : PresetLibrary::tagsIn(all_))
        tagNames_.push_back(text(tag));
    tagButtons_.clear();
    for (const auto& tag : tagNames_)
    {
        auto& b = *tagButtons_.emplace_back(std::make_unique<Toggle>(tag));
        b.setClickingTogglesState(true);
        b.setWantsKeyboardFocus(false);
        b.setToggleState(bar_.filter.tags.contains(tag.toStdString()), juce::dontSendNotification);
        b.onClick = [this, &b, tag] { setTagFilter(tag, b.getToggleState()); };
        addChildComponent(b);
    }
    // Filters on tags no preset has any more go.
    std::erase_if(bar_.filter.tags,
                  [this](const std::string& tag)
                  {
                      return std::none_of(tagNames_.begin(), tagNames_.end(),
                                          [&](const juce::String& t) { return t.toStdString() == tag; });
                  });
    refilter();
}

void PresetList::refilter()
{
    auto& controller = bar_.controller();
    bar_.filter.query = search_.getText();
    shown_ = bar_.filtered();
    highlighted_ = -1;
    const auto loaded = controller.loaded();
    for (size_t i = 0; i < shown_.size(); ++i)
        if (loaded != nullptr && shown_[i]->key() == loaded->key())
            highlighted_ = static_cast<int>(i);
    rows_->setSize(viewport_.getMaximumVisibleWidth() > 0 ? viewport_.getMaximumVisibleWidth()
                                                          : Theme::current().shell.presetListWidth,
                   rows_->getHeight());
    rows_->build();
    place();
    if (highlighted_ >= 0)
        viewport_.setViewPosition(
            0, std::max(0, rows_->areaOf(highlighted_).getCentreY() - viewport_.getHeight() / 2));
}

std::vector<std::pair<juce::String, bool>> PresetList::tagFilters() const
{
    std::vector<std::pair<juce::String, bool>> out;
    for (const auto& tag : tagNames_)
        out.emplace_back(tag, bar_.filter.tags.contains(tag.toStdString()));
    return out;
}

void PresetList::setTagFilter(const juce::String& tag, bool on)
{
    if (on)
        bar_.filter.tags.insert(tag.toStdString());
    else
        bar_.filter.tags.erase(tag.toStdString());
    for (size_t i = 0; i < tagNames_.size(); ++i)
        if (tagNames_[i] == tag)
            tagButtons_[i]->setToggleState(on, juce::dontSendNotification);
    refilter();
}

void PresetList::setFavouritesOnly(bool on)
{
    bar_.filter.favourites = on;
    favourites_.setToggleState(on, juce::dontSendNotification);
    refilter();
}

void PresetList::setFavourite(int index, bool favourite)
{
    if (index < 0 || index >= static_cast<int>(shown_.size()))
        return;
    bar_.controller().library().setFavourite(bar_.context().info.id,
                                             shown_[static_cast<size_t>(index)]->key(), favourite);
    if (bar_.filter.favourites)
        refilter();
    else
        rows_->repaint();
}

void PresetList::highlight(int index)
{
    if (index < 0 || index >= static_cast<int>(shown_.size()))
        return;
    highlighted_ = index;
    const auto& p = *shown_[static_cast<size_t>(index)];
    if (!presetTooNew(bar_.context().info, p))
        bar_.controller().load(p);
    bar_.refresh();
    rows_->repaint();
    const auto area = rows_->areaOf(index);
    const auto view = viewport_.getViewArea();
    if (area.getY() < view.getY())
        viewport_.setViewPosition(0, area.getY());
    else if (area.getBottom() > view.getBottom())
        viewport_.setViewPosition(0, area.getBottom() - view.getHeight());
}

void PresetList::rename(int index, const juce::String& name)
{
    if (index < 0 || index >= static_cast<int>(shown_.size()))
        return;
    const auto saved = bar_.controller().rename(*shown_[static_cast<size_t>(index)], name.toStdString());
    if (saved.preset == nullptr)
    {
        problem_ = text(saved.problem);
        repaint();
        return;
    }
    problem_.clear();
    reload();
    bar_.refresh();
}

void PresetList::showRowMenu(int index)
{
    if (index < 0 || index >= static_cast<int>(shown_.size()))
        return;
    const auto& p = *shown_[static_cast<size_t>(index)];
    const auto key = p.key();
    const bool favourite = bar_.controller().library().isFavourite(bar_.context().info.id, key);
    juce::PopupMenu menu;
    menu.addItem(favourite ? "Remove from favourites" : "Add to favourites",
                 [self = juce::Component::SafePointer(this), index, favourite]
                 {
                     if (self != nullptr)
                         self->setFavourite(index, !favourite);
                 });
    if (p.isYours())
    {
        menu.addSeparator();
        menu.addItem(juce::String::fromUTF8("Rename\xE2\x80\xA6"),
                     [self = juce::Component::SafePointer(this), index]
                     {
                         if (self != nullptr)
                             self->rows_->startRename(index);
                     });
        menu.addItem("Delete",
                     [self = juce::Component::SafePointer(this), key]
                     {
                         if (self == nullptr)
                             return;
                         if (const auto* found =
                                 self->bar_.controller().library().find(self->bar_.context().info, key))
                             self->bar_.controller().remove(*found);
                         self->reload();
                         self->bar_.refresh();
                     });
        menu.addItem("Show in folder",
                     [self = juce::Component::SafePointer(this), file = p.file]
                     {
                         if (self != nullptr)
                             self->bar_.context().presets->reveal(file.parent_path());
                     });
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
}

// ---- the save form -------------------------------------------------------------------------

void PresetList::showSaveForm()
{
    auto& controller = bar_.controller();
    const auto* loaded = controller.loaded();
    const bool named = loaded != nullptr && (loaded->source == Preset::Source::Factory || loaded->isYours());
    form_ = true;
    formOnly_ = !viewport_.isVisible() || formOnly_;
    replace_ = false;
    problem_.clear();
    name_.setText(named ? text(loaded->name) : juce::String(), false);
    tags_.setText(named ? joinTags(loaded->tags) : juce::String(), false);
    description_.setText(loaded != nullptr && loaded->isYours() ? text(loaded->description) : juce::String(),
                         false);
    author_.setText(loaded != nullptr && loaded->isYours() ? text(loaded->author) : juce::String(), false);
    confirm_.setButtonText("Save");
    for (auto* c :
         std::initializer_list<juce::Component*>{&search_, &favourites_, &viewport_, &save_, &saveAs_})
        c->setVisible(false);
    for (auto& b : tagButtons_)
        b->setVisible(false);
    for (auto* c : std::initializer_list<juce::Component*>{&name_, &tags_, &description_, &author_, &cancel_,
                                                           &confirm_})
        c->setVisible(true);
    place();
    takeFocus(name_);
    name_.selectAll();
}

void PresetList::endForm()
{
    form_ = false;
    replace_ = false;
    problem_.clear();
    for (auto* c : std::initializer_list<juce::Component*>{&name_, &tags_, &description_, &author_, &cancel_,
                                                           &confirm_})
        c->setVisible(false);
    for (auto* c :
         std::initializer_list<juce::Component*>{&search_, &favourites_, &viewport_, &save_, &saveAs_})
        c->setVisible(true);
    reload(); // places it
    if (isVisible())
        takeFocus(search_);
}

void PresetList::confirmForm()
{
    auto saved = bar_.controller().saveAs(name_.getText().trim().toStdString(), splitTags(tags_.getText()),
                                          description_.getText().trim().toStdString(),
                                          author_.getText().trim().toStdString(), replace_);
    if (saved.preset == nullptr)
    {
        if (saved.exists)
        {
            replace_ = true;
            confirm_.setButtonText("Replace");
            problem_ = "\"" + name_.getText().trim() + "\" is taken: Replace writes over it.";
        }
        else
            problem_ = text(saved.problem);
        place();
        repaint();
        return;
    }
    bar_.refresh();
    if (formOnly_)
        close(true);
    else
        endForm();
}

// ---- keys ----------------------------------------------------------------------------------

void PresetList::textEditorTextChanged(juce::TextEditor& editor)
{
    if (&editor == &search_)
        refilter();
    else if (&editor == &name_ && replace_)
    {
        replace_ = false;
        confirm_.setButtonText("Save");
        problem_.clear();
        place();
        repaint();
    }
}

void PresetList::textEditorReturnKeyPressed(juce::TextEditor& editor)
{
    if (&editor == &search_)
        close(true);
    else
        confirmForm();
}

void PresetList::textEditorEscapeKeyPressed(juce::TextEditor& editor)
{
    if (&editor == &search_)
        close(false);
    else if (formOnly_)
        close(true);
    else
        endForm();
}

bool PresetList::keyPressed(const juce::KeyPress& key, juce::Component* origin)
{
    if (origin != &search_ || shown_.empty())
        return false;
    const int n = static_cast<int>(shown_.size());
    if (key == juce::KeyPress::downKey)
        highlight(highlighted_ < 0 ? 0 : std::min(n - 1, highlighted_ + 1));
    else if (key == juce::KeyPress::upKey)
        highlight(highlighted_ < 0 ? n - 1 : std::max(0, highlighted_ - 1));
    else
        return false;
    return true;
}

// ---- layout --------------------------------------------------------------------------------

int PresetList::preferredHeight() const
{
    const auto& s = Theme::current().shell;
    int h = 2 * s.padding;
    if (form_)
    {
        h += s.rowHeight + s.itemGap;         // the title
        h += 4 * (s.fieldHeight + s.itemGap); // name, tags, about, author
        if (problem_.isNotEmpty())
            h += s.rowHeight + s.itemGap; // why not
        return h + s.rowHeight;           // the buttons
    }
    const int list = std::clamp(rows_->totalHeight(), s.listRowHeight, s.presetListRows * s.listRowHeight);
    return h + s.fieldHeight + s.itemGap + s.rowHeight + s.itemGap + list + s.itemGap + s.rowHeight +
           (problem_.isNotEmpty() ? s.rowHeight + s.itemGap : 0);
}

juce::Rectangle<int> PresetList::inner() const
{
    return getLocalBounds().reduced(juce::roundToInt(Theme::current().shell.shadowRadius));
}

void PresetList::place()
{
    if (top_ == nullptr)
        return;
    const auto& s = Theme::current().shell;
    const int margin = juce::roundToInt(s.shadowRadius);
    const int width = s.presetListWidth;
    int height = preferredHeight();
    auto room = [&](juce::Rectangle<int> area)
    {
        return std::pair{area.getBottom() - (anchor_.getBottom() + s.itemGap),
                         anchor_.getY() - s.itemGap - area.getY()};
    };
    auto area = top_->getLocalBounds();
    auto [below, above] = room(area);
    if (below < height && above < height && bar_.context().popoverRoom)
    {
        bar_.context().popoverRoom(height + s.itemGap); // a plugin's editor grows to hold it
        area = top_->getLocalBounds();
        std::tie(below, above) = room(area);
    }
    const bool under = below >= height || below >= above;
    height = std::min(height, std::max(under ? below : above, s.listRowHeight));
    const int y = under ? anchor_.getBottom() + s.itemGap : anchor_.getY() - s.itemGap - height;
    const int x =
        std::clamp(anchor_.getRight() - width, area.getX(), std::max(area.getX(), area.getRight() - width));
    setBounds(juce::Rectangle<int>(x, y, width, height).expanded(margin));
    resized();
}

void PresetList::layoutTags()
{
    const auto& s = Theme::current().shell;
    auto row = inner().reduced(s.padding).withTrimmedTop(s.fieldHeight + s.itemGap).withHeight(s.rowHeight);
    const int option = favourites_.preferredWidth();
    favourites_.setBounds(row.removeFromRight(option));
    row.removeFromRight(s.groupGap);
    for (auto& b : tagButtons_)
    {
        const int w = Toggle::widthFor(b->getButtonText());
        const bool fits = !form_ && w <= row.getWidth();
        b->setVisible(fits);
        if (fits)
        {
            b->setBounds(row.removeFromLeft(w));
            row.removeFromLeft(s.itemGap);
        }
        else
            row.setWidth(0); // the rest wait their turn: tags in order of use
    }
}

void PresetList::resized()
{
    const auto& s = Theme::current().shell;
    auto area = inner().reduced(s.padding);
    if (form_)
    {
        area.removeFromTop(s.rowHeight + s.itemGap); // the title
        for (auto* f : {&name_, &tags_, &description_, &author_})
        {
            f->setBounds(area.removeFromTop(s.fieldHeight).withTrimmedLeft(s.propertyLabelWidth));
            area.removeFromTop(s.itemGap);
        }
        auto buttons = area.removeFromBottom(s.rowHeight);
        const int confirm = std::max(Toggle::widthFor("Replace"), Toggle::widthFor("Save"));
        confirm_.setBounds(buttons.removeFromRight(confirm));
        buttons.removeFromRight(s.itemGap);
        cancel_.setBounds(buttons.removeFromRight(Toggle::widthFor(cancel_.getButtonText())));
        return;
    }
    search_.setBounds(area.removeFromTop(s.fieldHeight));
    area.removeFromTop(s.itemGap);
    area.removeFromTop(s.rowHeight); // the tags
    layoutTags();
    area.removeFromTop(s.itemGap);
    auto footer = area.removeFromBottom(s.rowHeight);
    area.removeFromBottom(s.itemGap);
    if (problem_.isNotEmpty())
        area.removeFromBottom(s.rowHeight + s.itemGap);
    saveAs_.setBounds(footer.removeFromLeft(Toggle::widthFor(saveAs_.getButtonText())));
    footer.removeFromLeft(s.itemGap);
    save_.setBounds(footer.removeFromLeft(Toggle::widthFor(save_.getButtonText())));
    viewport_.setBounds(area);
    rows_->setSize(viewport_.getMaximumVisibleWidth(), std::max(rows_->totalHeight(), 1));
}

void PresetList::paint(juce::Graphics& g)
{
    const auto& t = Theme::current();
    const auto& s = t.shell;
    paintPopover(g, inner());
    auto area = inner().reduced(s.padding);
    if (form_)
    {
        paintCapsLabel(g, "Save preset", area.removeFromTop(s.rowHeight).toFloat());
        area.removeFromTop(s.itemGap);
        g.setFont(font(TextStyle::Label));
        g.setColour(t.textSecondary);
        for (const auto* label : {"Name", "Tags", "About", "Author"})
        {
            g.drawText(label, area.removeFromTop(s.fieldHeight).withWidth(s.propertyLabelWidth),
                       juce::Justification::centredLeft, false);
            area.removeFromTop(s.itemGap);
        }
    }
    // Why not, a line above the buttons.
    area.removeFromBottom(s.rowHeight + s.itemGap);
    if (problem_.isNotEmpty())
    {
        g.setFont(font(TextStyle::Caption));
        g.setColour(t.roles.warning);
        g.drawText(problem_, area.removeFromBottom(s.rowHeight), juce::Justification::centredLeft, true);
    }
}

// ---- the bar -------------------------------------------------------------------------------

PresetBar::PresetBar(DeviceUiContext& context)
    : context_(context), controller_(context.info, *context.presets),
      list_(std::make_unique<PresetList>(*this))
{
    previous_.setTooltip("Previous preset");
    next_.setTooltip("Next preset");
    for (auto* b : {&previous_, &next_})
    {
        b->setWantsKeyboardFocus(false);
        addAndMakeVisible(*b);
    }
    previous_.onClick = [this] { step(-1); };
    next_.onClick = [this] { step(+1); };
    field_.onOpen = [this]
    {
        if (isListOpen())
            closeList(true);
        else
            openList();
    };
    field_.onMenu = [this]
    {
        juce::PopupMenu menu;
        addCommands(menu);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&field_));
    };
    addAndMakeVisible(field_);
    compare_.setClickingTogglesState(false);
    compare_.setWantsKeyboardFocus(false);
    compare_.setTooltip("Compare: switch between two settings, A and B (Copy is in the preset's menu)");
    compare_.onClick = [this]
    {
        controller_.switchCompare();
        refresh();
    };
    addAndMakeVisible(compare_);
    refresh();
}

PresetBar::~PresetBar()
{
    closeList(true);
}

int PresetBar::compareWidth() const
{
    return std::max(Toggle::widthFor("A"), Toggle::widthFor("B"));
}

int PresetBar::minimumWidth() const
{
    const auto& t = Theme::current();
    return 2 * t.deviceGrid.headerButton + t.deviceGrid.presetFieldMin + 3 * t.shell.itemGap + compareWidth();
}

int PresetBar::maximumWidth() const
{
    return minimumWidth() + Theme::current().deviceGrid.presetFieldMax -
           Theme::current().deviceGrid.presetFieldMin;
}

void PresetBar::refresh()
{
    // The mark compares the whole state: a few times a second is enough, and at once after a
    // change made here.
    const bool check = ticks_++ % 8 == 0;
    field_.setPreset(text(controller_.loadedName()), check ? controller_.modified() : field_.isModified());
    const bool onB = controller_.onB();
    compare_.setButtonText(onB ? "B" : "A");
    compare_.setToggleState(onB, juce::dontSendNotification);
}

bool PresetBar::isListOpen() const
{
    return list_->isVisible() && list_->getParentComponent() != nullptr;
}

void PresetBar::openList(bool saveAs)
{
    if (!isListOpen())
    {
        auto* top = getTopLevelComponent();
        if (auto* window = dynamic_cast<juce::ResizableWindow*>(top))
            top = window->getContentComponent();
        if (top == nullptr)
            return;
        list_->show(*top, top->getLocalArea(&field_, field_.getLocalBounds()));
    }
    if (saveAs)
        list_->showSaveForm();
}

void PresetBar::closeList(bool keep)
{
    list_->close(keep);
}

std::vector<const Preset*> PresetBar::filtered() const
{
    return controller_.library().filter(controller_.list(), context_.info.id, filter.query.toStdString(),
                                        filter.tags, filter.favourites);
}

void PresetBar::step(int direction)
{
    auto list = filtered();
    if (list.empty())
        list = controller_.list();
    controller_.step(list, direction);
    ticks_ = 0;
    refresh();
}

void PresetBar::addCommands(juce::PopupMenu& menu)
{
    const juce::Component::SafePointer<PresetBar> self(this);
    auto run = [self](std::function<void(PresetBar&)> f)
    {
        return [self, f]
        {
            if (self != nullptr)
            {
                f(*self);
                self->ticks_ = 0;
                self->refresh();
            }
        };
    };
    const bool onB = controller_.onB();
    menu.addItem("Previous preset", run([](PresetBar& b) { b.step(-1); }));
    menu.addItem("Next preset", run([](PresetBar& b) { b.step(+1); }));
    menu.addSeparator();
    menu.addItem("Save preset", run(
                                    [](PresetBar& b)
                                    {
                                        if (b.controller_.canSave())
                                            b.controller_.save();
                                        else
                                            b.openList(true);
                                    }));
    menu.addItem(juce::String::fromUTF8("Save preset as\xE2\x80\xA6"),
                 run([](PresetBar& b) { b.openList(true); }));
    menu.addItem("Revert to preset", controller_.modified(), false,
                 run([](PresetBar& b) { b.controller_.revert(); }));
    menu.addSeparator();
    menu.addItem("Compare A/B", true, onB, run([](PresetBar& b) { b.controller_.switchCompare(); }));
    menu.addItem(onB ? "Copy B to A" : "Copy A to B", run([](PresetBar& b) { b.controller_.copyCompare(); }));
    menu.addSeparator();
    menu.addItem("Use as the default for a new " + text(std::string(context_.info.name)),
                 run([](PresetBar& b) { b.controller_.useAsDefault(); }));
    menu.addItem("Clear the default", controller_.hasDefault(), false,
                 run([](PresetBar& b) { b.controller_.clearDefault(); }));
    menu.addSeparator();
    menu.addItem("Show presets folder",
                 run([](PresetBar& b) { b.context_.presets->reveal(b.controller_.library().root()); }));
}

void PresetBar::resized()
{
    const auto& t = Theme::current();
    const int button = t.deviceGrid.headerButton, gap = t.shell.itemGap, row = t.deviceGrid.row;
    auto r = getLocalBounds();
    compare_.setBounds(r.removeFromRight(compareWidth()).withSizeKeepingCentre(compareWidth(), row));
    r.removeFromRight(gap);
    next_.setBounds(r.removeFromRight(button).withSizeKeepingCentre(button, button));
    r.removeFromRight(gap);
    previous_.setBounds(r.removeFromLeft(button).withSizeKeepingCentre(button, button));
    r.removeFromLeft(gap);
    field_.setBounds(r.withSizeKeepingCentre(r.getWidth(), row));
}
} // namespace fd::ui
