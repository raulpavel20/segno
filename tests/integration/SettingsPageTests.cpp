// Settings in the DAW (M3 C1, ADR 0045, docs/design/settings.md): the page opens from its places
// at the right row; search, Changed only and each section's Reset; changing a shortcut; settings
// kept between launches; a recording format applied to what's recorded.

#include "RecordingFixture.h"
#include "model/SettingsCatalogue.h"
#include "ui/Shell.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace recording_test;
using fd::model::SettingsSection;

namespace
{
/// A shell on the recording fixture's session, keeping its settings in a folder of its own.
struct Page
{
    Recording r{"fd-settings-tests"}; // the count-in, the recording format: kept by the engine
    fd::ui::LookAndFeel lnf;
    juce::File folder = r.root.getChildFile("Settings");
    std::unique_ptr<fd::model::SettingsStore> store;
    std::unique_ptr<fd::app::Shell> shell;

    Page()
    {
        juce::LookAndFeel::setDefaultLookAndFeel(&lnf);
        r.session->setPresetsFolder(r.root.getChildFile("Presets"));
        open();
    }
    ~Page()
    {
        shell.reset();
        store.reset();
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    }
    /// A new shell and store on the same folder: the next launch.
    void open()
    {
        shell.reset();
        store = std::make_unique<fd::model::SettingsStore>(folder.getFullPathName().toStdString());
        shell = std::make_unique<fd::app::Shell>(*r.session);
        shell->setSettingsStore(*store);
        shell->setSize(1600, 960);
    }
    fd::app::SettingsView& view() { return shell->settingsView(); }
    juce::ComboBox& combo(const std::string& id)
    {
        const auto controls = view().controlsOf(id);
        REQUIRE_FALSE(controls.empty());
        auto* c = dynamic_cast<juce::ComboBox*>(controls.front());
        REQUIRE(c != nullptr);
        return *c;
    }
    /// Chooses the item whose text starts with `text`, as a click would.
    void choose(const std::string& id, const juce::String& text)
    {
        auto& c = combo(id);
        for (int i = 0; i < c.getNumItems(); ++i)
            if (c.getItemText(i).startsWith(text))
            {
                c.setSelectedId(c.getItemId(i), juce::sendNotificationSync);
                settle();
                return;
            }
        FAIL("no item " << text << " in " << id);
    }
    void settle() { juce::MessageManager::getInstance()->runDispatchLoopUntil(20); }
    /// Types `text` into the search and waits until the page shows something else (two seconds at
    /// most): the field tells its listeners after the change, behind whatever else is queued. In a
    /// full run other tests write into the shared test folders, and 20 ms wasn't always enough.
    void search(const juce::String& text)
    {
        const auto before = view().shownSettings();
        view().searchField().setText(text, true);
        for (int i = 0; i < 100 && view().shownSettings() == before; ++i)
            settle();
    }
    bool inView(const std::string& id)
    {
        const auto row = view().rowBounds(id);
        return !row.isEmpty() && view().visibleArea().contains(row);
    }
};

/// The shortcut the menu bar shows beside `text` (in any menu or submenu); nullopt without the item.
std::optional<juce::String> menuShortcut(fd::app::Shell& shell, const juce::String& text)
{
    for (int i = 0; i < shell.menus().getMenuBarNames().size(); ++i)
    {
        const auto menu = shell.menus().menu(i);
        for (juce::PopupMenu::MenuItemIterator it(menu, true); it.next();)
            if (it.getItem().text == text)
                return it.getItem().shortcutKeyDescription;
    }
    return std::nullopt;
}

void dumpPng(const char* dir, const juce::String& name, const juce::Image& image)
{
    const auto file = juce::File(dir).getChildFile(name);
    file.deleteFile();
    juce::FileOutputStream out(file);
    juce::PNGImageFormat().writeImageToStream(image, out);
}

/// Nothing on the page touches anything else: every control inside the column, none overlapping
/// another or a row's name.
void checkLayout(fd::app::SettingsView& view)
{
    auto& page = view.page();
    std::vector<juce::Rectangle<int>> placed;
    for (auto* c : page.getChildren())
        if (c->isVisible())
        {
            INFO(c->getName() << " " << c->getBounds().toString());
            CHECK(page.getLocalBounds().contains(c->getBounds()));
            CHECK_FALSE(c->getBounds().isEmpty());
            for (const auto& other : placed)
                CHECK_FALSE(other.intersects(c->getBounds()));
            placed.push_back(c->getBounds());
        }
    const auto& s = fd::ui::Theme::current().shell;
    const auto widthOf = [](fd::ui::TextStyle style, const std::string& text)
    {
        return juce::GlyphArrangement::getStringWidth(fd::ui::font(style),
                                                      juce::String::fromUTF8(text.c_str()));
    };
    for (const auto& id : view.shownSettings())
    {
        // Its name and description whole: neither is cut short.
        if (const auto* info = fd::model::findSetting(id))
        {
            INFO(id);
            CHECK(widthOf(fd::ui::TextStyle::Body, info->name) <=
                  static_cast<float>(s.settingsLabelWidth - s.groupGap));
            CHECK(widthOf(fd::ui::TextStyle::Caption, info->description) <=
                  static_cast<float>(view.rowBounds(id).getWidth()));
        }
        const auto label = view.rowBounds(id).withWidth(s.settingsLabelWidth - s.itemGap);
        for (auto* c : view.controlsOf(id))
            if (c->isVisible())
            {
                INFO(id);
                CHECK_FALSE(c->getBounds().intersects(label));
            }
    }
}
} // namespace

