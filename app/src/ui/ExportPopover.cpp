#include "ui/ExportPopover.h"

#include "model/Chance.h"

#include <cmath>

namespace fd::app
{
namespace
{
using model::ExportFormat;
using model::ExportNormalise;
using model::ExportRange;
using model::ExportWhat;

/// A text button's width: its label in the control style, with the bar's padding each side.
int buttonWidth(const juce::String& text)
{
    const auto& sh = ui::Theme::current().shell;
    return static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(ui::font(ui::TextStyle::Control), text))) +
           2 * sh.barPadding;
}

/// "3:24" for 204 s.
juce::String minutes(double seconds)
{
    const int s = static_cast<int>(std::lround(std::max(0.0, seconds)));
    return juce::String(s / 60) + ":" + juce::String(s % 60).paddedLeft('0', 2);
}

juce::String rateName(double rate)
{
    return juce::String(rate / 1000.0, rate == std::floor(rate / 1000.0) * 1000.0 ? 0 : 1) + " kHz";
}

/// `text` cut from its start ("...Song/Exports") to fit `width` in `font`.
juce::String fromTheEnd(juce::String text, float width, const juce::Font& font)
{
    if (juce::GlyphArrangement::getStringWidth(font, text) <= width)
        return text;
    const juce::String dots = juce::String::fromUTF8("\xe2\x80\xa6");
    while (text.isNotEmpty() && juce::GlyphArrangement::getStringWidth(font, dots + text) > width)
        text = text.substring(1);
    return dots + text;
}

constexpr int kWhat[] = {static_cast<int>(ExportWhat::Mix), static_cast<int>(ExportWhat::SelectedTracks), static_cast<int>(ExportWhat::AllTracks)};
} // namespace

ExportPopover::ExportPopover(engine::Session& session) : session_(session)
{
    setWantsKeyboardFocus(true);
    what_.addItem("The mix", 1);
    what_.addItem("Selected tracks", 2);
    what_.addItem("All tracks (and returns)", 3);
    format_.addItem("WAV", 1);
    format_.addItem("FLAC", 2);
    format_.addItem("AIFF", 3);
    depth_.addItem("16-bit", 16);
    depth_.addItem("24-bit", 24);
    depth_.addItem("32-bit float", 32);
    dither_.addItem("Automatic", 1);
    dither_.addItem("Off", 2);
    dither_.setTooltip("Triangular dither whenever the bit depth is below 32-bit float");
    normalise_.addItem("Off", 1);
    normalise_.addItem(juce::String::fromUTF8("Peak (\xe2\x88\x92") + "1 dBTP)", 2);
    normalise_.addItem("Loudness", 3);
    normalise_.setTooltip("A gain, never a limiter: Loudness never lets the true peak pass -1 dBTP");
    through_.setClickingTogglesState(true);
    through_.setTooltip("Each stem as heard in the mix: what its sends add in the returns, and the master's modules. "
                        "The stems then no longer add up to the mix");
    target_.setRange(-36.0, -6.0, 0.5);
    target_.textFromValue = [](double v) { return juce::String(v, 1).replace("-", juce::String::fromUTF8("\xe2\x88\x92")) + " LUFS"; };
    target_.valueFromText = [](const juce::String& text) -> std::optional<double> {
        const auto plain = text.replace(juce::String::fromUTF8("\xe2\x88\x92"), "-").upToFirstOccurrenceOf("LUFS", false, true).trim();
        if (plain.isEmpty() || !plain.containsOnly("-+.0123456789"))
            return std::nullopt;
        return plain.getDoubleValue();
    };
    target_.setTooltip("The loudness to normalise to (integrated, LUFS)");
    name_.setFont(ui::font(ui::TextStyle::Body));
    name_.setJustification(juce::Justification::centredLeft);
    name_.setIndents(ui::Theme::current().shell.fieldTextInset, 0);
    name_.setTooltip("The file's name; stems add their track's");
    name_.onReturnKey = [this] { exportNow(); };
    name_.onTextChange = [this] { store(); };
    choose_.setTooltip("Export anywhere on disk: choose the folder and the name");
    choose_.onClick = [this] { chooseFolder(); };
    defaultFolder_.setTooltip("Back to the project's Exports folder");
    defaultFolder_.onClick = [this] {
        auto s = session_.exportSettings();
        s.folder.clear();
        session_.setExportSettings(s);
        repaint();
    };
    export_.onClick = [this] { job_ ? (void)cancel() : (void)exportNow(); };
    keepAll_.setTooltip(
        "Each export rolls them again, so each is different: Keep all as played keeps what you "
        "heard last (Song > Keep all as played)");
    keepAll_.onClick = [this]
    {
        session_.keepAllRolls(true);
        session_.updateSnapshotNow();
        updateEnabled();
        setSize(getWidth(), preferredHeight());
        resized();
        repaint();
    };
    show_.setTooltip("Show the file in the file manager");
    show_.onClick = [this] {
        if (last_ && !last_->files.empty())
            last_->files.front().file.revealToUser();
    };
    for (auto* box : {&what_, &range_, &format_, &depth_, &rate_, &dither_, &normalise_})
    {
        box->onChange = [this] { store(); };
        addAndMakeVisible(box);
    }
    through_.onClick = [this] { store(); };
    target_.onValueChange = [this](double) { store(); };
    for (juce::Component* c : {static_cast<juce::Component*>(&through_), static_cast<juce::Component*>(&target_),
                               static_cast<juce::Component*>(&name_), static_cast<juce::Component*>(&choose_),
                               static_cast<juce::Component*>(&defaultFolder_), static_cast<juce::Component*>(&export_),
                               static_cast<juce::Component*>(&show_)})
        addAndMakeVisible(c);
    addChildComponent(keepAll_);
}

