#include "mcp/McpServer.h"

#include "mcp/protocol/Encoding.h"

#if !JUCE_WINDOWS
#include <sys/stat.h>
#endif

namespace fd::mcp
{
namespace
{
constexpr double kActiveSeconds = 3.0;

constexpr const char* kInstructions =
    "FossDAW is a music production app: an arrangement DAW with its own instruments and effects (modules). Read "
    "fossdaw://project (or call get_project) first: it lists the tracks, their clips and their module chains. Ids "
    "are typed strings: \"t:\" tracks, \"c:\" clips, \"m:\" modules. Positions are beats (quarter notes) from the "
    "song's start or \"bar.beat.sixteenth\" such as \"5.1.1\"; note times count from their clip's start; pitches "
    "are MIDI numbers or names with C3 = 60. Every call that edits is one undo step the user can take back. Check "
    "what you made with render, which measures a range without changing anything.";

double now()
{
    return juce::Time::getMillisecondCounterHiRes() * 0.001;
}

/// Secrets in files only the user can read.
void writePrivate(const juce::File& file, const std::string& text)
{
    (void)file.replaceWithText(juce::String(text));
#if !JUCE_WINDOWS
    ::chmod(file.getFullPathName().toRawUTF8(), S_IRUSR | S_IWUSR);
#endif
}

template <typename T>
T valueOr(const Json& object, const char* key, T fallback)
{
    if (!object.is_object() || !object.contains(key))
        return fallback;
    try
    {
        return object[key].get<T>();
    }
    catch (const Json::exception&)
    {
        return fallback;
    }
}
} // namespace

juce::File McpServer::defaultDataFolder()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("FossDAW");
}

juce::File McpServer::endpointFile(const juce::File& dataFolder)
{
    return dataFolder.getChildFile("mcp-endpoint.json");
}

McpServer::McpServer(engine::Session& session, juce::File dataFolder, std::string appVersion)
    : session_(session), dataFolder_(std::move(dataFolder)), appVersion_(std::move(appVersion)),
      env_{session, main_, dataFolder_.getChildFile("renders"), {}}
{
    (void)dataFolder_.createDirectory();
    loadSettings();
    registerAll(registry_, env_);
    session_.addListener(this);
    startTimer(100);
}

McpServer::~McpServer()
{
    stopTimer();
    session_.removeListener(this);
    stop();
    cancelPendingUpdate();
}

// ---- settings and token ------------------------------------------------------------------------

void McpServer::loadSettings()
{
    const auto json = Json::parse(dataFolder_.getChildFile("assistants.json").loadFileAsString().toStdString(), nullptr, false);
    settings_.enabled = valueOr(json, "enabled", false);
    settings_.readOnly = valueOr(json, "readOnly", false);
    settings_.port = std::clamp(valueOr(json, "port", defaultPort), 1024, 65535);
    token_ = dataFolder_.getChildFile("mcp-token").loadFileAsString().trim().toStdString();
    if (token_.size() < 32)
        regenerateToken();
}

void McpServer::saveSettings() const
{
    const Json json{{"enabled", settings_.enabled}, {"readOnly", settings_.readOnly}, {"port", settings_.port}};
    (void)dataFolder_.getChildFile("assistants.json").replaceWithText(json.dump(2));
}

void McpServer::regenerateToken()
{
    token_ = randomHex(32);
    writePrivate(dataFolder_.getChildFile("mcp-token"), token_);
    if (http_ != nullptr)
    {
        http_->setToken(token_);
        writeEndpoint();
    }
    {
        std::lock_guard lock(clientsMutex_);
        clients_.clear();
    }
    triggerAsyncUpdate();
}

void McpServer::setEnabled(bool enabled)
{
    settings_.enabled = enabled;
    saveSettings();
    if (enabled)
        (void)start();
    else
        stop();
    triggerAsyncUpdate();
}

void McpServer::setReadOnly(bool readOnly)
{
    settings_.readOnly = readOnly;
    saveSettings();
    if (dispatcher_ != nullptr)
        dispatcher_->setReadOnly(readOnly);
    triggerAsyncUpdate();
}

// ---- serving -------------------------------------------------------------------------------------

