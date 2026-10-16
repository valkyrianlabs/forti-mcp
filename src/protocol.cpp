#include "protocol.hpp"

#include "approval.hpp"
#include "budget.hpp"

#include <forti_mcp_version.hpp>

#include <algorithm>
#include <format>
#include <iostream>

namespace forti::mcp {

    using json = nlohmann::json;

    namespace {

        constexpr const char* meta_version = "io.modelcontextprotocol/protocolVersion";
        constexpr const char* meta_capabilities = "io.modelcontextprotocol/clientCapabilities";
        constexpr const char* meta_client = "io.modelcontextprotocol/clientInfo";
        constexpr const char* meta_server = "io.modelcontextprotocol/serverInfo";

        // An error raised while handling a request; becomes a JSON-RPC error response.
        struct RpcError {
            int code;
            std::string message;
            json data = nullptr;
        };

        json server_info() {
            return {{"name", "forti-mcp"}, {"title", "FortiOS (forti-mcp)"}, {"version", FORTI_MCP_VERSION},
                    {"description", "FortiGate configuration and monitoring over the FortiOS REST API"}};
        }

        json supported_versions() {
            json out = json::array({modern_version});
            for (auto version: legacy_versions) out.push_back(version);
            return out;
        }

        json error_response(const json* id, int code, std::string message, json data = nullptr) {
            json error = {{"code", code}, {"message", std::move(message)}};
            if (!data.is_null()) error["data"] = std::move(data);
            json out = {{"jsonrpc", "2.0"}};
            // MCP: the id is omitted (never null) when it could not be read.
            if (id) out["id"] = *id;
            out["error"] = std::move(error);
            return out;
        }

        bool valid_id(const json& id) { return id.is_string() || id.is_number_integer(); }

        // Nesting depth outside strings, so a hostile line cannot exhaust the stack in the recursive parts of
        // the JSON library. Returns false as soon as `limit` is exceeded.
        bool nesting_within(std::string_view text, std::size_t limit) {
            std::size_t depth = 0;
            bool in_string = false, escaped = false;
            for (char c: text) {
                if (in_string) {
                    if (escaped) escaped = false;
                    else if (c == '\\') escaped = true;
                    else if (c == '"') in_string = false;
                } else if (c == '"') {
                    in_string = true;
                } else if (c == '[' || c == '{') {
                    if (++depth > limit) return false;
                } else if ((c == ']' || c == '}') && depth > 0) {
                    --depth;
                }
            }
            return true;
        }

        void log(std::string_view message) { std::cerr << "forti-mcp: " << message << std::endl; }

    }  // namespace

    Server::Server(engine::Engine& engine, ServerOptions options) : tools_(engine, std::move(options)) {}

    std::string Server::oversized_response() {
        return dump(error_response(nullptr, invalid_request,
                                   std::format("Invalid Request: message exceeds {} bytes", max_message_bytes)));
    }

    std::optional<json> parse_message(std::string_view line) {
        if (line.find_first_not_of(" \t\r\n") == std::string_view::npos) return std::nullopt;
        if (line.size() > max_message_bytes || !nesting_within(line, max_nesting)) return std::nullopt;
        json message = json::parse(line, nullptr, false);
        if (message.is_discarded()) return std::nullopt;
        return message;
    }

    std::optional<std::string> Server::handle_line(std::string_view line) {
        auto first = line.find_first_not_of(" \t\r\n");
        if (first == std::string_view::npos) return std::nullopt;   // blank line: not a message
        if (line.size() > max_message_bytes) return oversized_response();
        if (!nesting_within(line, max_nesting))
            return dump(error_response(nullptr, invalid_request,
                                       std::format("Invalid Request: nesting deeper than {} levels", max_nesting)));
        json message = json::parse(line, nullptr, false);
        if (message.is_discarded()) return dump(error_response(nullptr, parse_error, "Parse error: invalid JSON"));
        auto response = handle(message);
        if (!response) return std::nullopt;
        return dump(*response);
    }

