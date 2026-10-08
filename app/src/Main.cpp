// foss-daw: the DAW (M0 skeleton). See docs/milestones/M0-foundations.md, step 9.

#include "engine/DevicePlugin.h"
#include "ui/MainComponent.h"

namespace
{
class MainWindow final : public juce::DocumentWindow
{
public:
    explicit MainWindow(tracktion::Engine& engine)
        : DocumentWindow("FossDAW", fd::ui::Theme::current().surface0, allButtons)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new fd::app::MainComponent(engine), true);
        setResizable(true, true);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }

    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

class Application final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "FossDAW"; }
    const juce::String getApplicationVersion() override { return "0.0.1"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String&) override
    {
        juce::LookAndFeel::setDefaultLookAndFeel(&lnf_);
        engine_ = std::make_unique<tracktion::Engine>("FossDAW");
        fd::engine::registerDevices(*engine_);
        window_ = std::make_unique<MainWindow>(*engine_);
    }

    void shutdown() override
    {
        window_.reset();
        engine_.reset();
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    }

    void systemRequestedQuit() override { quit(); }

private:
    fd::ui::LookAndFeel lnf_;
    std::unique_ptr<tracktion::Engine> engine_;
    std::unique_ptr<MainWindow> window_;
};
} // namespace

START_JUCE_APPLICATION(Application)