TEST_CASE("settings: the page opens from its places at the right row; Escape and the views close it",
          "[integration]")
{
    Page p;
    auto& shell = *p.shell;
    auto& actions = shell.actions();
    REQUIRE(actions.find("app.settings") != nullptr);
    CHECK(actions.shortcut("app.settings") == "Ctrl+,");

    // Options > Settings… (Ctrl+,): the centre shows them, the detail panel hides.
    CHECK(actions.handleKey(juce::KeyPress(',', juce::ModifierKeys::commandModifier, 0)));
    CHECK(shell.settingsShown());
    CHECK(p.view().isVisible());
    CHECK_FALSE(shell.arrangement().isVisible());
    CHECK_FALSE(shell.detail().isVisible());
    CHECK(actions.isToggled("app.settings"));
    CHECK(fd::model::kSettingsSections[0] == SettingsSection::Audio);
    // Settings… again closes them; the arrangement comes back.
    actions.perform("app.settings");
    CHECK_FALSE(shell.settingsShown());
    CHECK(shell.arrangement().isVisible());
    CHECK(shell.detail().isVisible());

    // Escape closes; so does switching to the mixer, which then shows.
    shell.showSettings();
    CHECK(p.view().keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    CHECK_FALSE(shell.settingsShown());
    shell.showSettings();
    actions.perform("view.mixer");
    CHECK_FALSE(shell.settingsShown());
    CHECK(shell.mixer().isVisible());
    // From the mixer and back to it.
    shell.showSettings();
    CHECK_FALSE(shell.mixer().isVisible());
    shell.closeSettings();
    CHECK(shell.mixer().isVisible());
    actions.perform("view.mixer");
    CHECK(shell.arrangement().isVisible());

    // At a row (the Record button's "Recording settings…", the palette): scrolled to it.
    shell.showSettings("assistants.readOnly");
    CHECK(p.inView("assistants.readOnly"));
    CHECK_FALSE(p.inView("audio.driver"));
    shell.closeSettings();
    shell.palette().open(shell);
    shell.palette().setQuery("Settings: Count-in");
    REQUIRE_FALSE(shell.palette().shownIds().empty());
    CHECK(shell.palette().shownIds().front() == "settings:recording.countIn");
    shell.palette().performSelected();
    CHECK(shell.settingsShown());
    CHECK(p.inView("recording.countIn"));
    // Every setting is in the palette.
    shell.palette().open(shell);
    shell.palette().setQuery("Settings:");
    CHECK(shell.palette().shownIds().size() >=
          std::min<size_t>(fd::app::CommandPalette::maxRows, fd::model::settingsCatalogue().size()));
    shell.palette().close();

    // In the menu bar (ADR 0040), Options.
    CHECK(menuShortcut(shell, juce::String::fromUTF8("Settings\xE2\x80\xA6")) == juce::String("Ctrl+,"));

    // While they show, keys that act on the hidden arrangement don't run; playing does.
    shell.showSettings();
    CHECK_FALSE(shell.keyPressed(juce::KeyPress('a', juce::ModifierKeys::commandModifier, 0))); // Select all
    CHECK(shell.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)));
    CHECK(p.r.session->isPlaying());
    p.r.session->stop();
}

