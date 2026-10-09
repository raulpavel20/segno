// fd-spike: rendering spike (ADR 0012, M0 step 10).
//
//   fd-spike                         interactive: switch scene and renderer, read live frame stats
//   fd-spike --benchmark <seconds> [--display <index>]
//                                    maximised window; runs every scene x renderer and prints results

#include "Spike.h"

#include <iostream>

namespace
{
class Window final : public juce::DocumentWindow, private juce::Timer
{
public:
    Window(bool benchmark, double secondsPerRun, int displayIndex)
        : DocumentWindow("Segno rendering spike", fd::ui::Theme::current().surface0, allButtons),
          secondsPerRun_(secondsPerRun)
    {
        setUsingNativeTitleBar(true);
        spike_ = new fd::spike::SpikeComponent();
        setContentOwned(spike_, false);
        setResizable(true, true);
        if (benchmark)
        {
            const auto& displays = juce::Desktop::getInstance().getDisplays().displays;
            const auto& display = displays[juce::jlimit(0, displays.size() - 1, displayIndex)];
            setBounds(display.userBounds.toNearestInt());
            startRun(0);
            startTimer(100);
        }
        else
        {
            centreWithSize(1400, 860);
        }
        setVisible(true);
    }

    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

private:
    struct Run
    {
        fd::spike::SpikeComponent::Scene scene;
        bool openGL;
    };
    const std::vector<Run> runs_{{fd::spike::SpikeComponent::Scene::DeviceChain, false},
                                 {fd::spike::SpikeComponent::Scene::DeviceChain, true},
                                 {fd::spike::SpikeComponent::Scene::Arrangement, false},
                                 {fd::spike::SpikeComponent::Scene::Arrangement, true}};

    void startRun(size_t index)
    {
        run_ = index;
        spike_->setScene(runs_[index].scene);
        spike_->setOpenGL(runs_[index].openGL);
        runStartMs_ = juce::Time::getMillisecondCounterHiRes();
    }

    void timerCallback() override
    {
        if (juce::Time::getMillisecondCounterHiRes() - runStartMs_ < secondsPerRun_ * 1000.0)
            return;
        std::cout << spike_->describe() << "  (" << spike_->stats().frames() << " frames)\n" << std::flush;
        if (run_ + 1 < runs_.size())
            startRun(run_ + 1);
        else
        {
            stopTimer();
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    }

    fd::spike::SpikeComponent* spike_ = nullptr;
    double secondsPerRun_ = 8.0;
    size_t run_ = 0;
    double runStartMs_ = 0.0;
};

class App final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "fd-spike"; }
    const juce::String getApplicationVersion() override { return "0.0.1"; }

    void initialise(const juce::String& commandLine) override
    {
        const auto args = juce::StringArray::fromTokens(commandLine, true);
        const bool benchmark = args.contains("--benchmark");
        const double seconds = benchmark && args.size() > args.indexOf("--benchmark") + 1
                                   ? args[args.indexOf("--benchmark") + 1].getDoubleValue()
                                   : 8.0;
        juce::LookAndFeel::setDefaultLookAndFeel(&lnf_);
        const int display = args.contains("--display") ? args[args.indexOf("--display") + 1].getIntValue() : 0;
        window_ = std::make_unique<Window>(benchmark, seconds > 0.0 ? seconds : 8.0, display);
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
