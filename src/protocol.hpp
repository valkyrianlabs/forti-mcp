//
// The MCP protocol core: JSON-RPC 2.0 message in, optional message out. No I/O, so the tests drive it
// directly; stdio.cpp feeds it lines.
//
// Two protocol eras are served on one process (MCP 2026-07-28, "Versioning and Compatibility"):
//
//   modern  2026-07-28   stateless; every request carries _meta["io.modelcontextprotocol/protocolVersion"]
//                        and clientCapabilities. Methods: server/discover, tools/list, tools/call.
//   legacy  2025-11-25,  an `initialize` handshake selects legacy semantics for the rest of the process.
//           2025-06-18   Methods: initialize, ping, tools/list, tools/call.
//
// JSON-RPC batches are rejected (MCP removed them in 2025-06-18). Notifications never get a response.
//
// The server sends the client one kind of request: elicitation/create, for guarded-mode approval
// (approval.hpp). Legacy era: during a tools/call, through the AskClient the transport installs
// (stdio.cpp reads stdin until the response arrives, answering pings and queueing everything else;
// notifications/cancelled for that tools/call abandons it, and no response is sent for it). Modern era:
// server-to-client requests no longer exist; the tools/call result is "input_required" instead.
//

#ifndef FORTI_MCP_PROTOCOL_HPP
#define FORTI_MCP_PROTOCOL_HPP

#include "options.hpp"
#include "tools.hpp"

#include <forti/engine/engine.hpp>

#include <nlohmann/json.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace forti::mcp {

    inline constexpr std::string_view modern_version = "2026-07-28";
    inline constexpr std::array<std::string_view, 2> legacy_versions = {"2025-11-25", "2025-06-18"};

    // JSON-RPC and MCP error codes.
    inline constexpr int parse_error = -32700;
    inline constexpr int invalid_request = -32600;
    inline constexpr int method_not_found = -32601;
    inline constexpr int invalid_params = -32602;
    inline constexpr int internal_error = -32603;
    inline constexpr int unsupported_protocol_version = -32022;

    // Input limits: one message per line, at most this large and this deeply nested.
    inline constexpr std::size_t max_message_bytes = 4 * 1024 * 1024;
    inline constexpr std::size_t max_nesting = 64;

    // Parses one line under the input limits; nullopt when it is blank, too large, too deeply nested or
    // not JSON (handle_line answers those).
    std::optional<nlohmann::json> parse_message(std::string_view line);

    // Sends `request` (a JSON-RPC request to the client) and waits for its response, while `call_id`
    // names the tools/call being served (its cancellation ends the wait).
    using AskClient = std::function<ClientReply(const nlohmann::json& request, const nlohmann::json& call_id)>;

    class Server {
        Tools tools_;
        std::optional<std::string> legacy_version_;   // set by initialize
        nlohmann::json legacy_capabilities_ = nlohmann::json::object();
        std::string legacy_client_;
        AskClient ask_client_;
        std::uint64_t requests_sent_ = 0;

    public:
        Server(engine::Engine& engine, ServerOptions options);

        // Installed by the transport; without it no in-chat approval is offered in the legacy era.
        void set_ask_client(AskClient ask) { ask_client_ = std::move(ask); }

        // One parsed message. Returns the response, or nullopt for notifications and client responses.
        std::optional<nlohmann::json> handle(const nlohmann::json& message);

        // One raw line (without the newline): parse, guard, handle, serialize.
        std::optional<std::string> handle_line(std::string_view line);

        // The response to a line longer than max_message_bytes (its id cannot be known).
        static std::string oversized_response();

        [[nodiscard]] const Tools& tools() const { return tools_; }
        [[nodiscard]] Tools& tools() { return tools_; }
        [[nodiscard]] const std::optional<std::string>& legacy_version() const { return legacy_version_; }

    private:
        nlohmann::json initialize(const nlohmann::json& params);
        nlohmann::json discover_result() const;
        nlohmann::json list_tools(const nlohmann::json& params, bool modern) const;
        nlohmann::json call_tool(const nlohmann::json& params, const nlohmann::json* meta, const nlohmann::json& id);
    };

}  // namespace forti::mcp

#endif  // FORTI_MCP_PROTOCOL_HPP