TEST_CASE("settings: search narrows the page; Changed only; a section's Reset shows only while it differs",
          "[integration]")
{
    Page p;
    auto& view = p.view();
    p.shell->showSettings();
    auto& session = *p.r.session;

    // Search: name, description, section, extra words; shortcuts by their keys.
    p.search("offset");
    CHECK(view.shownSettings() == std::vector<std::string>{"recording.audioOffset", "recording.midiOffset"});
    p.search("ctrl+z");
    const auto keys = view.shownSettings();
    CHECK(std::find(keys.begin(), keys.end(), "key:edit.undo") != keys.end());
    p.search("nothing like this");
    CHECK(view.shownSettings().empty());
    view.searchField().setText({}, true);
    p.settle();
    CHECK(view.shownSettings().size() > 30);

    // Nothing changed yet: no Reset anywhere but where the fixture's device differs. (The engine
    // keeps the count-in and offsets between runs: put them back first.)
    view.resetSection(SettingsSection::Recording);
    for (const auto section : {SettingsSection::Recording, SettingsSection::Export, SettingsSection::Keys,
                               SettingsSection::Display, SettingsSection::Library, SettingsSection::Plugins,
                               SettingsSection::Assistants})
        CHECK_FALSE(view.resetShown(section));

    // A choice marks its default; choosing another applies at once and saves.
    CHECK(p.combo("recording.format").getText() == "WAV (default)");
    p.choose("recording.format", "FLAC");
    CHECK(p.store->get().recording.format == fd::model::RecordingFormat::Flac);
    CHECK(session.recordingFormat().first == fd::model::RecordingFormat::Flac);
    CHECK(p.folder.getChildFile("settings.json").loadFileAsString().contains("\"flac\""));
    // FLAC holds 16 or 24 bits: the choices follow.
    CHECK(p.combo("recording.bitDepth").getNumItems() == 2);
    p.choose("recording.countIn", "2 bars");
    CHECK(session.countIn() == fd::model::CountIn::TwoBars);
    CHECK(view.resetShown(SettingsSection::Recording));
    CHECK_FALSE(view.resetShown(SettingsSection::Export));

    // Changed only: just those.
    view.setChangedOnly(true);
    auto changed = view.shownSettings();
    CHECK(std::find(changed.begin(), changed.end(), "recording.format") != changed.end());
    CHECK(std::find(changed.begin(), changed.end(), "recording.countIn") != changed.end());
    CHECK(std::find(changed.begin(), changed.end(), "recording.midiOffset") == changed.end());
    CHECK(std::find(changed.begin(), changed.end(), "display.interfaceSize") == changed.end());
    view.setChangedOnly(false);

    // The section's Reset: everything in it back to its defaults; then it hides.
    view.resetSection(SettingsSection::Recording);
    CHECK(p.store->get().recording == fd::model::AppSettings::Recording{});
    CHECK(session.countIn() == fd::model::CountIn::OneBar);
    CHECK_FALSE(view.resetShown(SettingsSection::Recording));

    // An offset: a number, its default in its text; then Reset.
    auto* offset = dynamic_cast<fd::ui::NumberField*>(view.controlsOf("recording.audioOffset").front());
    REQUIRE(offset != nullptr);
    offset->setValue(-20.0, juce::sendNotificationSync);
    CHECK(session.recordingOffsets().first == Catch::Approx(-20.0));
    p.view().refresh();
    CHECK(view.resetShown(SettingsSection::Recording));
    view.resetSection(SettingsSection::Recording);
    CHECK(session.recordingOffsets().first == Catch::Approx(0.0));

    // Export defaults: a new project's export starts from them.
    p.choose("export.format", "AIFF");
    CHECK(session.exportSettings().format == fd::model::ExportFormat::Aiff);
    CHECK(view.resetShown(SettingsSection::Export));
    view.resetSection(SettingsSection::Export);
    CHECK(session.exportSettings().format == fd::model::ExportFormat::Wav);

    // Assistants' switches, here too.
    auto* on = dynamic_cast<fd::ui::Toggle*>(view.controlsOf("assistants.readOnly").front());
    REQUIRE(on != nullptr);
    on->triggerClick();
    p.settle();
    CHECK(p.store->get().assistants.readOnly);
    CHECK(view.resetShown(SettingsSection::Assistants));
    checkLayout(view);
}

