// Segno: the DAW. See docs/design/daw-shell.md.

#include "engine/AppBehaviour.h"
#include "engine/EngineBehaviour.h"
#include "engine/DevicePlugin.h"
#include "engine/Session.h"
#include "mcp/McpServer.h"
#include "mcp/transport/StdioRelay.h"
#include "ui/WindowContent.h"

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
/// The main window (ADR 0040, docs/design/menu-bar.md §4). On Linux and Windows, a title bar of
/// ours with the menus, the project's name and the window buttons, which moves, snaps, maximises
/// and resizes the window through the system; the system's title bar is an option. On macOS, the
/// system's title bar and menu bar.
class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow(fd::engine::Session& session, fd::mcp::McpServer& assistants, bool openGL, int displayIndex,
               const juce::File& settingsFile)
        : DocumentWindow(fd::ui::brand::name, fd::ui::Theme::current().surface0, allButtons)
    {
        setIcon(fd::ui::appIcon(256)); // the taskbar and window icon (ADR 0024)
        auto* content = new fd::app::WindowContent(session, &assistants, !kMac);
        content_ = content;
        auto& shell = content->shell();
        shell.setSettingsFile(settingsFile);
        shell.setVersion(juce::JUCEApplication::getInstance()->getApplicationVersion());
        setUsingNativeTitleBar(kMac || shell.settings().systemTitleBar);
        setTitleBarHeight(0); // ours is in the content
        content->setOwnsWindow(!isUsingNativeTitleBar());
        content->onTitleChanged = [this](const juce::String& title) { setName(title); };

        // What the window does for the shell's actions (Quit, Full screen, the title bar option).
        shell.window.quit = [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); };
        shell.window.isFullScreen = [this] { return isKioskMode(); };
        shell.window.setFullScreen = [this](bool on) {
            juce::Desktop::getInstance().setKioskModeComponent(on ? this : nullptr, false);
            updateWindowState();
        };
        if (!kMac)
            shell.window.setSystemTitleBar = [this](bool system) {
                setUsingNativeTitleBar(system);
                setTitleBarHeight(0);
                content_->setOwnsWindow(!system);
            };
        if (auto* bar = content->titleBar())
        {
            bar->onMinimise = [this] { minimiseButtonPressed(); };
            bar->onMaximise = [this] { maximiseButtonPressed(); };
            bar->onClose = [this] { closeButtonPressed(); };
            // Linux: the window manager moves the window, so its snapping and tiling work (on
            // Windows the system drags the caption itself, from findControlAtPoint).
            bar->onCaptionDrag = [this](const juce::MouseEvent& e) {
                if (auto* peer = getPeer(); peer != nullptr && !isKioskMode())
                    peer->startHostManagedResize(peer->globalToLocal(e.getScreenPosition()), {});
            };
            bar->onCaptionDoubleClick = [this] {
                if (!isKioskMode())
                    maximiseButtonPressed();
            };
            bar->onCaptionMenu = [this] {
                juce::PopupMenu menu;
                menu.addItem("Minimise", [this] { minimiseButtonPressed(); });
                menu.addItem(isFullScreen() ? "Restore" : "Maximise", [this] { maximiseButtonPressed(); });
                menu.addItem("Close", [this] { closeButtonPressed(); });
                menu.addSeparator();
                content_->shell().actions().addMenuItem(menu, "app.systemTitleBar");
                menu.showMenuAsync(juce::PopupMenu::Options().withMousePosition());
            };
        }
#if JUCE_MAC
        appMenu_ = shell.menus().appMenu();
        juce::MenuBarModel::setMacMainMenu(&shell.menus(), &appMenu_);
#endif

        setContentOwned(content, false);
        setResizable(true, false);
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
            gl_.attachTo(*content);
        }
        setVisible(true);
        fd::ui::takeFocus(shell); // shortcuts work at once (the window takes it as it's focused)
    }

    ~MainWindow() override
    {
#if JUCE_MAC
        juce::MenuBarModel::setMacMainMenu(nullptr);
#endif
        gl_.detach();
    }

    [[nodiscard]] fd::app::Shell& shell() const { return content_->shell(); }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

    /// Windows asks what each point of a window without its title bar is: our title bar's caption
    /// and buttons (snapping, the snap layouts over Maximise), the resize edges, else the content.
    WindowControlKind findControlAtPoint(juce::Point<float> p) const override
    {
        if (isUsingNativeTitleBar() || isKioskMode())
            return WindowControlKind::client;
        if (const auto edge = DocumentWindow::findControlAtPoint(p); edge != WindowControlKind::client)
            return edge;
        return content_->controlAt(content_->getLocalPoint(this, p));
    }

    /// Linux: the resize grips just inside the window's edges, outside the content; Windows: a
    /// 1 px outline (the system resizes from outside the window). None while maximised.
    juce::BorderSize<int> getBorderThickness() const override
    {
        if (isUsingNativeTitleBar() || isKioskMode() || isFullScreen())
            return {};
        return juce::BorderSize<int>(kLinux ? fd::ui::Theme::current().shell.resizeBorder : 1);
    }

    void resized() override
    {
        DocumentWindow::resized();
        updateWindowState();
    }

private:
#if JUCE_MAC
    static constexpr bool kMac = true;
#else
    static constexpr bool kMac = false;
#endif
#if JUCE_LINUX || JUCE_BSD
    static constexpr bool kLinux = true;
#else
    static constexpr bool kLinux = false;
#endif

    void updateWindowState()
    {
        if (content_ != nullptr)
            content_->setWindowState(isFullScreen(), isKioskMode());
    }

    juce::OpenGLContext gl_;
    fd::app::WindowContent* content_ = nullptr;
#if JUCE_MAC
    juce::PopupMenu appMenu_;
#endif
};

class Application final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return fd::ui::brand::name; }
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
        engine_ = std::make_unique<tracktion::Engine>(fd::ui::brand::name, std::move(behaviour),
                                                      fd::engine::makeEngineBehaviour());
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
            behaviour_->onWarning = [](const juce::String& m) { std::cerr << "Segno: " << m << std::endl; };
            termination_ = std::make_unique<TerminationWatcher>();
            openProjectArgument();
            if (!mcp_->start())
            {
                std::cerr << "Segno: no free port for assistants from " << mcp_->settings().port << " on" << std::endl;
                setApplicationReturnValue(1);
                quit();
                return;
            }
            std::cerr << "Segno: serving assistants on " << mcp_->url() << std::endl;
            if (stdio)
                startRelay({"127.0.0.1", mcp_->port(), mcp_->token(), "/mcp"});
            return;
        }

        const int display = args.contains("--display") ? args[args.indexOf("--display") + 1].getIntValue() : -1;
        window_ = std::make_unique<MainWindow>(*session_, *mcp_, !commandLine.contains("--software"), display,
                                               fd::mcp::McpServer::defaultDataFolder().getChildFile("app.json"));
        behaviour_->onWarning = [this](const juce::String& m) { window_->shell().showWarning(m); };
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
