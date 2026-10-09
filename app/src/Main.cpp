// foss-daw: the DAW. See docs/design/daw-shell.md.

#include "engine/AppBehaviour.h"
#include "engine/DevicePlugin.h"
#include "engine/Session.h"
#include "mcp/McpServer.h"
#include "mcp/transport/StdioRelay.h"
#include "ui/Shell.h"

#include <juce_opengl/juce_opengl.h>

#include <nlohmann/json.hpp>

#include <csignal>
#include <cstdio>
#include <iostream>
#include <thread>

#if JUCE_WINDOWS
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace
{
/// An output stream on a file descriptor: the real standard output once it has been moved
/// aside for the MCP stdio transport.
class FdStreamBuf final : public std::streambuf
{
public:
    explicit FdStreamBuf(int fd) : fd_(fd) {}

protected:
    int_type overflow(int_type c) override
    {
        if (c == traits_type::eof())
            return 0;
        const char ch = static_cast<char>(c);
        return xsputn(&ch, 1) == 1 ? c : traits_type::eof();
    }
    std::streamsize xsputn(const char* s, std::streamsize n) override
    {
        std::streamsize done = 0;
        while (done < n)
        {
#if JUCE_WINDOWS
            const auto w = _write(fd_, s + done, static_cast<unsigned>(n - done));
#else
            const auto w = ::write(fd_, s + done, static_cast<size_t>(n - done));
#endif
            if (w <= 0)
                break;
            done += w;
        }
        return done;
    }

private:
    int fd_;
};

/// The stdio transport owns standard output: the protocol goes to the real one, and everything
/// else that prints (the engine's logs) goes to standard error instead.
std::unique_ptr<std::ostream> takeStandardOutput()
{
    std::fflush(stdout);
#if JUCE_WINDOWS
    const int fd = _dup(_fileno(stdout));
    _dup2(_fileno(stderr), _fileno(stdout));
    _setmode(fd, _O_BINARY);
#else
    const int fd = ::dup(STDOUT_FILENO);
    ::dup2(STDERR_FILENO, STDOUT_FILENO);
#endif
    struct Stream final : std::ostream
    {
        explicit Stream(int fd) : std::ostream(nullptr), buffer(fd) { rdbuf(&buffer); }
        FdStreamBuf buffer;
    };
    return std::make_unique<Stream>(fd);
}

/// SIGTERM or SIGINT in headless mode: quit cleanly (the endpoint file goes, connections close).
volatile std::sig_atomic_t terminateRequested = 0;

class TerminationWatcher final : private juce::Timer
{
public:
    TerminationWatcher()
    {
        std::signal(SIGTERM, [](int) { terminateRequested = 1; });
        std::signal(SIGINT, [](int) { terminateRequested = 1; });
        startTimer(100);
    }

private:
    void timerCallback() override
    {
        if (terminateRequested != 0)
        {
            stopTimer();
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    }
};

/// A DAW already serving assistants on this machine (its endpoint file), if one answers.
std::optional<fd::mcp::RelayTarget> runningDaw()
{
    const auto json = nlohmann::json::parse(
        fd::mcp::McpServer::endpointFile(fd::mcp::McpServer::defaultDataFolder()).loadFileAsString().toStdString(), nullptr,
        false);
    if (!json.is_object() || !json.contains("port") || !json["port"].is_number_integer() || !json.contains("token") ||
        !json["token"].is_string())
        return std::nullopt;
    fd::mcp::RelayTarget target;
    target.port = json["port"].get<int>();
    target.token = json["token"].get<std::string>();
    return fd::mcp::probe(target) ? std::optional(target) : std::nullopt;
}
class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow(fd::engine::Session& session, fd::mcp::McpServer& assistants, bool openGL, int displayIndex)
        : DocumentWindow("FossDAW", fd::ui::Theme::current().surface0, allButtons)
    {
        setUsingNativeTitleBar(true);
        auto* shell = new fd::app::Shell(session, &assistants);
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
        const auto args = juce::StringArray::fromTokens(commandLine, true);
        // --mcp-stdio: an assistant launched this process to talk MCP over standard input and
        // output (mcp.md §1). It relays to a running DAW, or runs one headless (B21).
        const bool stdio = args.contains("--mcp-stdio");
        const bool headless = args.contains("--headless") || stdio;
        if (stdio)
        {
            protocolOut_ = takeStandardOutput();
            if (!args.contains("--headless"))
                if (const auto target = runningDaw())
                {
                    startRelay(*target);
                    return;
                }
        }

        juce::LookAndFeel::setDefaultLookAndFeel(&lnf_);
        auto behaviour = std::make_unique<fd::engine::AppBehaviour>();
        behaviour_ = behaviour.get();
        engine_ = std::make_unique<tracktion::Engine>("FossDAW", std::move(behaviour), nullptr);
        fd::engine::registerDevices(*engine_);
        session_ = std::make_unique<fd::engine::Session>(*engine_);
        mcp_ = std::make_unique<fd::mcp::McpServer>(*session_, fd::mcp::McpServer::defaultDataFolder(),
                                                     getApplicationVersion().toStdString());
        // A project file on the command line opens directly.
        const auto openProjectArgument = [this, &args] {
            for (const auto& arg : args)
                if (juce::File::isAbsolutePath(arg.unquoted()) &&
                    juce::File(arg.unquoted()).hasFileExtension(fd::engine::Session::fileExtension))
                    session_->open(juce::File(arg.unquoted()));
        };

        if (headless)
        {
            // --headless: a Session without a window, serving assistants (batch work, tests).
            behaviour_->onWarning = [](const juce::String& m) { std::cerr << "FossDAW: " << m << std::endl; };
            termination_ = std::make_unique<TerminationWatcher>();
            openProjectArgument();
            if (!mcp_->start())
            {
                std::cerr << "FossDAW: no free port for assistants from " << mcp_->settings().port << " on" << std::endl;
                setApplicationReturnValue(1);
                quit();
                return;
            }
            std::cerr << "FossDAW: serving assistants on " << mcp_->url() << std::endl;
            if (stdio)
                startRelay({"127.0.0.1", mcp_->port(), mcp_->token(), "/mcp"});
            return;
        }

        const int display = args.contains("--display") ? args[args.indexOf("--display") + 1].getIntValue() : -1;
        window_ = std::make_unique<MainWindow>(*session_, *mcp_, !commandLine.contains("--software"), display);
        behaviour_->onWarning = [this](const juce::String& m) { window_->shell().showWarning(m); };
        window_->shell().onTitleChanged = [this](const juce::String& title) { window_->setName(title); };
        openProjectArgument();
        if (mcp_->settings().enabled && !mcp_->start())
            window_->shell().showWarning("The assistant server couldn't start: no free port from " +
                                         juce::String(mcp_->settings().port) + " on");
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
        if (behaviour_ != nullptr)
            behaviour_->onWarning = nullptr;
        termination_.reset();
        window_.reset();
        mcp_.reset(); // before the session its tools use
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
    /// Relays standard input and output to `target` on a thread; quits when input ends.
    void startRelay(fd::mcp::RelayTarget target)
    {
        std::thread([target, out = protocolOut_.get()] {
            fd::mcp::relay(target, std::cin, *out);
            juce::MessageManager::callAsync([] { quit(); });
        }).detach(); // may still be reading when the app quits for another reason
    }

    fd::ui::LookAndFeel lnf_;
    fd::engine::AppBehaviour* behaviour_ = nullptr;
    std::unique_ptr<tracktion::Engine> engine_;
    std::unique_ptr<fd::engine::Session> session_;
    std::unique_ptr<fd::mcp::McpServer> mcp_;
    std::unique_ptr<MainWindow> window_;
    std::unique_ptr<std::ostream> protocolOut_; // the real standard output (--mcp-stdio)
    std::unique_ptr<TerminationWatcher> termination_;
};
} // namespace

START_JUCE_APPLICATION(Application)
