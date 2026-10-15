namespace fd::ui
{
namespace
{
constexpr int kFoldButton = 16;
constexpr int kSettingsGap = 8;
constexpr int kFoldedFoot = 8; // the folded strip's padding at its foot

juce::Font nameFont()
{
    return font(TextStyle::Title);
}

juce::Font captionFont()
{
    return font(TextStyle::Caption);
}

constexpr int kCaptionGap = 6;

int roundUpToGrid(float px)
{
    const int grid = Theme::current().grid;
    return (static_cast<int>(std::ceil(px)) + grid - 1) / grid * grid;
}

/// The sidechain picker (ADR 0025): the host's sources, kept up to date (tracks come, go and
/// are renamed). A fixed width, since track names are any length.
class SidechainPicker final : public juce::ComboBox, public ParamControl
{
public:
    static constexpr int width = 128;

    explicit SidechainPicker(SidechainRouting& routing) : routing_(routing)
    {
        setName("sidechain"); // its choices are track names: the layout lint doesn't fit them
        setTooltip("Sidechain: what the module listens to instead of its own input");
        onChange = [this] {
            if (const int id = getSelectedId(); id > 0 && id - 1 != routing_.selected())
                routing_.select(id - 1);
        };
        refresh();
    }

    void refresh() override
    {
        if (isPopupActive())
            return;
        if (auto sources = routing_.sources(); sources != sources_)
        {
            sources_ = std::move(sources);
            clear(juce::dontSendNotification);
            for (size_t i = 0; i < sources_.size(); ++i)
                addItem(juce::String(sources_[i]), static_cast<int>(i) + 1);
        }
        setSelectedId(routing_.selected() + 1, juce::dontSendNotification);
    }

private:
    SidechainRouting& routing_;
    std::vector<std::string> sources_;
};

/// A header setting's width: a combo fits its widest choice and its chevron.
int settingWidth(juce::Component& c)
{
    if (dynamic_cast<SidechainPicker*>(&c) != nullptr)
        return SidechainPicker::width;
    if (auto* badge = dynamic_cast<LatencyBadge*>(&c))
        return badge->preferredWidth(); // none while there's no delay
    if (auto* readout = dynamic_cast<HeaderReadout*>(&c))
        return roundUpToGrid(static_cast<float>(readout->preferredWidth()));
    if (auto* combo = dynamic_cast<juce::ComboBox*>(&c))
    {
        float widest = 0.0f;
        for (int i = 0; i < combo->getNumItems(); ++i)
            widest = std::max(widest, juce::GlyphArrangement::getStringWidth(c.getLookAndFeel().getComboBoxFont(*combo),
                                                                              combo->getItemText(i)));
        return roundUpToGrid(widest + 26.0f);
    }
    return c.getWidth();
}

/// The room a header setting keeps in the panel's width, shown or not: a latency badge as for
/// a delay of up to 999 ms.
int reservedWidth(juce::Component& c)
{
    if (dynamic_cast<LatencyBadge*>(&c) != nullptr)
        return LatencyBadge::widthFor("+999 ms");
    return settingWidth(c);
}
} // namespace

DevicePanel::DevicePanel(DeviceUiContext& context) : context_(context)
{
    power_.setToggleState(true, juce::dontSendNotification);
    power_.setTooltip("Module on/off");
    power_.onClick = [this] {
        if (onEnabledChange)
            onEnabledChange(power_.getToggleState());
    };
    addAndMakeVisible(power_);
    fold_.setTooltip("Fold the module");
    fold_.setWantsKeyboardFocus(false);
    fold_.onClick = [this] { setFolded(!folded_, true); };
    addChildComponent(fold_);
    // A device with a sidechain bus offers the host's sources in the header (ADR 0025).
    if (context_.sidechain != nullptr && context_.info.buses.sidechainChannels > 0)
    {
        headerControls_.push_back(&adopt(std::make_unique<SidechainPicker>(*context_.sidechain), *this));
        headerCaptions_.push_back("Sidechain");
        headerShown_.emplace_back();
    }
    // Presets, where the host has them (ADR 0044).
    if (context_.presets != nullptr)
    {
        presetBar_ = std::make_unique<PresetBar>(context_);
        addAndMakeVisible(*presetBar_);
    }
    startTimerHz(30);
}

void DevicePanel::setSidechainCaption(juce::String caption, juce::String tooltip)
{
    for (size_t i = 0; i < headerControls_.size(); ++i)
        if (auto* picker = dynamic_cast<SidechainPicker*>(headerControls_[i]))
        {
            headerCaptions_[i] = caption;
            picker->setTooltip(tooltip);
            resized();
            repaint();
        }
}

DevicePanel::~DevicePanel()
{
    stopTimer();
    sections_.clear(); // before the controls they place
}

void DevicePanel::setEnabledState(bool enabled)
{
    power_.setToggleState(enabled, juce::dontSendNotification);
}

void DevicePanel::timerCallback()
{
    if (!isShowing())
        return;
    for (auto& c : controls_)
        if (auto* pc = dynamic_cast<ParamControl*>(c.get()))
            pc->refresh();
    for (auto& s : sections_)
        s->refresh();
    if (presetBar_ != nullptr)
        presetBar_->refresh();
    updateDimming();
    springLoad();
    updateShownSections();
    // A header setting shown only while it matters came or went.
    for (size_t i = 0; i < headerShown_.size(); ++i)
        if (headerShown_[i] && i < headerShowing_.size() && headerShown_[i]() != headerShowing_[i])
        {
            resized();
            repaint();
            break;
        }
}

void DevicePanel::dimWhen(std::vector<juce::Component*> controls, std::function<bool()> inactive)
{
    dimming_.push_back({std::move(controls), std::move(inactive)});
    updateDimming();
}

void DevicePanel::updateDimming()
{
    for (const auto& d : dimming_)
    {
        const bool inactive = d.inactive();
        for (auto* c : d.controls)
        {
            // A section that is powered off dims its whole content already: never twice.
            const auto* section = c->findParentComponentOfClass<PanelSection>();
            const float alpha = inactive && (section == nullptr || section->isPoweredOn()) ? 0.35f : 1.0f;
            if (c->getAlpha() != alpha)
                c->setAlpha(alpha);
        }
    }
}

// ---- declaration ---------------------------------------------------------------------------

void DevicePanel::addPage(std::string id, juce::String title)
{
    jassert(pages_.empty() ? sections_.empty() : true); // pages begin before the first section
    jassert(std::none_of(pages_.begin(), pages_.end(), [&](const Page& p) { return p.id == id; }));
    pages_.push_back({std::move(id), std::move(title)});
    juce::StringArray titles;
    for (const auto& p : pages_)
        titles.add(p.title);
    pageTabs_.setTabs(titles, page_);
    if (pageTabs_.getParentComponent() == nullptr)
    {
        pageTabs_.onSelect = [this](int index) { setPage(index, true); };
        addAndMakeVisible(pageTabs_);
    }
}

PanelSection& DevicePanel::addSection(std::string id, juce::String title, int columns, std::optional<size_t> powerParam)
{
    jassert(findSection(id) == nullptr); // ids are unique within a panel
    auto& s = *sections_.emplace_back(std::make_unique<PanelSection>(*this, std::move(id), std::move(title), columns, powerParam));
    sectionPage_.push_back(std::max(0, pageCount() - 1));
    sectionShown_.emplace_back();
    sectionShowing_.push_back(true);
    addAndMakeVisible(s);
    return s;
}

void DevicePanel::showSectionWhen(PanelSection& section, std::function<bool()> shown)
{
    for (size_t i = 0; i < sections_.size(); ++i)
        if (sections_[i].get() == &section)
        {
            sectionShown_[i] = std::move(shown);
            sectionShowing_[i] = sectionShown_[i]();
        }
    resized();
}

bool DevicePanel::updateShownSections()
{
    // A section shown only while it matters came or went: the panel's width changes.
    bool changed = false;
    for (size_t i = 0; i < sectionShown_.size(); ++i)
        if (sectionShown_[i] && sectionShown_[i]() != sectionShowing_[i])
        {
            sectionShowing_[i] = !sectionShowing_[i];
            changed = true;
        }
    if (changed)
        viewChanged(true);
    return changed;
}

void DevicePanel::setDisplayName(juce::String name)
{
    if (name == displayName_)
        return;
    displayName_ = std::move(name);
    resized();
    repaint();
}

void DevicePanel::setFoldedDetail(juce::String detail)
{
    if (detail == foldedDetail_)
        return;
    foldedDetail_ = std::move(detail);
    repaint();
}

void DevicePanel::setHeaderIcon(std::optional<Icon> icon)
{
    headerIcon_ = icon;
    resized();
    repaint();
}

void DevicePanel::setFoldedAccessory(juce::Component* accessory)
{
    if (foldedAccessory_ != nullptr && foldedAccessory_->getParentComponent() == this)
        removeChildComponent(foldedAccessory_);
    foldedAccessory_ = accessory;
    if (foldedAccessory_ != nullptr)
        addChildComponent(*foldedAccessory_);
    resized();
}

juce::String DevicePanel::headerName() const
{
    return displayName_.isNotEmpty() ? displayName_ : juce::String(std::string(context_.info.name));
}

ParamComboBox& DevicePanel::addHeaderCombo(size_t param)
{
    auto& c = adopt(std::make_unique<ParamComboBox>(context_, param), *this);
    headerControls_.push_back(&c);
    const auto& spec = context_.info.params[param];
    headerCaptions_.push_back(juce::String(std::string(spec.shortName.empty() ? spec.name : spec.shortName)));
    headerShown_.emplace_back();
    return c;
}

void DevicePanel::showHeaderWhen(juce::Component& setting, std::function<bool()> shown)
{
    for (size_t i = 0; i < headerControls_.size(); ++i)
        if (headerControls_[i] == &setting)
            headerShown_[i] = std::move(shown);
    resized();
}

HeaderReadout& DevicePanel::addHeaderReadout(juce::String caption, juce::String widest,
                                             std::function<juce::String()> text, juce::String tooltip)
{
    auto& readout = adopt(std::make_unique<HeaderReadout>(std::move(text), std::move(widest)), *this);
    readout.setTooltip(std::move(tooltip));
    headerControls_.push_back(&readout);
    headerCaptions_.push_back(std::move(caption));
    headerShown_.emplace_back();
    return readout;
}

LatencyBadge& DevicePanel::showLatency(std::function<double()> latencyMs)
{
    auto& badge = adopt(
        std::make_unique<LatencyBadge>(juce::String(std::string(context_.info.name)), std::move(latencyMs)),
        *this);
    headerControls_.push_back(&badge);
    headerCaptions_.push_back({}); // it says what it is
    headerShown_.emplace_back();
    badge.onChange = [this]
    {
        resized();
        repaint();
    };
    return badge;
}

void DevicePanel::setMeters(std::vector<std::pair<juce::Component*, juce::String>> meters)
{
    meters_ = std::move(meters);
    for (auto& [meter, caption] : meters_)
        if (meter->getParentComponent() != this)
            addAndMakeVisible(meter);
}

void DevicePanel::destroy(juce::Component* control)
{
    for (auto& d : dimming_)
        std::erase(d.controls, control);
    std::erase_if(dimming_, [](const Dimming& d) { return d.controls.empty(); });
    std::erase_if(controls_, [control](const auto& c) { return c.get() == control; });
}

std::vector<PanelSection*> DevicePanel::sections() const
{
    std::vector<PanelSection*> out;
    for (const auto& s : sections_)
        out.push_back(s.get());
    return out;
}

PanelSection* DevicePanel::findSection(std::string_view id) const
{
    for (const auto& s : sections_)
        if (s->id() == id)
            return s.get();
    return nullptr;
}

std::vector<PanelSection*> DevicePanel::pageSections(int index) const
{
    std::vector<PanelSection*> out;
    for (size_t i = 0; i < sections_.size(); ++i)
        if ((pages_.empty() || sectionPage_[i] == index) && sectionShowing_[i])
            out.push_back(sections_[i].get());
    return out;
}

void DevicePanel::setPage(int index, bool byUser)
{
    index = std::clamp(index, 0, std::max(0, pageCount() - 1));
    if (index == page_)
        return;
    page_ = index;
    pageTabs_.setSelected(index);
    viewChanged(byUser);
}

void DevicePanel::turnPage(int delta, bool byUser)
{
    if (const int n = pageCount(); n > 1)
        setPage(((page_ + delta) % n + n) % n, byUser);
}

void DevicePanel::dragSource(int source, juce::Point<int> position)
{
    auto* m = context_.modulation;
    if (m == nullptr)
        return;
    m->draggedSource = m->lastSource = source;
    // The knob under the pointer, if it can take the source.
    m->dropParam.reset();
    for (auto* c = getComponentAt(position); c != nullptr && c != this; c = c->getParentComponent())
        if (auto* knob = dynamic_cast<ParamKnob*>(c))
        {
            if (knob->style() == Knob::Style::Rotary && m->canModulate(knob->param()))
                m->dropParam = knob->param();
            break;
        }
    // Held on a page tab: that page, after a moment.
    int tab = -1;
    if (pageTabs_.isVisible() && pageTabs_.getBounds().contains(position))
        tab = pageTabs_.tabAt(position - pageTabs_.getPosition());
    if (tab != springTab_ && !(tab == page_ && springTab_ < 0))
    {
        springTab_ = tab == page_ ? -1 : tab;
        springSince_ = juce::Time::getMillisecondCounterHiRes();
    }
    springLoad();
    for (auto& c : controls_)
        if (auto* pc = dynamic_cast<ParamControl*>(c.get()))
            pc->refresh();
}

void DevicePanel::springLoad()
{
    // A dragged source held on a page tab turns to that page (device-panels.md, "Modulation").
    if (springTab_ >= 0 && context_.modulation != nullptr && context_.modulation->draggedSource >= 0 &&
        juce::Time::getMillisecondCounterHiRes() - springSince_ >=
            Theme::current().motion.springLoad * 1000.0)
    {
        setPage(springTab_, true);
        springTab_ = -1;
    }
}

void DevicePanel::dropSource(int source, juce::Point<int> position, bool cancelled)
{
    auto* m = context_.modulation;
    if (m == nullptr)
        return;
    if (!cancelled)
        dragSource(source, position);
    const auto target = m->dropParam;
    m->draggedSource = -1;
    m->dropParam.reset();
    springTab_ = -1;
    if (!cancelled && target)
        m->addRoute(source, *target);
    for (auto& c : controls_)
        if (auto* pc = dynamic_cast<ParamControl*>(c.get()))
            pc->refresh();
}

bool DevicePanel::showParameter(size_t param)
{
    for (size_t i = 0; i < sections_.size(); ++i)
        if (sections_[i]->holds(param))
        {
            setPage(pages_.empty() ? 0 : sectionPage_[i], true);
            return true;
        }
    return false;
}

std::vector<juce::Component*> DevicePanel::meters() const
{
    std::vector<juce::Component*> out;
    for (const auto& [meter, caption] : meters_)
        out.push_back(meter);
    return out;
}

// ---- view state ----------------------------------------------------------------------------

std::string DevicePanel::viewState() const
{
    std::vector<std::string> collapsed;
    for (const auto& s : sections_)
        if (s->isCollapsed())
            collapsed.push_back(s->id());
    std::sort(collapsed.begin(), collapsed.end());
    auto json = nlohmann::json::object();
    if (!collapsed.empty())
        json["collapsed"] = collapsed;
    if (folded_)
        json["folded"] = true;
    if (page_ > 0)
        json["page"] = pageId(page_);
    return json.empty() ? std::string() : json.dump();
}

void DevicePanel::setViewState(std::string_view text)
{
    const auto json = nlohmann::json::parse(text, nullptr, false);
    std::set<std::string> collapsed;
    bool folded = false;
    int page = 0;
    if (json.is_object())
    {
        if (const auto it = json.find("page"); it != json.end() && it->is_string())
            for (int i = 0; i < pageCount(); ++i)
                if (pageId(i) == it->get<std::string>())
                    page = i;
        if (const auto it = json.find("collapsed"); it != json.end() && it->is_array())
            for (const auto& id : *it)
                if (id.is_string())
                    collapsed.insert(id.get<std::string>());
        if (const auto it = json.find("folded"); it != json.end() && it->is_boolean())
            folded = it->get<bool>();
    }
    for (auto& s : sections_)
        s->collapsed_ = collapsed.contains(s->id());
    folded_ = foldable_ && folded;
    page_ = page;
    pageTabs_.setSelected(page);
    resized();
    repaint();
}

void DevicePanel::setFoldable(bool foldable)
{
    foldable_ = foldable;
    if (!foldable_)
        folded_ = false;
    resized();
}

void DevicePanel::setFolded(bool folded, bool byUser)
{
    folded = folded && foldable_;
    if (folded == folded_)
        return;
    folded_ = folded;
    viewChanged(byUser);
}

void DevicePanel::setAllSectionsCollapsed(bool collapsed, bool byUser)
{
    bool changed = false;
    for (auto& s : sections_)
        if (s->collapsed_ != collapsed)
        {
            s->collapsed_ = collapsed;
            changed = true;
        }
    if (changed)
        viewChanged(byUser);
}

void DevicePanel::viewChanged(bool byUser)
{
    resized();
    repaint();
    if (byUser && onViewChanged)
        onViewChanged();
}

// ---- geometry ------------------------------------------------------------------------------

int DevicePanel::headerMinimumWidth() const
{
    const auto& grid = Theme::current().deviceGrid;
    float w = static_cast<float>(2 * grid.headerPadding);
    if (foldable_)
        w += kFoldButton + 4;
    if (onEnabledChange)
        w += static_cast<float>(grid.power + 6);
    if (headerIcon_)
        w += static_cast<float>(grid.power + 6);
    // Text widths rounded up, as resized() lays them out: a name short by a fraction is cut.
    w += std::ceil(juce::GlyphArrangement::getStringWidth(nameFont(), headerName()));
    if (!pages_.empty())
        w += static_cast<float>(kSettingsGap + pageTabs_.preferredWidth());
    // Room for every setting, shown or not, so the panel never changes width as they come and go.
    for (size_t i = 0; i < headerControls_.size(); ++i)
        if (const int width = reservedWidth(*headerControls_[i]); width > 0)
            w += static_cast<float>(width + kSettingsGap) +
                 (headerCaptions_[i].isEmpty()
                      ? 0.0f
                      : std::ceil(juce::GlyphArrangement::getStringWidth(captionFont(), headerCaptions_[i])) +
                            kCaptionGap);
    if (presetBar_ != nullptr)
        w += static_cast<float>(presetBar_->minimumWidth() + kSettingsGap);
    return roundUpToGrid(w);
}

int DevicePanel::preferredWidth() const
{
    const auto& grid = Theme::current().deviceGrid;
    if (folded_)
        return grid.foldedWidth;
    // The widest page's sections (as collapsed as they are), so turning a page moves nothing.
    int w = 0;
    for (int page = 0; page < std::max(1, pageCount()); ++page)
    {
        int sum = 0;
        for (auto* s : pageSections(page))
            sum += s->preferredWidth();
        w = std::max(w, sum);
    }
    if (!meters_.empty())
        w += grid.meterStripWidth(static_cast<int>(meters_.size()));
    return std::max(w, headerMinimumWidth());
}

bool DevicePanel::isDragHandle(juce::Point<int> position) const
{
    auto onControl = [&](const juce::Component& c) { return c.isVisible() && c.getBounds().contains(position); };
    if (onControl(power_) || onControl(fold_) || (presetBar_ != nullptr && onControl(*presetBar_)) ||
        onControl(pageTabs_) || (foldedAccessory_ != nullptr && onControl(*foldedAccessory_)))
        return false;
    if (folded_)
        return getLocalBounds().contains(position);
    if (position.y >= Theme::current().deviceHeaderHeight)
        return false;
    return std::none_of(headerControls_.begin(), headerControls_.end(), [&](auto* c) { return onControl(*c); });
}

void DevicePanel::resized()
{
    const auto& t = Theme::current();
    const auto& grid = t.deviceGrid;
    auto area = getLocalBounds();
    auto header = area.removeFromTop(t.deviceHeaderHeight);
    power_.setVisible(static_cast<bool>(onEnabledChange));
    fold_.setVisible(foldable_);
    separators_.clear();

    if (folded_)
    {
        // The strip: unfold chevron, power, then the name (painted, rotated).
        fold_.setIcon(Icon::ChevronRight);
        fold_.setTooltip("Unfold the module");
        auto strip = getLocalBounds().withTrimmedTop(6);
        fold_.setBounds(strip.removeFromTop(kFoldButton).withSizeKeepingCentre(kFoldButton, kFoldButton));
        strip.removeFromTop(6);
        power_.setBounds(strip.removeFromTop(grid.power).withSizeKeepingCentre(grid.power, grid.power));
        if (foldedAccessory_ != nullptr)
        {
            // At the strip's foot, as wide as it asks, at most the strip's width.
            const int w =
                std::min(foldedAccessory_->getWidth() > 0 ? foldedAccessory_->getWidth() : grid.foldedWidth,
                         grid.foldedWidth - 2 * t.shell.itemGap);
            foldedAccessory_->setBounds(getLocalBounds()
                                            .withTrimmedBottom(kFoldedFoot)
                                            .removeFromBottom(grid.sourceHandle)
                                            .withSizeKeepingCentre(w, grid.sourceHandle));
            foldedAccessory_->setVisible(true);
        }
        for (auto& s : sections_)
            s->setVisible(false);
        pageTabs_.setVisible(false);
        for (auto* c : headerControls_)
            c->setVisible(false);
        if (presetBar_ != nullptr)
            presetBar_->setVisible(false);
        for (auto& [meter, caption] : meters_)
            meter->setVisible(false);
        return;
    }
    fold_.setIcon(Icon::ChevronLeft);
    fold_.setTooltip("Fold the module");
    if (foldedAccessory_ != nullptr)
        foldedAccessory_->setVisible(false);

    // Header: fold, power, name, then the settings from the right.
    auto h = header.reduced(grid.headerPadding, 0);
    if (foldable_)
    {
        fold_.setBounds(h.removeFromLeft(kFoldButton).withSizeKeepingCentre(kFoldButton, kFoldButton));
        h.removeFromLeft(4);
    }
    if (onEnabledChange)
    {
        power_.setBounds(h.removeFromLeft(grid.power).withSizeKeepingCentre(grid.power, grid.power));
        h.removeFromLeft(6);
    }
    headerIconArea_ = {};
    if (headerIcon_)
    {
        headerIconArea_ = h.removeFromLeft(grid.power).withSizeKeepingCentre(grid.power, grid.power);
        h.removeFromLeft(6);
    }
    const int nameWidth =
        static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(nameFont(), headerName())));
    // The page tabs right after the name (ADR 0051); the name keeps just its own width.
    if (!pages_.empty())
    {
        nameArea_ = h.removeFromLeft(nameWidth);
        h.removeFromLeft(kSettingsGap);
        pageTabs_.setVisible(true);
        pageTabs_.setBounds(h.removeFromLeft(pageTabs_.preferredWidth()));
        h.removeFromLeft(kSettingsGap);
    }
    captionAreas_.assign(headerControls_.size(), {});
    headerShowing_.assign(headerControls_.size(), true);
    for (size_t i = headerControls_.size(); i-- > 0;)
    {
        auto* c = headerControls_[i];
        // A latency badge while there's no delay, a setting while it doesn't matter: nothing, and no gap.
        headerShowing_[i] = !headerShown_[i] || headerShown_[i]();
        const int width = headerShowing_[i] ? settingWidth(*c) : 0;
        c->setVisible(width > 0);
        if (width <= 0)
            continue;
        c->setBounds(h.removeFromRight(width).withSizeKeepingCentre(width, grid.row));
        if (headerCaptions_[i].isNotEmpty())
        {
            h.removeFromRight(kCaptionGap);
            const int caption = static_cast<int>(
                std::ceil(juce::GlyphArrangement::getStringWidth(captionFont(), headerCaptions_[i])));
            captionAreas_[i] = h.removeFromRight(caption);
        }
        h.removeFromRight(kSettingsGap);
    }
    // The presets, right-aligned before the settings; the field takes the room the name leaves.
    if (presetBar_ != nullptr)
    {
        const int name = pages_.empty() ? nameWidth + kSettingsGap : 0;
        const int width =
            std::clamp(h.getWidth() - name, presetBar_->minimumWidth(), presetBar_->maximumWidth());
        presetBar_->setVisible(true);
        presetBar_->setBounds(h.removeFromRight(width).withSizeKeepingCentre(width, grid.row));
        h.removeFromRight(kSettingsGap);
    }
    if (pages_.empty())
        nameArea_ = h;

    // Body: the page's sections left to right, separated by lines; then the meter strip.
    for (auto& s : sections_)
        s->setVisible(false);
    int x = 0;
    for (auto* s : shownSections())
    {
        if (x > 0)
            separators_.push_back(x);
        s->refresh(); // power dimming, also before the first refresh tick
        s->setVisible(true);
        s->setBounds(x, area.getY(), s->preferredWidth(), grid.bodyHeight());
        x += s->preferredWidth();
    }
    if (!meters_.empty())
    {
        // At the right edge, where a wider header leaves room after the sections.
        x = std::max(x, getWidth() - grid.meterStripWidth(static_cast<int>(meters_.size())));
        separators_.push_back(x);
        int mx = x + grid.meterStripPadding - 8; // 6 px after the line, 8 px before the edge
        for (auto& [meter, caption] : meters_)
        {
            meter->setVisible(true);
            meter->setBounds(mx, area.getY() + grid.contentTop(), grid.meterWidth, grid.height(grid.rows));
            mx += grid.meterWidth + grid.meterGap;
        }
    }
}

