// foss-daw: the DAW. See docs/design/daw-shell.md.

#include "engine/AppBehaviour.h"
#include "engine/DevicePlugin.h"
#include "engine/Session.h"
#include "ui/Shell.h"

#include <juce_opengl/juce_opengl.h>

#include <iostream>

namespace
{
class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow(fd::engine::Session& session, bool openGL, int displayIndex)
        : DocumentWindow("FossDAW", fd::ui::Theme::current().surface0, allButtons)
    {
        setUsingNativeTitleBar(true);
        auto* shell = new fd::app::Shell(session);
        shell_ = shell;
        setContentOwned(shell, false);
        setResizable(true, true);
        setResizeLimits(1024, 640, 16384, 16384);
        // --display N opens on another display (benchmarks); the primary display by default.
        const auto& displays = juce::Desktop::getInstance().getDisplays().displays;
        const auto& display = juce::isPositiveAndBelow(displayIndex, displays.size())
                                  ? displays.getReference(displayIndex)
                                  : *juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
        const auto area = display.userBounds.toNearestInt();
        setBounds(area.withSizeKeepingCentre(juce::roundToInt(area.getWidth() * 0.9),
                                             juce::roundToInt(area.getHeight() * 0.9)));

        // ADR 0015: OpenGL renders the window, driven by the GL thread; --software disables it.
        if (openGL)
        {
            gl_.setComponentPaintingEnabled(true);
            gl_.setImageCacheSize(256u * 1024u * 1024u); // waveform tiles stay on the GPU
            gl_.setContinuousRepainting(true);
            gl_.attachTo(*shell);
        }
        setVisible(true);
        shell->grabKeyboardFocus();
    }

    ~MainWindow() override { gl_.detach(); }

    [[nodiscard]] fd::app::Shell& shell() const { return *shell_; }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

private:
    juce::OpenGLContext gl_;
    fd::app::Shell* shell_ = nullptr;
};

class Application final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "FossDAW"; }
    const juce::String getApplicationVersion() override { return "0.0.1"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String& commandLine) override
    {
        juce::LookAndFeel::setDefaultLookAndFeel(&lnf_);
        auto behaviour = std::make_unique<fd::engine::AppBehaviour>();
        behaviour_ = behaviour.get();
        engine_ = std::make_unique<tracktion::Engine>("FossDAW", std::move(behaviour), nullptr);
        fd::engine::registerDevices(*engine_);
        session_ = std::make_unique<fd::engine::Session>(*engine_);
        const auto args = juce::StringArray::fromTokens(commandLine, true);
        const int display = args.contains("--display") ? args[args.indexOf("--display") + 1].getIntValue() : -1;
        window_ = std::make_unique<MainWindow>(*session_, !commandLine.contains("--software"), display);
        behaviour_->onWarning = [this](const juce::String& m) { window_->shell().showWarning(m); };
        window_->shell().onTitleChanged = [this](const juce::String& title) { window_->setName(title); };
        // A project file on the command line opens directly.
        for (const auto& arg : juce::StringArray::fromTokens(commandLine, true))
            if (juce::File::isAbsolutePath(arg.unquoted()) &&
                juce::File(arg.unquoted()).hasFileExtension(fd::engine::Session::fileExtension))
                session_->open(juce::File(arg.unquoted()));
        if (commandLine.contains("--stats"))
            window_->shell().setStatsVisible(true);
        if (commandLine.contains("--benchmark"))
            window_->shell().runBenchmark(6.0, [this, commandLine](juce::String report) {
                const auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect(
                    window_->getScreenBounds());
                const auto px = d != nullptr ? d->logicalBounds.toDouble() * d->scale : juce::Rectangle<double>();
                std::cout << (commandLine.contains("--software") ? "software" : "opengl") << " on "
                          << juce::roundToInt(px.getWidth()) << "x" << juce::roundToInt(px.getHeight()) << " @"
                          << (d != nullptr ? d->scale : 1.0) << "x: " << report << std::endl;
                quit();
            });
    }

    void shutdown() override
    {
        behaviour_->onWarning = nullptr;
        window_.reset();
        session_.reset();
        engine_.reset();
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    }

    void systemRequestedQuit() override
    {
        if (window_ == nullptr)
            quit();
        else
            window_->shell().confirmDiscard([] { quit(); });
    }

private:
    fd::ui::LookAndFeel lnf_;
    fd::engine::AppBehaviour* behaviour_ = nullptr;
    std::unique_ptr<tracktion::Engine> engine_;
    std::unique_ptr<fd::engine::Session> session_;
    std::unique_ptr<MainWindow> window_;
};
} // namespace

START_JUCE_APPLICATION(Application)
