namespace fd::ui
{
namespace
{
constexpr int kFoldButton = 16;
constexpr int kPower = 14;
constexpr int kSettingsGap = 8;

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

/// A header setting's width: a combo fits its widest choice and its chevron.
int settingWidth(juce::Component& c)
{
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
    startTimerHz(30);
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
    updateDimming();
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

PanelSection& DevicePanel::addSection(std::string id, juce::String title, int columns, std::optional<size_t> powerParam)
{
    jassert(findSection(id) == nullptr); // ids are unique within a panel
    auto& s = *sections_.emplace_back(std::make_unique<PanelSection>(*this, std::move(id), std::move(title), columns, powerParam));
    addAndMakeVisible(s);
    return s;
}

ParamComboBox& DevicePanel::addHeaderCombo(size_t param)
{
    auto& c = adopt(std::make_unique<ParamComboBox>(context_, param), *this);
    headerControls_.push_back(&c);
    const auto& spec = context_.info.params[param];
    headerCaptions_.push_back(juce::String(std::string(spec.shortName.empty() ? spec.name : spec.shortName)));
    return c;
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
    return json.empty() ? std::string() : json.dump();
}

void DevicePanel::setViewState(std::string_view text)
{
    const auto json = nlohmann::json::parse(text, nullptr, false);
    std::set<std::string> collapsed;
    bool folded = false;
    if (json.is_object())
    {
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
        w += kPower + 6;
    w += juce::GlyphArrangement::getStringWidth(nameFont(), juce::String(std::string(context_.info.name)));
    for (size_t i = 0; i < headerControls_.size(); ++i)
        w += static_cast<float>(settingWidth(*headerControls_[i]) + kSettingsGap) +
             juce::GlyphArrangement::getStringWidth(captionFont(), headerCaptions_[i]) + kCaptionGap;
    return roundUpToGrid(w);
}

int DevicePanel::preferredWidth() const
{
    const auto& grid = Theme::current().deviceGrid;
    if (folded_)
        return grid.foldedWidth;
    int w = 0;
    for (const auto& s : sections_)
        w += s->preferredWidth();
    if (!meters_.empty())
        w += grid.meterStripWidth(static_cast<int>(meters_.size()));
    return std::max(w, headerMinimumWidth());
}

bool DevicePanel::isDragHandle(juce::Point<int> position) const
{
    auto onControl = [&](const juce::Component& c) { return c.isVisible() && c.getBounds().contains(position); };
    if (onControl(power_) || onControl(fold_))
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
        power_.setBounds(strip.removeFromTop(kPower).withSizeKeepingCentre(kPower, kPower));
        for (auto& s : sections_)
            s->setVisible(false);
        for (auto* c : headerControls_)
            c->setVisible(false);
        for (auto& [meter, caption] : meters_)
            meter->setVisible(false);
        return;
    }
    fold_.setIcon(Icon::ChevronLeft);
    fold_.setTooltip("Fold the module");

    // Header: fold, power, name, then the settings from the right.
    auto h = header.reduced(grid.headerPadding, 0);
    if (foldable_)
    {
        fold_.setBounds(h.removeFromLeft(kFoldButton).withSizeKeepingCentre(kFoldButton, kFoldButton));
        h.removeFromLeft(4);
    }
    if (onEnabledChange)
    {
        power_.setBounds(h.removeFromLeft(kPower).withSizeKeepingCentre(kPower, kPower));
        h.removeFromLeft(6);
    }
    captionAreas_.assign(headerControls_.size(), {});
    for (size_t i = headerControls_.size(); i-- > 0;)
    {
        auto* c = headerControls_[i];
        const int width = settingWidth(*c);
        c->setVisible(true);
        c->setBounds(h.removeFromRight(width).withSizeKeepingCentre(width, grid.row));
        h.removeFromRight(kCaptionGap);
        const int caption = static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(captionFont(), headerCaptions_[i])));
        captionAreas_[i] = h.removeFromRight(caption);
        h.removeFromRight(kSettingsGap);
    }
    nameArea_ = h;

    // Body: sections left to right, separated by lines; then the meter strip.
    int x = 0;
    for (auto& s : sections_)
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

    const auto name = juce::String(std::string(context_.info.name));
    if (folded_)
    {
        // The name rotated, reading upwards, below the chevron and power.
        auto area = getLocalBounds().toFloat().withTrimmedTop(48.0f).withTrimmedBottom(8.0f);
        juce::Graphics::ScopedSaveState saved(g);
        g.addTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::halfPi, area.getCentreX(), area.getCentreY()));
        const auto rotated = juce::Rectangle<float>(area.getHeight(), area.getWidth()).withCentre(area.getCentre());
        g.setColour(t.textPrimary);
        g.setFont(nameFont());
        g.drawText(name, rotated, juce::Justification::centredRight, true);
        return;
    }

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