juce::String ExportPopover::chanceText() const
{
    return juce::String::fromUTF8(model::chanceIn(*session_.snapshot()).rolling().c_str());
}

ExportPopover::~ExportPopover() = default;

void ExportPopover::open(juce::Component& parent)
{
    if (getParentComponent() != &parent)
        parent.addChildComponent(this);
    load();
    const auto& sh = ui::Theme::current().shell;
    setBounds(juce::Rectangle<int>(sh.formPopoverWidth, preferredHeight())
                  .withCentre({parent.getWidth() / 2, 0})
                  .withY(sh.popoverTop)
                  .constrainedWithin(parent.getLocalBounds()));
    setVisible(true);
    toFront(true);
    outside_.attach();
    ui::takeFocus(export_); // Enter exports; opened from a menu too
}

void ExportPopover::close()
{
    outside_.detach();
    if (!isVisible())
        return;
    setVisible(false);
    if (auto* parent = getParentComponent(); parent != nullptr && parent->isShowing())
        parent->grabKeyboardFocus();
}

bool ExportPopover::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        close();
        return true;
    }
    if (key == juce::KeyPress::returnKey)
    {
        exportNow();
        return true;
    }
    return false;
}

void ExportPopover::load()
{
    loading_ = true;
    const auto s = session_.exportSettings();
    what_.setSelectedId(static_cast<int>(s.what) + 1, juce::dontSendNotification);
    through_.setToggleState(s.throughReturns, juce::dontSendNotification);
    // The ranges, with what they'd export now.
    range_.clear(juce::dontSendNotification);
    session_.updateSnapshotNow();
    const auto& snapshot = *session_.snapshot();
    const double end = model::songEnd(snapshot);
    range_.addItem("Song  " + (end > 0.0 ? "0:00 - " + minutes(end) + " + tail" : juce::String("(no clips yet)")), 1);
    const auto [loopStart, loopEnd] = session_.loopRange();
    const bool loop = loopEnd > loopStart;
    range_.addItem("Loop  " + (loop ? juce::String(model::loopBars(snapshot.tempo, loopStart, loopEnd)) : juce::String("(no loop)")), 2);
    range_.setItemEnabled(2, loop);
    range_.setSelectedId(s.range == ExportRange::Loop && loop ? 2 : 1, juce::dontSendNotification);
    format_.setSelectedId(static_cast<int>(s.format) + 1, juce::dontSendNotification);
    depth_.setSelectedId(s.bitDepth, juce::dontSendNotification);
    rate_.clear(juce::dontSendNotification);
    rate_.addItem("Project (" + rateName(session_.sampleRate()) + ")", 1);
    int id = 2;
    for (const double r : model::kExportRates)
        rate_.addItem(rateName(r), id++);
    id = 1;
    for (size_t i = 0; i < std::size(model::kExportRates); ++i)
        if (model::kExportRates[i] == s.sampleRate)
            id = static_cast<int>(i) + 2;
    rate_.setSelectedId(id, juce::dontSendNotification);
    dither_.setSelectedId(s.dither ? 1 : 2, juce::dontSendNotification);
    normalise_.setSelectedId(static_cast<int>(s.normalise) + 1, juce::dontSendNotification);
    target_.setValue(s.loudnessTarget, juce::dontSendNotification);
    name_.setText(juce::String::fromUTF8(s.name.c_str()), false);
    name_.setTextToShowWhenEmpty(juce::String::fromUTF8(session_.projectName().c_str()), ui::Theme::current().textTertiary);
    loading_ = false;
    updateEnabled();
}