TEST_CASE("settings: a shortcut changed runs its action and shows in the menus; a conflict asks first",
          "[integration]")
{
    Page p;
    auto& view = p.view();
    auto& shell = *p.shell;
    auto& actions = shell.actions();
    p.shell->showSettings("keys");
    const juce::KeyPress ctrlH('h', juce::ModifierKeys::commandModifier, 0); // a key no action has
    REQUIRE(actions.actionOn(ctrlH) == nullptr);

    // Click the shortcut, press the keys.
    view.startKeyCapture("view.focus");
    CHECK(view.keyPressed(ctrlH));
    CHECK(actions.shortcut("view.focus") == "Ctrl+H");
    CHECK(p.store->get().keys.at("view.focus") == std::vector<std::string>{"ctrl + H"});
    CHECK(view.resetShown(SettingsSection::Keys));
    shell.closeSettings();
    CHECK(actions.handleKey(ctrlH));
    CHECK(actions.isToggled("view.focus"));
    actions.perform("view.focus");
    CHECK(menuShortcut(shell, "Focus mode") == juce::String("Ctrl+H"));
    CHECK(actions.tooltip("view.focus") == "Focus mode (Ctrl+H)");

    // A key another action has: it asks, in place; Cancel leaves both as they were.
    shell.showSettings("key:view.focus");
    const juce::KeyPress ctrlD('d', juce::ModifierKeys::commandModifier, 0);
    REQUIRE(actions.actionOn(ctrlD) != nullptr);
    const auto owner = actions.actionOn(ctrlD)->id;
    view.startKeyCapture("view.focus");
    view.captureKey(ctrlD);
    CHECK(view.keyConflict().startsWith("Ctrl+D is "));
    checkLayout(view);
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
        dumpPng(dump, "settings-keys-conflict@2x.png",
                shell.createComponentSnapshot(p.view().getBounds(), true, 2.0f));
    view.answerKeyConflict(false);
    CHECK(actions.shortcut("view.focus") == "Ctrl+H");
    CHECK(actions.actionOn(ctrlD)->id == owner);
    // Replace: the other is left without.
    view.startKeyCapture("view.focus");
    view.captureKey(ctrlD);
    view.answerKeyConflict(true);
    CHECK(actions.shortcut("view.focus") == "Ctrl+D");
    CHECK(actions.shortcut(owner).isEmpty());
    // Escape cancels; Backspace clears.
    view.startKeyCapture("view.focus");
    view.captureKey(juce::KeyPress(juce::KeyPress::escapeKey));
    CHECK(actions.shortcut("view.focus") == "Ctrl+D");
    view.startKeyCapture("view.focus");
    view.captureKey(juce::KeyPress(juce::KeyPress::backspaceKey));
    CHECK(actions.shortcut("view.focus").isEmpty());

    // Reset all shortcuts: the defaults back, the Reset gone.
    view.resetSection(SettingsSection::Keys);
    CHECK(actions.shortcut("view.focus") == "Ctrl+Alt+F");
    CHECK(actions.actionOn(ctrlD)->id == owner);
    CHECK(p.store->get().keys.empty());
    CHECK_FALSE(view.resetShown(SettingsSection::Keys));
}

