#include "mcp/protocol/Dispatcher.h"

#include "mcp/protocol/Encoding.h"
#include "mcp/protocol/Schema.h"

#include <algorithm>

namespace fd::mcp
{
namespace
{
const std::vector<std::string> kLegacyVersions{"2025-11-25", "2025-06-18", "2025-03-26"};
constexpr auto kConfirmationLifetime = std::chrono::minutes(10);
constexpr std::size_t kMaxQueued = 256;
constexpr std::size_t kMaxSessions = 64;
constexpr const char* kVersionKey = "io.modelcontextprotocol/protocolVersion";
constexpr const char* kClientInfoKey = "io.modelcontextprotocol/clientInfo";
constexpr const char* kCapabilitiesKey = "io.modelcontextprotocol/clientCapabilities";
constexpr const char* kServerInfoKey = "io.modelcontextprotocol/serverInfo";
constexpr const char* kReadOnlyMessage =
    "The DAW is in read-only mode for assistants: only tools that read, render or measure run. The user can "
    "switch it off in the DAW's Assistants popover.";
constexpr const char* kDeclinedMessage = "The user declined, so nothing was changed.";

const Json& member(const Json& object, const char* key)
{
    static const Json none;
    if (!object.is_object())
        return none;
    const auto it = object.find(key);
    return it == object.end() ? none : *it;
}

std::string text(const Json& object, const char* key)
{
    const auto& v = member(object, key);
    return v.is_string() ? v.get<std::string>() : std::string();
}

Json objectOf(const Json& object, const char* key)
{
    const auto& v = member(object, key);
    return v.is_object() ? v : Json::object();
}

bool validId(const Json& id)
{
    return id.is_string() || id.is_number_integer();
}

Json notification(std::string method, Json params)
{
    return {{"jsonrpc", "2.0"}, {"method", std::move(method)}, {"params", std::move(params)}};
}

Json toolResultJson(const ToolResult& r)
{
    Json content = Json::array();
    if (!r.summary.empty())
        content.push_back({{"type", "text"}, {"text", r.summary}});
    if (!r.isError)
        content.push_back({{"type", "text"}, {"text", r.structured.dump()}});
    for (const auto& c : r.content)
        content.push_back(c);
    Json body{{"content", std::move(content)}, {"isError", r.isError}};
    if (!r.isError)
        body["structuredContent"] = r.structured;
    return body;
}
} // namespace

const std::vector<std::string>& supportedVersions()
{
    static const std::vector<std::string> versions = [] {
        std::vector<std::string> all{modernVersion};
        all.insert(all.end(), kLegacyVersions.begin(), kLegacyVersions.end());
        return all;
    }();
    return versions;
}

Dispatcher::Dispatcher(ServerIdentity identity, const Registry& registry)
    : identity_(std::move(identity)), registry_(registry)
{
}

Dispatcher::~Dispatcher()
{
    shutdown();
}

// ---- message handling ------------------------------------------------------------------------

std::optional<Outcome> Dispatcher::resolve(const Json& message, const Incoming& incoming, Caller& caller) const
{
    const auto& id = member(message, "id");
    if (text(message, "method") == "initialize")
    {
        caller.modern = false;
        return std::nullopt;
    }
    const auto& meta = member(member(message, "params"), "_meta");
    if (meta.is_object() && meta.contains(kVersionKey))
    {
        const auto& version = meta[kVersionKey];
        if (!version.is_string())
            return error(id, codes::invalidParams, "_meta: the protocol version must be a string", 400);
        const auto& supported = supportedVersions();
        if (std::find(supported.begin(), supported.end(), version.get<std::string>()) == supported.end())
            return error(id, codes::unsupportedVersion, "Unsupported protocol version", 400,
                         {{"supported", supported}, {"requested", version}});
        if (!member(meta, kCapabilitiesKey).is_object())
            return error(id, codes::invalidParams, std::string("_meta: missing ") + kCapabilitiesKey, 400);
        caller.modern = true;
        caller.version = version.get<std::string>();
        caller.capabilities = meta[kCapabilitiesKey];
        caller.clientInfo = objectOf(meta, kClientInfoKey);
        return std::nullopt;
    }

    caller.modern = false;
    if (!incoming.session.empty())
    {
        std::lock_guard lock(mutex_);
        const auto it = sessions_.find(incoming.session);
        if (it == sessions_.end())
            return error(id, codes::invalidRequest, "Unknown or ended session; send initialize again", 404);
        caller.version = it->second.version;
        caller.clientInfo = it->second.clientInfo;
        caller.capabilities = it->second.capabilities;
        caller.session = incoming.session;
        return std::nullopt;
    }
    // A legacy client without a session: served, without the client features it didn't declare.
    caller.version = kLegacyVersions.front();
    return std::nullopt;
}

Plan Dispatcher::plan(const Json& message, const Incoming& incoming) const
{
    Plan plan;
    const auto& id = member(message, "id");
    const auto& params = member(message, "params");
    if (!member(message, "method").is_string() || !validId(id) || !(params.is_null() || params.is_object()))
        return plan; // handle() answers these at once
    Caller caller;
    if (auto rejected = resolve(message, incoming, caller))
    {
        plan.immediate = std::move(*rejected);
        return plan;
    }
    const auto method = text(message, "method");
    if (!knownMethod(method, caller.modern))
    {
        plan.immediate = error(id, codes::methodNotFound, "Method not found: " + method, 404);
        return plan;
    }
    if (method == "subscriptions/listen")
        plan.stream = true;
    else if (method == "tools/call")
        plan.stream = member(member(params, "_meta"), "progressToken").is_string() ||
                      member(member(params, "_meta"), "progressToken").is_number_integer() ||
                      (!caller.modern && supportsElicitation(caller));
    return plan;
}

Outcome Dispatcher::handle(const Json& message, const Incoming& incoming, Stream* stream)
{
    if (!message.is_object())
        return error(nullptr, codes::invalidRequest, "Expected a JSON-RPC message object", 400);
    if (!message.contains("method"))
    {
        if (validId(member(message, "id")) && (message.contains("result") || message.contains("error")))
        {
            handleClientResponse(message);
            return {std::nullopt, 202, {}};
        }
        return error(member(message, "id"), codes::invalidRequest, "Expected a request, a notification or a response", 400);
    }
    if (!message["method"].is_string())
        return error(member(message, "id"), codes::invalidRequest, "\"method\" must be a string", 400);
    const auto method = message["method"].get<std::string>();
    if (!message.contains("id"))
        return {std::nullopt, 202, {}}; // notifications: nothing to do (cancelling is closing the stream)

    const Json id = message["id"];
    if (!validId(id))
        return error(nullptr, codes::invalidRequest, "\"id\" must be a string or an integer", 400);
    const Json params = message.contains("params") && !message["params"].is_null() ? message["params"] : Json::object();
    if (!params.is_object())
        return error(id, codes::invalidParams, "\"params\" must be an object", 400);
    {
        std::lock_guard lock(mutex_);
        if (shutDown_)
            return error(id, codes::internalError, "The DAW's assistant server is shutting down", 503);
    }

    Caller caller;
    if (auto rejected = resolve(message, incoming, caller))
        return std::move(*rejected);
    if (!knownMethod(method, caller.modern))
        return error(id, codes::methodNotFound, "Method not found: " + method, 404);
    if (method == "initialize")
        return initialize(id, params);
    if (onActivity)
        onActivity(caller.clientInfo, method);

    if (method == "ping")
        return {result(id, Json::object(), caller)};
    if (method == "server/discover")
        return {result(id,
                       {{"supportedVersions", supportedVersions()},
                        {"capabilities", capabilities()},
                        {"instructions", identity_.instructions}},
                       caller)};
    if (method == "tools/list")
    {
        Json tools = Json::array();
        for (const auto& t : registry_.tools)
        {
            Json tool{{"name", t.name},
                      {"title", t.title},
                      {"description", t.description},
                      {"inputSchema", t.inputSchema},
                      {"annotations",
                       {{"title", t.title},
                        {"readOnlyHint", t.annotations.readOnly},
                        {"destructiveHint", t.annotations.destructive},
                        {"idempotentHint", t.annotations.idempotent},
                        {"openWorldHint", false}}}};
            if (!t.outputSchema.is_null())
                tool["outputSchema"] = t.outputSchema;
            tools.push_back(std::move(tool));
        }
        return {result(id, {{"tools", std::move(tools)}}, caller)};
    }
    if (method == "tools/call")
        return callTool(id, params, caller, stream);
    if (method == "resources/list")
    {
        Json list = Json::array();
        for (const auto& r : registry_.resources)
            list.push_back({{"uri", r.uri},
                            {"name", r.name},
                            {"title", r.title},
                            {"description", r.description},
                            {"mimeType", r.mimeType}});
        return {result(id, {{"resources", std::move(list)}}, caller)};
    }
    if (method == "resources/templates/list")
    {
        Json list = Json::array();
        for (const auto& r : registry_.templates)
            list.push_back({{"uriTemplate", r.uriTemplate},
                            {"name", r.name},
                            {"title", r.title},
                            {"description", r.description},
                            {"mimeType", r.mimeType}});
        return {result(id, {{"resourceTemplates", std::move(list)}}, caller)};
    }
    if (method == "resources/read")
        return readResource(id, params, caller);
    if (method == "resources/subscribe" || method == "resources/unsubscribe")
        return subscribe(id, params, caller, method == "resources/subscribe");
    if (method == "prompts/list")
    {
        Json list = Json::array();
        for (const auto& p : registry_.prompts)
        {
            Json arguments = Json::array();
            for (const auto& a : p.arguments)
                arguments.push_back({{"name", a.name}, {"description", a.description}, {"required", a.required}});
            list.push_back(
                {{"name", p.name}, {"title", p.title}, {"description", p.description}, {"arguments", std::move(arguments)}});
        }
        return {result(id, {{"prompts", std::move(list)}}, caller)};
    }
    if (method == "prompts/get")
        return getPrompt(id, params, caller);
    if (method == "subscriptions/listen")
        return listen(id, params, caller, stream);
    return error(id, codes::methodNotFound, "Method not found: " + method, 404);
}

Outcome Dispatcher::initialize(const Json& id, const Json& params)
{
    const auto requested = text(params, "protocolVersion");
    const bool known = std::find(kLegacyVersions.begin(), kLegacyVersions.end(), requested) != kLegacyVersions.end();
    Session session;
    session.version = known ? requested : kLegacyVersions.front();
    session.clientInfo = objectOf(params, "clientInfo");
    session.capabilities = objectOf(params, "capabilities");
    session.subscription = std::make_shared<Subscription>();
    session.subscription->modern = false;
    const auto sessionId = randomHex(16);
    {
        std::lock_guard lock(mutex_);
        if (sessions_.size() >= kMaxSessions) // clients that never end their sessions
        {
            const auto oldest = sessions_.begin();
            oldest->second.subscription->ended = true;
            std::erase(subscriptions_, oldest->second.subscription);
            sessions_.erase(oldest);
        }
        sessions_[sessionId] = session;
        subscriptions_.push_back(session.subscription);
    }
    wake_.notify_all();
    if (onActivity)
        onActivity(session.clientInfo, "initialize");

    Caller caller;
    caller.modern = false;
    Outcome outcome{result(id,
                           {{"protocolVersion", session.version},
                            {"capabilities", capabilities()},
                            {"serverInfo", serverInfo()},
                            {"instructions", identity_.instructions}},
                           caller)};
    outcome.newSession = sessionId;
    return outcome;
}

Outcome Dispatcher::callTool(const Json& id, const Json& params, const Caller& caller, Stream* stream)
{
    const auto name = text(params, "name");
    if (name.empty())
        return error(id, codes::invalidParams, "tools/call needs the tool's \"name\"");
    const auto* tool = registry_.findTool(name);
    if (tool == nullptr)
        return error(id, codes::invalidParams, "Unknown tool: " + name);
    Json args = params.contains("arguments") && !params["arguments"].is_null() ? params["arguments"] : Json::object();
    if (!args.is_object())
        return error(id, codes::invalidParams, "\"arguments\" must be an object");
    const auto reply = [&](const ToolResult& r) { return Outcome{result(id, toolResultJson(r), caller)}; };

    if (readOnly_ && !tool->annotations.readOnly)
        return reply(ToolResult::error(kReadOnlyMessage));
    if (auto problem = validate(tool->inputSchema, args))
        return reply(ToolResult::error(*problem + "."));

    CallContext context;
    context.clientInfo = caller.clientInfo;
    if (stream != nullptr)
    {
        context.cancelled = [stream] { return !stream->isOpen(); };
        const auto& token = member(member(params, "_meta"), "progressToken");
        if (token.is_string() || token.is_number_integer())
            context.progress = [stream, token](double done, std::optional<double> total, const std::string& message) {
                Json p{{"progressToken", token}, {"progress", done}};
                if (total)
                    p["total"] = *total;
                if (!message.empty())
                    p["message"] = message;
                stream->send(notification("notifications/progress", std::move(p)));
            };
    }

    // A retry after the user answered (multi round-trip requests).
    const bool elicit = supportsElicitation(caller);
    if (const auto state = text(params, "requestState"); !state.empty())
    {
        std::optional<Confirmation> pending;
        {
            std::lock_guard lock(mutex_);
            if (const auto it = confirmations_.find(state); it != confirmations_.end())
            {
                pending = it->second;
                confirmations_.erase(it);
            }
        }
        if (pending && pending->tool == name && pending->arguments == args &&
            pending->expires > std::chrono::steady_clock::now())
        {
            const auto action = text(member(member(params, "inputResponses"), "confirm"), "action");
            if (action == "accept")
                context.confirmed = true;
            else if (!action.empty())
                return reply(ToolResult::error(kDeclinedMessage));
        }
    }
    // Clients that can't ask the user themselves: the assistant asks, then confirms.
    if (!elicit && member(args, "confirm").is_boolean() && args["confirm"].get<bool>())
        context.confirmed = true;

    const auto run = [&] {
        try
        {
            return tool->call(args, context);
        }
        catch (const std::exception& e)
        {
            return ToolResult::error(std::string("The tool failed: ") + e.what());
        }
    };
    auto outcome = run();
    if (outcome.confirmation && !context.confirmed)
    {
        const Json form{{"mode", "form"},
                        {"message", *outcome.confirmation},
                        {"requestedSchema", {{"type", "object"}, {"properties", Json::object()}}}};
        if (caller.modern && elicit)
        {
            const auto state = randomHex(16);
            {
                std::lock_guard lock(mutex_);
                const auto now = std::chrono::steady_clock::now();
                std::erase_if(confirmations_, [now](const auto& c) { return c.second.expires <= now; });
                confirmations_[state] = {name, args, now + kConfirmationLifetime};
            }
            return {result(id,
                           {{"resultType", "input_required"},
                            {"inputRequests", {{"confirm", {{"method", "elicitation/create"}, {"params", form}}}}},
                            {"requestState", state}},
                           caller)};
        }
        if (!caller.modern && elicit && stream != nullptr)
        {
            const auto answer = askClient(*stream, "elicitation/create", form);
            if (!answer || text(*answer, "action") != "accept")
                return reply(ToolResult::error(kDeclinedMessage));
            context.confirmed = true;
            outcome = run();
        }
        else
            return reply(ToolResult::error(*outcome.confirmation +
                                           " Ask the user; if they agree, call this tool again with \"confirm\": true."));
    }
    return reply(outcome);
}

Outcome Dispatcher::readResource(const Json& id, const Json& params, const Caller& caller)
{
    const auto uri = text(params, "uri");
    if (uri.empty())
        return error(id, codes::invalidParams, "resources/read needs a \"uri\"");
    std::optional<ResourceContents> contents;
    try
    {
        if (registry_.readResource)
            contents = registry_.readResource(uri);
    }
    catch (const std::exception& e)
    {
        return error(id, codes::internalError, e.what(), 500);
    }
    if (!contents)
        return error(id, caller.modern ? codes::invalidParams : codes::legacyResourceNotFound, "Resource not found: " + uri,
                     200, {{"uri", uri}});
    Json item{{"uri", contents->uri}, {"mimeType", contents->mimeType}, {"text", contents->text}};
    return {result(id, {{"contents", Json::array({std::move(item)})}}, caller)};
}

Outcome Dispatcher::getPrompt(const Json& id, const Json& params, const Caller& caller)
{
    const auto name = text(params, "name");
    const auto* prompt = registry_.findPrompt(name);
    if (prompt == nullptr)
        return error(id, codes::invalidParams, "Unknown prompt: " + name);
    const auto args = objectOf(params, "arguments");
    for (const auto& [key, value] : args.items())
        if (!value.is_string())
            return error(id, codes::invalidParams, "Prompt argument \"" + key + "\" must be a string");
    for (const auto& a : prompt->arguments)
        if (a.required && !args.contains(a.name))
            return error(id, codes::invalidParams, "Missing prompt argument \"" + a.name + "\"");
    return {result(id, {{"description", prompt->description}, {"messages", prompt->get(args)}}, caller)};
}

// ---- subscriptions ------------------------------------------------------------------------------

Outcome Dispatcher::listen(const Json& id, const Json& params, const Caller& caller, Stream* stream)
{
    if (stream == nullptr)
        return error(id, codes::internalError, "subscriptions/listen needs a streaming transport", 500);
    auto subscription = std::make_shared<Subscription>();
    subscription->id = id;
    const auto& filter = member(params, "notifications");
    Json acknowledged = Json::object();
    if (const auto& uris = member(filter, "resourceSubscriptions"); uris.is_array())
    {
        Json accepted = Json::array();
        for (const auto& uri : uris)
            if (uri.is_string())
            {
                subscription->resources.insert(uri.get<std::string>());
                accepted.push_back(uri);
            }
        acknowledged["resourceSubscriptions"] = std::move(accepted);
    }
    // The lists of tools, resources and prompts never change: those notifications aren't offered.
    addSubscription(subscription);
    if (onWatch && !subscription->resources.empty())
        onWatch({subscription->resources.begin(), subscription->resources.end()});
    stream->send(notification("notifications/subscriptions/acknowledged",
                              {{"_meta", {{subscriptionIdKey, id}}}, {"notifications", std::move(acknowledged)}}));
    const bool endedByServer = pump(*subscription, *stream);
    removeSubscription(subscription);
    if (!endedByServer)
        return {std::nullopt, 200, {}};
    return {result(id, {{"_meta", {{subscriptionIdKey, id}}}}, caller)};
}

Outcome Dispatcher::subscribe(const Json& id, const Json& params, const Caller& caller, bool add)
{
    const auto uri = text(params, "uri");
    if (uri.empty())
        return error(id, codes::invalidParams, "Needs a resource \"uri\"");
    {
        std::lock_guard lock(mutex_);
        const auto it = sessions_.find(caller.session);
        if (it == sessions_.end())
            return error(id, codes::invalidRequest, "Subscriptions need a session: send initialize first", 400);
        if (add)
            it->second.subscription->resources.insert(uri);
        else
            it->second.subscription->resources.erase(uri);
    }
    if (add && onWatch)
        onWatch({uri});
    return {result(id, Json::object(), caller)};
}

bool Dispatcher::pump(Subscription& subscription, Stream& stream)
{
    std::unique_lock lock(mutex_);
    for (;;)
    {
        if (subscription.ended)
            return true;
        if (!subscription.queue.empty())
        {
            auto message = std::move(subscription.queue.front());
            subscription.queue.pop_front();
            lock.unlock();
            const bool sent = stream.send(message);
            lock.lock();
            if (!sent)
                return false;
            continue;
        }
        if (!stream.isOpen())
            return false;
        if (wake_.wait_for(lock, keepAlive_) == std::cv_status::timeout && subscription.queue.empty() &&
            !subscription.ended)
        {
            lock.unlock();
            const bool alive = stream.keepAlive();
            lock.lock();
            if (!alive)
                return false;
        }
    }
}

bool Dispatcher::hasSession(const std::string& session) const
{
    std::lock_guard lock(mutex_);
    return sessions_.contains(session);
}

bool Dispatcher::serveSessionStream(const std::string& session, Stream& stream)
{
    std::shared_ptr<Subscription> subscription;
    {
        std::lock_guard lock(mutex_);
        const auto it = sessions_.find(session);
        if (it == sessions_.end() || shutDown_)
            return false;
        subscription = it->second.subscription;
        subscription->streaming = true;
    }
    pump(*subscription, stream);
    std::lock_guard lock(mutex_);
    subscription->streaming = false;
    return true;
}

bool Dispatcher::endSession(const std::string& session)
{
    {
        std::lock_guard lock(mutex_);
        const auto it = sessions_.find(session);
        if (it == sessions_.end())
            return false;
        it->second.subscription->ended = true;
        std::erase(subscriptions_, it->second.subscription);
        sessions_.erase(it);
    }
    wake_.notify_all();
    return true;
}

void Dispatcher::addSubscription(const std::shared_ptr<Subscription>& subscription)
{
    std::lock_guard lock(mutex_);
    subscription->ended = shutDown_;
    subscriptions_.push_back(subscription);
}

void Dispatcher::removeSubscription(const std::shared_ptr<Subscription>& subscription)
{
    std::lock_guard lock(mutex_);
    std::erase(subscriptions_, subscription);
}

void Dispatcher::resourceUpdated(const std::string& uri)
{
    {
        std::lock_guard lock(mutex_);
        for (const auto& s : subscriptions_)
        {
            if (s->ended || !s->resources.contains(uri) || (!s->modern && !s->streaming))
                continue;
            Json params{{"uri", uri}};
            if (s->modern)
                params["_meta"] = {{subscriptionIdKey, s->id}};
            s->queue.push_back(notification("notifications/resources/updated", std::move(params)));
            if (s->queue.size() > kMaxQueued)
                s->queue.pop_front();
        }
    }
    wake_.notify_all();
}

std::set<std::string> Dispatcher::watchedResources() const
{
    std::lock_guard lock(mutex_);
    std::set<std::string> uris;
    for (const auto& s : subscriptions_)
        if (!s->ended)
            uris.insert(s->resources.begin(), s->resources.end());
    return uris;
}

// ---- requests to legacy clients -----------------------------------------------------------------

std::optional<Json> Dispatcher::askClient(Stream& stream, const std::string& method, Json params)
{
    auto request = std::make_shared<ClientRequest>();
    std::string requestId;
    {
        std::lock_guard lock(mutex_);
        if (shutDown_)
            return std::nullopt;
        requestId = "fossdaw-" + std::to_string(nextRequest_++);
        clientRequests_[requestId] = request;
    }
    const bool sent =
        stream.send({{"jsonrpc", "2.0"}, {"id", requestId}, {"method", method}, {"params", std::move(params)}});
    std::unique_lock lock(mutex_);
    const auto deadline = std::chrono::steady_clock::now() + clientTimeout_;
    while (sent && !request->done && !shutDown_ && std::chrono::steady_clock::now() < deadline && stream.isOpen())
        wake_.wait_for(lock, std::chrono::milliseconds(100));
    clientRequests_.erase(requestId);
    return request->done ? request->answer : std::nullopt;
}

void Dispatcher::handleClientResponse(const Json& message)
{
    const auto& id = message["id"];
    if (!id.is_string())
        return;
    {
        std::lock_guard lock(mutex_);
        const auto it = clientRequests_.find(id.get<std::string>());
        if (it == clientRequests_.end())
            return;
        if (message.contains("result"))
            it->second->answer = message["result"];
        it->second->done = true;
    }
    wake_.notify_all();
}

void Dispatcher::shutdown()
{
    {
        std::lock_guard lock(mutex_);
        shutDown_ = true;
        for (const auto& s : subscriptions_)
            s->ended = true;
        for (const auto& [id, request] : clientRequests_)
            request->done = true;
    }
    wake_.notify_all();
}

// ---- helpers -------------------------------------------------------------------------------------

Json Dispatcher::result(const Json& id, Json body, const Caller& caller) const
{
    if (caller.modern)
    {
        if (!body.contains("resultType"))
            body["resultType"] = "complete";
        body["_meta"][kServerInfoKey] = serverInfo();
    }
    return {{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(body)}};
}

Outcome Dispatcher::error(const Json& id, int code, const std::string& message, int status, Json data)
{
    Json e{{"code", code}, {"message", message}};
    if (!data.is_null())
        e["data"] = std::move(data);
    Json m{{"jsonrpc", "2.0"}, {"error", std::move(e)}};
    if (validId(id))
        m["id"] = id;
    return {std::move(m), status, {}};
}

bool Dispatcher::knownMethod(const std::string& method, bool modern)
{
    static const std::set<std::string> both{"ping",          "tools/list",   "tools/call",  "resources/list",
                                            "resources/read", "prompts/list", "prompts/get", "resources/templates/list"};
    static const std::set<std::string> modernOnly{"server/discover", "subscriptions/listen"};
    static const std::set<std::string> legacyOnly{"initialize", "resources/subscribe", "resources/unsubscribe"};
    return both.contains(method) || (modern ? modernOnly : legacyOnly).contains(method);
}

bool Dispatcher::supportsElicitation(const Caller& caller)
{
    const auto& e = member(caller.capabilities, "elicitation");
    return e.is_object() && (e.empty() || e.contains("form"));
}

Json Dispatcher::capabilities() const
{
    return {{"tools", {{"listChanged", false}}},
            {"resources", {{"subscribe", true}, {"listChanged", false}}},
            {"prompts", {{"listChanged", false}}}};
}

Json Dispatcher::serverInfo() const
{
    return {{"name", identity_.name}, {"title", identity_.title}, {"version", identity_.version}};
}
} // namespace fd::mcp