bool McpServer::start(std::optional<int> port)
{
    if (isRunning())
        return true;
    main_.reopen();
    dispatcher_ = std::make_unique<Dispatcher>(ServerIdentity{"fossdaw", "FossDAW", appVersion_, kInstructions}, registry_);
    dispatcher_->setReadOnly(settings_.readOnly);
    dispatcher_->onActivity = [this](const Json& info, const std::string&) {
        {
            std::lock_guard lock(clientsMutex_);
            Client c;
            c.name = valueOr<std::string>(info, "title", valueOr<std::string>(info, "name", "An assistant"));
            c.version = valueOr<std::string>(info, "version", "");
            c.lastSeen = lastActivity_ = now();
            clients_[c.name] = c;
        }
        triggerAsyncUpdate();
    };
    dispatcher_->onWatch = [self = juce::WeakReference<McpServer>(this)](const std::vector<std::string>& uris) {
        juce::MessageManager::callAsync([self, uris] {
            if (auto* server = self.get())
                server->watch(uris);
        });
    };
    http_ = std::make_unique<HttpServer>(*dispatcher_);
    HttpOptions options;
    options.port = port.value_or(settings_.port);
    options.token = token_;
    if (http_->start(options) == 0)
    {
        http_.reset();
        dispatcher_.reset();
        return false;
    }
    writeEndpoint();
    triggerAsyncUpdate();
    return true;
}

void McpServer::stop()
{
    if (dispatcher_ == nullptr)
        return;
    main_.close();          // tool calls waiting for the message thread give up
    dispatcher_->shutdown(); // streams end
    http_->stop();           // and the threads finish
    http_.reset();
    dispatcher_.reset();
    const auto endpoint = endpointFile(dataFolder_);
    if (valueOr<std::string>(Json::parse(endpoint.loadFileAsString().toStdString(), nullptr, false), "token", "") == token_)
        (void)endpoint.deleteFile(); // ours (another instance may have written its own since)
    resourceTexts_.clear();
    {
        std::lock_guard lock(clientsMutex_);
        clients_.clear();
        lastActivity_ = -1.0;
    }
    triggerAsyncUpdate();
}

void McpServer::writeEndpoint() const
{
    const Json json{{"url", url()}, {"port", port()}, {"token", token_}};
    writePrivate(endpointFile(dataFolder_), json.dump(2));
}

std::string McpServer::url() const
{
    return "http://127.0.0.1:" + std::to_string(port() != 0 ? port() : settings_.port) + "/mcp";
}

std::string McpServer::httpSetupLine() const
{
    return "claude mcp add --transport http fossdaw " + url() + " --header \"Authorization: Bearer " + token_ + "\"";
}

std::string McpServer::stdioSetupLine() const
{
    const auto exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName().toStdString();
    const Json config{{"mcpServers", {{"fossdaw", {{"command", exe}, {"args", {"--mcp-stdio"}}}}}}};
    return config.dump();
}

std::vector<McpServer::Client> McpServer::clients() const
{
    std::lock_guard lock(clientsMutex_);
    std::vector<Client> out;
    for (const auto& [name, c] : clients_)
        out.push_back(c);
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.lastSeen > b.lastSeen; });
    return out;
}

bool McpServer::isActive() const
{
    std::lock_guard lock(clientsMutex_);
    return lastActivity_ >= 0.0 && now() - lastActivity_ < kActiveSeconds;
}

// ---- change notifications ----------------------------------------------------------------------

void McpServer::timerCallback()
{
    if (resourcesChanged_ && dispatcher_ != nullptr)
    {
        resourcesChanged_ = false;
        publishResourceChanges();
    }
    if (const bool active = isActive(); active != shownActive_)
    {
        shownActive_ = active;
        if (onStatusChanged)
            onStatusChanged();
    }
}

void McpServer::handleAsyncUpdate()
{
    if (onStatusChanged)
        onStatusChanged();
}

void McpServer::watch(const std::vector<std::string>& uris)
{
    for (const auto& uri : uris)
        if (!resourceTexts_.contains(uri))
        {
            const auto contents = registry_.readResource(uri);
            resourceTexts_[uri] = contents ? contents->text : std::string();
        }
}

void McpServer::publishResourceChanges()
{
    for (const auto& uri : dispatcher_->watchedResources())
    {
        if (uri == "fossdaw://transport")
            continue; // changes constantly; read when needed (mcp.md §4)
        const auto contents = registry_.readResource(uri);
        const auto text = contents ? contents->text : std::string();
        const auto it = resourceTexts_.find(uri);
        if (it == resourceTexts_.end())
            resourceTexts_[uri] = text;
        else if (it->second != text)
        {
            it->second = text;
            dispatcher_->resourceUpdated(uri);
        }
    }
}
} // namespace fd::mcp