void ExportPopover::store()
{
    if (loading_)
        return;
    auto s = session_.exportSettings();
    s.what = static_cast<ExportWhat>(kWhat[std::clamp(what_.getSelectedId() - 1, 0, 2)]);
    s.throughReturns = through_.getToggleState();
    s.range = range_.getSelectedId() == 2 ? ExportRange::Loop : ExportRange::Song;
    s.format = static_cast<ExportFormat>(std::clamp(format_.getSelectedId() - 1, 0, 2));
    s.bitDepth = depth_.getSelectedId();
    s.sampleRate = rate_.getSelectedId() >= 2 ? model::kExportRates[static_cast<size_t>(rate_.getSelectedId() - 2)] : 0.0;
    s.dither = dither_.getSelectedId() != 2;
    s.normalise = static_cast<ExportNormalise>(std::clamp(normalise_.getSelectedId() - 1, 0, 2));
    s.loudnessTarget = target_.getValue();
    s.name = name_.getText().trim().toStdString();
    session_.setExportSettings(s);
    // A format holds only some depths: the settings say which it takes now.
    loading_ = true;
    depth_.setSelectedId(session_.exportSettings().bitDepth, juce::dontSendNotification);
    loading_ = false;
    updateEnabled();
    if (isVisible() && getHeight() != preferredHeight()) // a row came or went
        setSize(getWidth(), preferredHeight());
    else
        resized();
    repaint();
}

bool ExportPopover::showsThrough() const
{
    return session_.exportSettings().what != ExportWhat::Mix;
}

bool ExportPopover::showsTarget() const
{
    return session_.exportSettings().normalise == ExportNormalise::Loudness;
}

void ExportPopover::updateEnabled()
{
    const auto s = session_.exportSettings();
    through_.setVisible(showsThrough());
    target_.setVisible(showsTarget());
    const auto depths = model::bitDepthsFor(s.format);
    for (const int d : {16, 24, 32})
        depth_.setItemEnabled(d, std::find(depths.begin(), depths.end(), d) != depths.end());
    dither_.setEnabled(s.bitDepth < 32);
    defaultFolder_.setEnabled(!s.folder.empty());
    export_.setButtonText(job_ ? "Cancel" : "Export");
    show_.setVisible(last_ && !last_->files.empty());
    keepAll_.setVisible(showsChance());
}

void ExportPopover::chooseFolder()
{
    const auto s = session_.exportSettings();
    const auto folder = session_.exportFolder(s);
    const auto name = s.name.empty() ? session_.projectName() : s.name;
    const auto ext = juce::String(model::extensionFor(s.format));
    chooser_ = std::make_unique<juce::FileChooser>("Export to", folder.getChildFile(juce::String::fromUTF8(name.c_str()) + ext), "*" + ext);
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                          [this](const juce::FileChooser& fc) {
                              const auto file = fc.getResult();
                              if (file == juce::File())
                                  return;
                              auto settings = session_.exportSettings();
                              // Inside the project's folder: kept relative, so it moves with it.
                              const auto project = session_.exportsFolder().getParentDirectory();
                              const auto chosen = file.getParentDirectory();
                              settings.folder = chosen == session_.exportsFolder() ? std::string()
                                                : chosen.isAChildOf(project)        ? chosen.getRelativePathFrom(project).toStdString()
                                                                                    : chosen.getFullPathName().toStdString();
                              settings.name = file.getFileNameWithoutExtension().toStdString();
                              for (const auto f : {ExportFormat::Wav, ExportFormat::Flac, ExportFormat::Aiff})
                                  if (file.hasFileExtension(juce::String(model::extensionFor(f)).substring(1)))
                                      settings.format = f;
                              session_.setExportSettings(settings);
                              load();
                              repaint();
                          });
}