void DevicePanel::paint(juce::Graphics& g)
{
    const auto& t = Theme::current();
    const auto& grid = t.deviceGrid;

    // Flat body; separation from the chain background comes from the tone step (ADR 0014).
    g.setColour(t.surface2);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), t.radiusPanel);

    const auto name = headerName();
    if (folded_)
    {
        // The name rotated, reading upwards, below the chevron and power (and above an accessory).
        auto area = getLocalBounds().toFloat().withTrimmedTop(48.0f).withTrimmedBottom(
            static_cast<float>(kFoldedFoot) +
            (foldedAccessory_ != nullptr ? static_cast<float>(grid.sourceHandle + t.shell.groupGap) : 0.0f));
        juce::Graphics::ScopedSaveState saved(g);
        g.addTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::halfPi, area.getCentreX(), area.getCentreY()));
        const auto rotated = juce::Rectangle<float>(area.getHeight(), area.getWidth()).withCentre(area.getCentre());
        g.setColour(t.textPrimary);
        g.setFont(nameFont());
        g.drawText(name, rotated, juce::Justification::centredRight, true);
        if (foldedDetail_.isNotEmpty())
        {
            // After the name, reading on down the strip.
            const float nameWidth = std::ceil(juce::GlyphArrangement::getStringWidth(nameFont(), name));
            g.setColour(t.textTertiary);
            g.setFont(captionFont());
            g.drawText(foldedDetail_,
                       rotated.withTrimmedRight(nameWidth + static_cast<float>(t.shell.groupGap)),
                       juce::Justification::centredRight, true);
        }
        return;
    }

    if (headerIcon_)
        drawIcon(g, *headerIcon_, headerIconArea_.toFloat(), t.textSecondary);
    g.setColour(t.textPrimary);
    g.setFont(nameFont());
    g.drawText(name, nameArea_, juce::Justification::centredLeft, true);
    g.setColour(t.textTertiary);
    g.setFont(captionFont());
    for (size_t i = 0; i < captionAreas_.size(); ++i)
        g.drawText(headerCaptions_[i], captionAreas_[i], juce::Justification::centredRight, false);

    // Lines between sections, from the title row to the bottom of the content.
    const auto top = static_cast<float>(t.deviceHeaderHeight + grid.bodyTop);
    const auto height = static_cast<float>(grid.titleHeight + grid.titleGap + grid.height(grid.rows));
    g.setColour(t.line);
    for (int x : separators_)
        g.fillRect(static_cast<float>(x), top, 1.0f, height);

    // Meter captions in the title row.
    for (const auto& [meter, caption] : meters_)
        if (meter->isVisible() && caption.isNotEmpty())
            paintCapsLabel(g, caption,
                           juce::Rectangle<float>(static_cast<float>(meter->getX()) - 8.0f, top,
                                                  static_cast<float>(meter->getWidth()) + 16.0f,
                                                  static_cast<float>(grid.titleHeight)),
                           juce::Justification::centred);
}

void DevicePanel::mouseUp(const juce::MouseEvent& e)
{
    // A click on the folded strip unfolds it (a drag reorders in the chain instead).
    if (folded_ && e.eventComponent == this && !e.mouseWasDraggedSinceMouseDown())
        setFolded(false, true);
}
} // namespace fd::ui