    std::optional<json> Server::handle(const json& message) {
        if (message.is_array())
            return error_response(nullptr, invalid_request, "Invalid Request: JSON-RPC batches are not supported");
        if (!message.is_object())
            return error_response(nullptr, invalid_request, "Invalid Request: a message must be a JSON object");

        auto id_it = message.find("id");
        const json* id = id_it != message.end() && valid_id(*id_it) ? &*id_it : nullptr;
        auto method_it = message.find("method");

        if (method_it == message.end()) {
            // A response from the client that arrived after its request stopped waiting (stdio.cpp matches
            // the awaited one): ignore it.
            if (id_it != message.end() && (message.contains("result") || message.contains("error"))) return std::nullopt;
            return error_response(id, invalid_request, "Invalid Request: missing \"method\"");
        }
        if (message.value("jsonrpc", json()) != "2.0")
            return error_response(id, invalid_request, "Invalid Request: \"jsonrpc\" must be \"2.0\"");
        if (!method_it->is_string())
            return error_response(id, invalid_request, "Invalid Request: \"method\" must be a string");
        if (id_it != message.end() && !id)
            return error_response(nullptr, invalid_request, "Invalid Request: \"id\" must be a string or an integer");
        const std::string method = method_it->get<std::string>();

        static const json no_params = json::object();
        auto params_it = message.find("params");
        bool params_ok = params_it == message.end() || params_it->is_object();

        if (!id) {
            // Notification: never answered, even when malformed.
            if (!params_ok) return std::nullopt;
            // notifications/initialized needs no action: legacy requests are served once initialize is answered.
            // notifications/cancelled: requests are handled synchronously and in order, so by the time a
            // cancellation reaches here its request has already been answered; the spec allows ignoring it.
            // (One for a tools/call that is waiting on the person is handled by the transport.)
            return std::nullopt;
        }

        try {
            if (!params_ok) throw RpcError{invalid_params, "Invalid params: \"params\" must be an object"};
            const json& params = params_it == message.end() ? no_params : *params_it;

            const json* meta = nullptr;
            if (auto it = params.find("_meta"); it != params.end()) {
                if (!it->is_object()) throw RpcError{invalid_params, "Invalid params: \"_meta\" must be an object"};
                meta = &*it;
            }
            bool modern = meta && meta->contains(meta_version);

            json result;
            if (method == "initialize") {
                result = initialize(params);
            } else if (modern) {
                const json& version = (*meta)[meta_version];
                if (!version.is_string())
                    throw RpcError{invalid_params, std::format("Invalid params: _meta \"{}\" must be a string", meta_version)};
                if (version != modern_version)
                    throw RpcError{unsupported_protocol_version, "Unsupported protocol version",
                                   {{"supported", supported_versions()}, {"requested", version}}};
                auto caps = meta->find(meta_capabilities);
                if (caps == meta->end() || !caps->is_object())
                    throw RpcError{invalid_params, std::format("Invalid params: _meta \"{}\" (an object) is required", meta_capabilities)};
                if (auto client = meta->find(meta_client); client != meta->end()) {
                    if (!client->is_object())
                        throw RpcError{invalid_params, std::format("Invalid params: _meta \"{}\" must be an object", meta_client)};
                    tools_.identify_client(client_label(*client));
                }

                if (method == "server/discover") result = discover_result();
                else if (method == "tools/list") result = list_tools(params, true);
                else if (method == "tools/call") result = call_tool(params, meta, *id);
                else throw RpcError{method_not_found, std::format("Method not found: {}", method)};
                if (!result.contains("resultType")) result["resultType"] = "complete";   // else input_required
                result["_meta"] = {{meta_server, server_info()}};
            } else if (method == "ping") {
                result = json::object();   // legacy; allowed before initialize too
            } else if (method == "tools/list" || method == "tools/call" || method == "server/discover") {
                if (!legacy_version_ || method == "server/discover")
                    throw RpcError{invalid_params,
                                   std::format("Invalid params: _meta \"{}\" and \"{}\" are required (or send initialize "
                                               "first for a legacy session)", meta_version, meta_capabilities),
                                   {{"supported", supported_versions()}}};
                result = method == "tools/list" ? list_tools(params, false) : call_tool(params, nullptr, *id);
            } else {
                throw RpcError{method_not_found, std::format("Method not found: {}", method)};
            }
            return json{{"jsonrpc", "2.0"}, {"id", *id}, {"result", std::move(result)}};
        } catch (const CallAbandoned&) {
            return std::nullopt;   // cancelled by the client while waiting: no response (the spec's rule)
        } catch (const RpcError& error) {
            return error_response(id, error.code, error.message, error.data);
        } catch (const std::exception& error) {
            log(std::format("internal error in {}: {}", method, error.what()));
            return error_response(id, internal_error, "Internal error");
        }
    }