bool ExportPopover::exportNow()
{
    if (job_)
        return false;
    const auto s = session_.exportSettings();
    const auto tracks = selectedTracks ? selectedTracks() : std::vector<std::uint64_t>{};
    if (const auto why = session_.exportRefusal(s, tracks); !why.empty())
    {
        if (onMessage)
            onMessage(juce::String::fromUTF8(why.c_str()));
        return false;
    }
    juce::Component::SafePointer<ExportPopover> self(this);
    job_ = session_.exportAudio(s, tracks, [self](const engine::Session::ExportResult& result) {
        if (self == nullptr)
            return;
        self->job_.reset();
        if (!result.cancelled && result.error.empty())
            self->last_ = result;
        else if (!result.cancelled && self->onMessage)
            self->onMessage(juce::String::fromUTF8(result.error.c_str()));
        self->updateEnabled();
        self->resized();
        self->repaint();
        if (self->onExported)
            self->onExported(result);
    });
    updateEnabled();
    repaint();
    return job_ != nullptr;
}

bool ExportPopover::cancel()
{
    if (!job_)
        return false;
    job_->cancel();
    return true;
}

std::optional<float> ExportPopover::progress() const
{
    return job_ ? std::optional(job_->progress()) : std::nullopt;
}

juce::String ExportPopover::report() const
{
    if (job_)
        return "Exporting " + juce::String(juce::roundToInt(job_->progress() * 100.0f)) + " %";
    if (!last_ || last_->files.empty())
        return {};
    const auto& f = last_->files.front();
    const auto& m = f.measured;
    const auto dot = juce::String::fromUTF8(" \xc2\xb7 ");
    juce::String text = f.file.getFileName();
    if (last_->files.size() > 1)
        text += " and " + juce::String(static_cast<int>(last_->files.size()) - 1) + " more";
    text += dot + minutes(m.seconds);
    if (std::isfinite(m.integratedLufs))
        text += dot + juce::String(m.integratedLufs, 1).replace("-", juce::String::fromUTF8("\xe2\x88\x92")) + " LUFS";
    if (std::isfinite(m.truePeakDb))
        text += dot + juce::String(m.truePeakDb, 1).replace("-", juce::String::fromUTF8("\xe2\x88\x92")) + " dBTP";
    if (m.clippedSamples > 0 || m.truePeakDb > 0.0)
        text += dot + "clips";
    return text;
}

int ExportPopover::preferredHeight() const
{
    const auto& sh = ui::Theme::current().shell;
    return sh.padding * 2 + sh.buttonSize                       // title
           + 3 * (sh.groupGap + 1)                              // separators
           + (showsThrough() ? 3 : 2) * sh.rowHeight            // what, (through), range
           + (showsTarget() ? 6 : 5) * sh.rowHeight             // format ... normalise, (target)
           + sh.rowHeight + 2 * sh.fieldHeight + 2 * sh.itemGap // save to (path, buttons), name
           + (showsChance() ? sh.groupGap + 1 + sh.rowHeight + sh.fieldHeight : 0) // chance, Keep all
           + sh.rowHeight + sh.itemGap + sh.fieldHeight;                           // report, export
}

