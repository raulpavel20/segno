// fd-snapshot: renders every device panel offscreen to PNG, once per theme and scale.
// Used for UI review (Claude reads the PNGs) and golden-image tests (ADR 0007).
//
//   fd-snapshot <out-dir> [--theme <name>] [--scale 1,2]
//
// Output: <out-dir>/<theme>/<device>@<scale>x.png

#include <fd/device/Registry.h>
#include <fd_device_ui/fd_device_ui.h>

#include <iostream>

namespace
{
juce::Image renderPanel(const fd::DeviceFactory& factory, const fd::ui::PanelFactory& panelFactory, float scale)
{
    auto device = factory.create();
    fd::ui::LocalParamBinding binding(device->params());
    fd::ui::DeviceUiContext ctx{factory.info(), binding, nullptr};
    auto panel = panelFactory.create(ctx);
    panel->onEnabledChange = [](bool) {};
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

    if (argc < 2)
    {
        std::cerr << "usage: fd-snapshot <out-dir> [--theme <name>] [--scale 1,2]\n";
        return 2;
    }
    const juce::File outDir = juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);
    juce::String onlyTheme;
    juce::StringArray scales{"1", "2"};
    for (int i = 2; i + 1 < argc; i += 2)
    {
        const juce::String k(argv[i]);
        if (k == "--theme")
            onlyTheme = argv[i + 1];
        else if (k == "--scale")
            scales = juce::StringArray::fromTokens(argv[i + 1], ",", "");
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
            for (const auto& s : scales)
            {
                const float scale = s.getFloatValue();
                const auto image = renderPanel(*factory, *panelFactory, scale);
                const auto file = outDir.getChildFile(theme.name)
                                      .getChildFile(juce::String(std::string(factory->info().id)) + "@" + s + "x.png");
                file.getParentDirectory().createDirectory();
                file.deleteFile();
                juce::FileOutputStream out(file);
                if (!out.openedOk() || !juce::PNGImageFormat().writeImageToStream(image, out))
                {
                    std::cerr << "error: cannot write " << file.getFullPathName() << "\n";
                    return 1;
                }
                std::cout << file.getFullPathName() << "\n";
                ++written;
            }
        }
    }
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    return written > 0 ? 0 : 1;
}