    json Server::initialize(const json& params) {
        if (legacy_version_) throw RpcError{invalid_request, "Invalid Request: already initialized"};
        auto version = params.find("protocolVersion");
        if (version == params.end() || !version->is_string())
            throw RpcError{invalid_params, "Invalid params: \"protocolVersion\" (a string) is required",
                           {{"supported", supported_versions()}}};
        if (auto caps = params.find("capabilities"); caps == params.end() || !caps->is_object())
            throw RpcError{invalid_params, "Invalid params: \"capabilities\" (an object) is required"};
        auto client = params.find("clientInfo");
        if (client == params.end() || !client->is_object())
            throw RpcError{invalid_params, "Invalid params: \"clientInfo\" (an object) is required"};
        legacy_capabilities_ = params.at("capabilities");
        legacy_client_ = client_label(*client);
        tools_.identify_client(legacy_client_);

        // Legacy negotiation: echo a supported version, else answer with the latest legacy one and let the
        // client decide. Modern versions have no handshake, so they also get the latest legacy version.
        std::string requested = version->get<std::string>();
        bool known = std::ranges::find(legacy_versions, requested) != legacy_versions.end();
        legacy_version_ = known ? requested : std::string(legacy_versions.front());

        json info = server_info();
        if (*legacy_version_ == "2025-06-18") info.erase("description");   // added in 2025-11-25
        return {{"protocolVersion", *legacy_version_},
                {"capabilities", {{"tools", json::object()}}},
                {"serverInfo", std::move(info)},
                {"instructions", tools_.instructions()}};
    }

    json Server::discover_result() const {
        return {{"supportedVersions", supported_versions()},
                {"capabilities", {{"tools", json::object()}}},
                {"instructions", tools_.instructions()},
                {"ttlMs", 300000},
                {"cacheScope", "private"}};
    }

    json Server::list_tools(const json& params, bool modern) const {
        if (auto cursor = params.find("cursor"); cursor != params.end()) {
            // One page: no cursor is ever issued, so any cursor is invalid.
            if (!cursor->is_string()) throw RpcError{invalid_params, "Invalid params: \"cursor\" must be a string"};
            throw RpcError{invalid_params, "Invalid params: unknown cursor"};
        }
        json result = {{"tools", tools_.definitions()}};
        if (modern) {
            // Modern results are cacheable (ttlMs and cacheScope are required there).
            result["ttlMs"] = 300000;
            result["cacheScope"] = "private";
        }
        return result;
    }

    json Server::call_tool(const json& params, const json* meta, const json& id) {
        auto name = params.find("name");
        if (name == params.end() || !name->is_string())
            throw RpcError{invalid_params, "Invalid params: \"name\" (a string) is required"};
        auto arguments = params.find("arguments");
        if (arguments != params.end() && !arguments->is_object())
            throw RpcError{invalid_params, "Invalid params: \"arguments\" must be an object"};
        const json* tool = tools_.find(name->get<std::string>());
        if (!tool) {
            std::string tool_name = name->get<std::string>();
            if (tool_name == "forti_plan" || tool_name == "forti_apply")
                throw RpcError{invalid_params,
                               std::format("Unknown tool: {} (forti-mcp runs with --read-only; plan and apply are disabled)", tool_name)};
            throw RpcError{invalid_params, std::format("Unknown tool: {}", utf8_prefix(tool_name, 128))};
        }

        CallContext context;
        if (meta) {
            context.modern = true;
            context.form_elicitation = supports_form_elicitation(meta->at(meta_capabilities));
            if (auto client = meta->find(meta_client); client != meta->end()) context.client = client_label(*client);
            // A retry after input_required (Multi Round-Trip Requests): both live in params.
            if (auto responses = params.find("inputResponses"); responses != params.end()) {
                if (!responses->is_object()) throw RpcError{invalid_params, "Invalid params: \"inputResponses\" must be an object"};
                context.input_responses = &*responses;
            }
            if (auto state = params.find("requestState"); state != params.end()) {
                if (!state->is_string()) throw RpcError{invalid_params, "Invalid params: \"requestState\" must be a string"};
                context.request_state = &*state;
            }
        } else {
            context.form_elicitation = supports_form_elicitation(legacy_capabilities_);
            context.client = legacy_client_;
            context.elicit_with_mode = legacy_version_ != "2025-06-18";
            if (ask_client_)
                context.elicit = [this, &id](const json& elicit_params) {
                    json request = {{"jsonrpc", "2.0"},
                                    {"id", std::format("forti-mcp-elicit-{}", ++requests_sent_)},
                                    {"method", "elicitation/create"},
                                    {"params", elicit_params}};
                    return ask_client_(request, id);
                };
        }
        return tools_.call(*tool, arguments == params.end() ? json::object() : *arguments, context);
    }

}  // namespace forti::mcp