TEST_CASE("settings: kept between launches; the next shell reads them back", "[integration]")
{
    Page p;
    p.shell->showSettings();
    p.choose("recording.format", "AIFF");
    p.choose("display.interfaceSize", "125");
    CHECK(juce::Desktop::getInstance().getGlobalScaleFactor() == Catch::Approx(1.25f));
    p.view().startKeyCapture("view.focus");
    p.view().captureKey(juce::KeyPress('h', juce::ModifierKeys::commandModifier, 0));

    p.open(); // the next launch
    CHECK(p.store->get().recording.format == fd::model::RecordingFormat::Aiff);
    CHECK(p.store->get().display.interfaceSize == 125);
    CHECK(p.shell->actions().shortcut("view.focus") == "Ctrl+H");
    p.shell->showSettings();
    CHECK(p.combo("recording.format").getText() == "AIFF");
    p.choose("display.interfaceSize", "Automatic");
    CHECK(juce::Desktop::getInstance().getGlobalScaleFactor() == Catch::Approx(1.0f));
}

TEST_CASE("settings: a FLAC recording format records FLAC files", "[integration]")
{
    Page p;
    auto& r = p.r;
    p.store->change(
        [](fd::model::AppSettings& s)
        {
            s.recording.format = fd::model::RecordingFormat::Flac;
            s.recording.bitDepth = 16;
        });
    r.input = sine;
    r.session->setCountIn(fd::model::CountIn::Off);
    const auto t = r.track(0);
    REQUIRE_FALSE(r.session->audioInputs().empty());
    r.session->setAudioInput(t, r.session->audioInputs().front().id);
    r.session->setArmed(t, true, true);
    r.recordFor(0.5);
    const auto clips = r.info(t).clips;
    REQUIRE(clips.size() == 1);
    const juce::File recorded(juce::String::fromUTF8(clips[0].sourcePath.c_str()));
    CHECK(recorded.hasFileExtension(".flac"));
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(recorded));
    REQUIRE(reader != nullptr);
    CHECK(reader->getFormatName() == "FLAC file");
    CHECK(reader->bitsPerSample == 16);
    CHECK(reader->lengthInSamples > 0);
}

TEST_CASE("settings: the page at standard and compact widths, searched; nothing touches", "[integration]")
{
    Page p;
    p.shell->showSettings();
    for (const int width : {1600, 1280})
    {
        INFO(width);
        p.shell->setSize(width, 960);
        p.settle();
        checkLayout(p.view());
        const auto& s = fd::ui::Theme::current().shell;
        CHECK(p.view().page().getWidth() <= s.settingsColumnWidth);
        if (const char* dump = std::getenv("FD_DUMP_DIR"))
        {
            dumpPng(dump, "settings-" + juce::String(width) + "@2x.png",
                    p.shell->createComponentSnapshot(p.view().getBounds(), true, 2.0f));
            p.view().showSetting("library.location");                        // the Library section (ADR 0055)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(1700); // past its flash
            dumpPng(dump, "settings-library-" + juce::String(width) + "@2x.png",
                    p.shell->createComponentSnapshot(p.view().getBounds(), true, 2.0f));
            p.view().showSetting("audio.driver");
            p.settle();
        }
    }
    p.view().searchField().setText("buffer", true);
    p.settle();
    checkLayout(p.view());
    if (const char* dump = std::getenv("FD_DUMP_DIR"))
    {
        dumpPng(dump, "settings-search@2x.png",
                p.shell->createComponentSnapshot(p.view().getBounds(), true, 2.0f));
        p.view().searchField().setText("nothing like this", true);
        p.settle();
        dumpPng(dump, "settings-nothing@2x.png",
                p.shell->createComponentSnapshot(p.view().getBounds(), true, 2.0f));
    }
}
