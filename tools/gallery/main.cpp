// fd-gallery: the design-gate gallery (ADR 0014, M0 step 11).
//
//   fd-gallery                      interactive window
//   fd-gallery --snapshot <dir>     render every theme to <dir>/<theme>@<scale>x.png and exit

#include "Gallery.h"

#include <iostream>

namespace
{
int snapshot(const juce::File& outDir)
{
    fd::ui::LookAndFeel lnf;
    juce::LookAndFeel::setDefaultLookAndFeel(&lnf);
    int written = 0;
    {
        fd::gallery::GalleryComponent gallery(lnf);
        gallery.setSize(1280, 720);
        for (int i = 0; i < fd::ui::Theme::candidates().size(); ++i)
        {
            gallery.selectTheme(i);
            for (float scale : {1.0f, 2.0f})
            {
                const auto image = gallery.createComponentSnapshot(gallery.getLocalBounds(), true, scale);
                const auto file = outDir.getChildFile(fd::ui::Theme::candidates()[i].name + "@" +
                                                      juce::String(static_cast<int>(scale)) + "x.png");
                outDir.createDirectory();
                file.deleteFile();
                juce::FileOutputStream out(file);
                if (out.openedOk() && juce::PNGImageFormat().writeImageToStream(image, out))
                {
                    std::cout << file.getFullPathName() << "\n";
                    ++written;
                }
            }
        }
    }
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    return written > 0 ? 0 : 1;
}

class Window final : public juce::DocumentWindow
{
public:
    explicit Window(fd::ui::LookAndFeel& lnf)
        : DocumentWindow("foss-daw theme gallery", fd::ui::Theme::current().surface0, allButtons)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new fd::gallery::GalleryComponent(lnf), false);
        setResizable(true, true);
        centreWithSize(1280, 720);
        setVisible(true);
    }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

class App final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "fd-gallery"; }
    const juce::String getApplicationVersion() override { return "0.0.1"; }

    void initialise(const juce::String& commandLine) override
    {
        const auto args = juce::StringArray::fromTokens(commandLine, true);
        if (args.size() >= 2 && args[0] == "--snapshot")
        {
            setApplicationReturnValue(
                snapshot(juce::File::getCurrentWorkingDirectory().getChildFile(args[1].unquoted())));
            quit();
            return;
        }
        juce::LookAndFeel::setDefaultLookAndFeel(&lnf_);
        window_ = std::make_unique<Window>(lnf_);
    }

    void shutdown() override
    {
        window_.reset();
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    }

private:
    fd::ui::LookAndFeel lnf_;
    std::unique_ptr<Window> window_;
};
} // namespace

START_JUCE_APPLICATION(App)
