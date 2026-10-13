// fd-snapshot: renders every device panel offscreen to PNG, once per theme and scale.
// Used for UI review (Claude reads the PNGs) and golden-image tests (ADR 0007).
//
//   fd-snapshot <out-dir> [--theme <name>] [--scale 1,2] [--presets <device>]
//   fd-snapshot --app-icon <file.png> [size]
//
// Output: <out-dir>/<theme>/<device>[-collapsed|-folded]@<scale>x.png: every panel expanded,
// with all its sections collapsed, and folded (ADR 0022); a module with pages (ADR 0051) adds
// <device>-page<n>[-collapsed]@<scale>x.png for its other pages. --presets adds the device's panel
// expanded with each of its factory presets loaded (<device>-preset-<name>@<scale>x.png), to
// review its displays in other settings. --app-icon writes the app icon (ADR 0024) from the same
// mark the DAW draws; app/resources/AppIcon.png is made with it.

#include <fd/device/Presets.h>
#include <fd/device/Registry.h>
#include <fd/device/State.h>
#include <fd_device_ui/fd_device_ui.h>

#include <iostream>

namespace
{
enum class View { Expanded, Collapsed, Folded };

/// The DAW's sidechain sources (ADR 0025), so the header looks as it does there.
struct SnapshotSidechain final : fd::ui::SidechainRouting
{
    std::vector<std::string> sources() const override { return {"None", "Kick"}; }
    int selected() const override { return 0; }
    void select(int) override {}
};

juce::Image renderPanel(const fd::DeviceFactory& factory, const fd::ui::PanelFactory& panelFactory,
                        float scale, View view, const fd::DeviceState* state = nullptr, int page = 0,
                        int* pages = nullptr)
{
    auto device = factory.create();
    if (factory.testSetup != nullptr) // e.g. a loaded sample, so its display isn't empty
        factory.testSetup(*device);
    if (state != nullptr)
        (void)fd::applyState(*device, *state);
    fd::ui::LocalParamBinding binding(*device);
    SnapshotSidechain sidechain;
    fd::ui::DeviceUiContext ctx{factory.info(), binding, nullptr, device.get(), &sidechain};
    auto panel = panelFactory.create(ctx);
    panel->onEnabledChange = [](bool) {};
    panel->setFoldable(view == View::Folded); // as in the DAW's chain
    panel->setAllSectionsCollapsed(view == View::Collapsed);
    panel->setFolded(view == View::Folded);
    panel->setPage(page); // a very wide module's (ADR 0051)
    if (pages != nullptr)
        *pages = panel->pageCount();
    panel->setSize(panel->preferredWidth(), fd::ui::Theme::current().devicePanelHeight);

    // A margin of app background around the panel, as it would sit in the device chain.
    constexpr int margin = 12;
    juce::Image image(juce::Image::ARGB, juce::roundToInt(static_cast<float>(panel->getWidth() + 2 * margin) * scale),
                      juce::roundToInt(static_cast<float>(panel->getHeight() + 2 * margin) * scale), true);
    juce::Graphics g(image);
    g.addTransform(juce::AffineTransform::scale(scale));
    g.fillAll(fd::ui::Theme::current().surface1);
    g.setOrigin(margin, margin);
    panel->paintEntireComponent(g, true);
    return image;
}
} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc >= 3 && juce::String(argv[1]) == "--app-icon")
    {
        const auto file = juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]);
        const int size = argc >= 4 ? juce::String(argv[3]).getIntValue() : 1024;
        file.deleteFile(); // streams append
        juce::PNGImageFormat png;
        auto out = file.createOutputStream();
        if (out == nullptr || !png.writeImageToStream(fd::ui::appIcon(size), *out))
        {
            std::cerr << "couldn't write " << file.getFullPathName() << "\n";
            return 1;
        }
        std::cout << "wrote " << file.getFullPathName() << "\n";
        return 0;
    }
    if (argc < 2)
    {
        std::cerr << "usage: fd-snapshot <out-dir> [--theme <name>] [--scale 1,2] [--presets <device>]\n"
                     "       fd-snapshot --app-icon <file.png> [size]\n";
        return 2;
    }
    const juce::File outDir = juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);
    juce::String onlyTheme;
    juce::StringArray scales{"1", "2"};
    std::string presetsOf;
    for (int i = 2; i + 1 < argc; i += 2)
    {
        const juce::String k(argv[i]);
        if (k == "--theme")
            onlyTheme = argv[i + 1];
        else if (k == "--scale")
            scales = juce::StringArray::fromTokens(argv[i + 1], ",", "");
        else if (k == "--presets")
            presetsOf = argv[i + 1];
    }

    fd::ui::LookAndFeel lnf;
    juce::LookAndFeel::setDefaultLookAndFeel(&lnf);

    int written = 0;
    for (const auto& theme : fd::ui::Theme::candidates())
    {
        if (onlyTheme.isNotEmpty() && theme.name != onlyTheme)
            continue;
        fd::ui::Theme::setCurrent(theme);
        lnf.applyTheme();

        for (const auto* factory : fd::allDevices())
        {
            const auto* panelFactory = fd::ui::findPanel(factory->info().id);
            if (panelFactory == nullptr)
                continue;
            const juce::String id(std::string(factory->info().id));
            const auto write = [&](const juce::Image& image, const juce::String& name)
            {
                const auto file = outDir.getChildFile(theme.name).getChildFile(name);
                file.getParentDirectory().createDirectory();
                file.deleteFile();
                juce::FileOutputStream out(file);
                if (!out.openedOk() || !juce::PNGImageFormat().writeImageToStream(image, out))
                {
                    std::cerr << "error: cannot write " << file.getFullPathName() << "\n";
                    return false;
                }
                std::cout << file.getFullPathName() << "\n";
                ++written;
                return true;
            };
            for (const auto& s : scales)
                for (const auto& [view, suffix] :
                     {std::pair{View::Expanded, ""}, std::pair{View::Collapsed, "-collapsed"},
                      std::pair{View::Folded, "-folded"}})
                {
                    int pages = 0;
                    if (!write(
                            renderPanel(*factory, *panelFactory, s.getFloatValue(), view, nullptr, 0, &pages),
                            id + suffix + "@" + s + "x.png"))
                        return 1;
                    // The other pages (ADR 0051), expanded and collapsed: <device>-page2@1x.png.
                    for (int page = 1; view != View::Folded && page < pages; ++page)
                        if (!write(
                                renderPanel(*factory, *panelFactory, s.getFloatValue(), view, nullptr, page),
                                id + "-page" + juce::String(page + 1) + suffix + "@" + s + "x.png"))
                            return 1;
                }
            if (factory->info().id != presetsOf)
                continue;
            for (const auto& file : fd::factoryPresetFiles())
            {
                const auto preset = fd::parsePresetFile(file.text, std::string(file.name));
                if (!preset || preset->state.deviceId != presetsOf)
                    continue;
                for (const auto& s : scales)
                    if (!write(renderPanel(*factory, *panelFactory, s.getFloatValue(), View::Expanded,
                                           &preset->state),
                               id + "-preset-" + juce::String(preset->name) + "@" + s + "x.png"))
                        return 1;
            }
        }
    }
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    return written > 0 ? 0 : 1;
}
