#pragma once

#include "engine/Session.h"
#include "mcp/MainThread.h"
#include "mcp/protocol/Dispatcher.h"
#include "mcp/tools/Tools.h"
#include "mcp/transport/HttpServer.h"

#include <juce_events/juce_events.h>

#include <map>
#include <mutex>

namespace fd::mcp
{
/// The DAW's MCP server (ADR 0021, docs/design/mcp.md): the tools, resources and prompts over
/// the Session, served on the loopback interface. Off until the user turns it on in the
/// Assistants popover (or `--headless`). Owns its settings, the per-install token and the
/// endpoint file the stdio relay reads, all in the user data folder. Message thread.
class McpServer final : private engine::Session::Listener, private juce::Timer, private juce::AsyncUpdater
{
public:
    static constexpr int defaultPort = 7377; // open question B20

    struct Settings
    {
        bool enabled = false;
        bool readOnly = false;
        int port = defaultPort;
    };
    /// A client seen since the server started.
    struct Client
    {
        std::string name;
        std::string version;
        double lastSeen = 0.0; // seconds, juce::Time::getMillisecondCounterHiRes() / 1000
    };

    McpServer(engine::Session& session, juce::File dataFolder, std::string appVersion);
    ~McpServer() override;
    McpServer(const McpServer&) = delete;
    McpServer& operator=(const McpServer&) = delete;

    /// Starts serving on the first free port from the settings' port (or from `port`; 0 = any
    /// free port, for tests). False if none was free.
    bool start(std::optional<int> port = std::nullopt);
    void stop();
    [[nodiscard]] bool isRunning() const noexcept { return http_ != nullptr && http_->isRunning(); }
    [[nodiscard]] int port() const noexcept { return http_ != nullptr ? http_->port() : 0; }
    [[nodiscard]] std::string url() const;

    [[nodiscard]] const Settings& settings() const noexcept { return settings_; }
    /// Turns the server on or off, and remembers it for the next launch.
    void setEnabled(bool enabled);
    void setReadOnly(bool readOnly);
    [[nodiscard]] const std::string& token() const noexcept { return token_; }
    /// A new token: clients must be set up again (the old one stops working at once).
    void regenerateToken();

    [[nodiscard]] std::vector<Client> clients() const;
    /// A client made a request in the last few seconds (the transport icon lights).
    [[nodiscard]] bool isActive() const;
    /// Ready-to-copy setup: Claude Code's command, and the config of clients that launch a
    /// process (the stdio relay).
    [[nodiscard]] std::string httpSetupLine() const;
    [[nodiscard]] std::string stdioSetupLine() const;

    /// Anything the popover shows changed (on, port, clients, activity). Message thread.
    std::function<void()> onStatusChanged;

    /// For tests: the protocol layer, to drive the tools in process.
    [[nodiscard]] Dispatcher* dispatcher() noexcept { return dispatcher_.get(); }
    [[nodiscard]] const Registry& registry() const noexcept { return registry_; }

    /// `<user data>/FossDAW`.
    [[nodiscard]] static juce::File defaultDataFolder();
    /// Where a running server writes its port and token (the stdio relay reads it).
    [[nodiscard]] static juce::File endpointFile(const juce::File& dataFolder);

private:
    void snapshotChanged() override { resourcesChanged_ = true; }
    void deviceStateChanged(std::uint64_t) override { resourcesChanged_ = true; }
    void deviceViewChanged(std::uint64_t) override { resourcesChanged_ = true; }
    void projectReplaced() override { resourcesChanged_ = true; }
    void timerCallback() override;
    void handleAsyncUpdate() override;
    void publishResourceChanges();
    void watch(const std::vector<std::string>& uris);
    void loadSettings();
    void saveSettings() const;
    void writeEndpoint() const;

    engine::Session& session_;
    juce::File dataFolder_;
    std::string appVersion_;
    Settings settings_;
    std::string token_;

    MainThread main_;
    ToolEnv env_;
    Registry registry_;
    std::unique_ptr<Dispatcher> dispatcher_;
    std::unique_ptr<HttpServer> http_;

    mutable std::mutex clientsMutex_;
    std::map<std::string, Client> clients_;
    double lastActivity_ = -1.0;
    bool shownActive_ = false;

    bool resourcesChanged_ = false;
    std::map<std::string, std::string> resourceTexts_; // last content of each watched resource

    JUCE_DECLARE_WEAK_REFERENCEABLE(McpServer)
};
} // namespace fd::mcp