void ExportPopover::resized()
{
    const auto& sh = ui::Theme::current().shell;
    auto r = getLocalBounds().reduced(sh.padding);
    separators_.clear();
    labels_.clear();
    const auto separator = [&] {
        r.removeFromTop(sh.groupGap / 2);
        separators_.push_back(r.getY());
        r.removeFromTop(1 + sh.groupGap - sh.groupGap / 2);
    };
    // A row: its label on the left, its control in the value column, inset from its top and bottom.
    const auto row = [&](const juce::String& label, int height) {
        auto line = r.removeFromTop(height);
        labels_.emplace_back(label, line.removeFromLeft(sh.propertyLabelWidth));
        return line;
    };
    const auto field = [&](const juce::String& label, juce::Component& c) { c.setBounds(row(label, sh.rowHeight).reduced(0, sh.propertyFieldInset)); };
    title_ = r.removeFromTop(sh.buttonSize);
    separator();
    field("What", what_);
    if (showsThrough())
    {
        auto line = row({}, sh.rowHeight).reduced(0, sh.propertyFieldInset);
        through_.setBounds(line.removeFromLeft(std::min(line.getWidth(), ui::Toggle::widthFor(through_.getButtonText()))));
    }
    field("Range", range_);
    separator();
    field("Format", format_);
    field("Bit depth", depth_);
    field("Sample rate", rate_);
    field("Dither", dither_);
    field("Normalise", normalise_);
    if (showsTarget())
    {
        auto line = row("Target", sh.rowHeight).reduced(0, sh.propertyFieldInset);
        target_.setBounds(line.removeFromLeft(target_.widthFor(juce::String::fromUTF8("\xe2\x88\x92") + "36.0 LUFS")));
    }
    separator();
    folder_ = row("Save to", sh.rowHeight);
    {
        auto line = r.removeFromTop(sh.fieldHeight).withTrimmedLeft(sh.propertyLabelWidth);
        choose_.setBounds(line.removeFromLeft(buttonWidth(choose_.getButtonText())));
        line.removeFromLeft(sh.itemGap);
        defaultFolder_.setBounds(line.removeFromLeft(buttonWidth(defaultFolder_.getButtonText())));
    }
    r.removeFromTop(sh.itemGap);
    {
        auto line = row("Name", sh.fieldHeight);
        int ext = 0; // the widest extension
        for (const auto f : {ExportFormat::Wav, ExportFormat::Flac, ExportFormat::Aiff})
            ext = std::max(ext, static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(
                                    ui::font(ui::TextStyle::Body), juce::String(model::extensionFor(f))))));
        extension_ = line.removeFromRight(ext);
        line.removeFromRight(sh.itemGap);
        name_.setBounds(line);
    }
    // What the song leaves to chance (ADR 0062), and Keep all as played under it.
    chance_ = {};
    if (showsChance())
    {
        separator();
        chance_ = row("Chance", sh.rowHeight);
        auto line = r.removeFromTop(sh.fieldHeight).withTrimmedLeft(sh.propertyLabelWidth);
        keepAll_.setBounds(line.removeFromLeft(buttonWidth(keepAll_.getButtonText())));
    }
    r.removeFromTop(sh.itemGap);
    report_ = r.removeFromTop(sh.rowHeight);
    r.removeFromTop(sh.itemGap);
    auto bottom = r.removeFromTop(sh.fieldHeight);
    export_.setBounds(bottom.removeFromRight(std::max(buttonWidth("Export"), buttonWidth("Cancel"))));
    bottom.removeFromRight(sh.itemGap);
    show_.setBounds(bottom.removeFromRight(buttonWidth(show_.getButtonText())));
    progressLine_ = getLocalBounds().removeFromBottom(sh.progressHeight);
}

void ExportPopover::paint(juce::Graphics& g)
{
    const auto& t = ui::Theme::current();
    ui::paintPopover(g, getLocalBounds());
    ui::paintCapsLabel(g, "Export", title_.toFloat());
    for (const auto& [label, area] : labels_)
        if (label.isNotEmpty())
            ui::paintProperties(g, area.withWidth(t.shell.propertyLabelWidth).withSizeKeepingCentre(t.shell.propertyLabelWidth, t.shell.rowHeight),
                                {{label, {}}});
    // Where the files go: the folder (its end, where it differs) and the name.
    const auto s = session_.exportSettings();
    g.setFont(ui::font(ui::TextStyle::Body));
    g.setColour(t.textPrimary);
    const auto folder = session_.exportFolder(s).getFullPathName();
    g.drawText(fromTheEnd(folder, static_cast<float>(folder_.getWidth()), ui::font(ui::TextStyle::Body)), folder_,
               juce::Justification::centredLeft, false);
    g.setColour(t.textSecondary);
    g.drawText(juce::String(model::extensionFor(s.format)), extension_, juce::Justification::centredLeft, false);
    if (!chance_.isEmpty())
    {
        g.setColour(t.textPrimary);
        g.drawText(chanceText(), chance_, juce::Justification::centredLeft, true);
    }
    // The last export's report, or the one running.
    g.setFont(ui::font(ui::TextStyle::Caption));
    const bool clips = last_ && !last_->files.empty() &&
                       (last_->files.front().measured.clippedSamples > 0 || last_->files.front().measured.truePeakDb > 0.0);
    g.setColour(clips && !job_ ? t.roles.warning : t.textSecondary);
    g.drawText(report(), report_, juce::Justification::centredLeft, true);
    g.setColour(t.line);
    for (int y : separators_)
        g.fillRect(t.shell.padding, y, getWidth() - 2 * t.shell.padding, 1);
    if (job_)
        ui::paintProgress(g, progressLine_.toFloat(), job_->progress());
}
} // namespace fd::app
