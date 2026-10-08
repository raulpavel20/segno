// foss-daw: the DAW. See docs/design/daw-shell.md.

#include "engine/AppBehaviour.h"
#include "engine/DevicePlugin.h"
#include "engine/Session.h"
#include "ui/Shell.h"

#include <juce_opengl/juce_opengl.h>

namespace
{
class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow(fd::engine::Session& session, bool openGL)
        : DocumentWindow("FossDAW", fd::ui::Theme::current().surface0, allButtons)
    {
        setUsingNativeTitleBar(true);
        auto* shell = new fd::app::Shell(session);
        shell_ = shell;
        setContentOwned(shell, false);
        setResizable(true, true);
        setResizeLimits(1024, 640, 16384, 16384);
        const auto area = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userBounds;
        centreWithSize(juce::roundToInt(area.getWidth() * 0.9), juce::roundToInt(area.getHeight() * 0.9));

        // ADR 0015: OpenGL renders the window, driven by the GL thread; --software disables it.
        if (openGL)
        {
            gl_.setComponentPaintingEnabled(true);
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
        window_ = std::make_unique<MainWindow>(*session_, !commandLine.contains("--software"));
        behaviour_->onWarning = [this](const juce::String& m) { window_->shell().showWarning(m); };
    }

    void shutdown() override
    {
        behaviour_->onWarning = nullptr;
        window_.reset();
        session_.reset();
        engine_.reset();
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    }

    void systemRequestedQuit() override { quit(); }

private:
    fd::ui::LookAndFeel lnf_;
    fd::engine::AppBehaviour* behaviour_ = nullptr;
    std::unique_ptr<tracktion::Engine> engine_;
    std::unique_ptr<fd::engine::Session> session_;
    std::unique_ptr<MainWindow> window_;
};
} // namespace

START_JUCE_APPLICATION(Application)
