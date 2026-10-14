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
// JSON-RPC batches are rejected (MCP removed them in 2025-06-18). Notifications never get a response;
// notifications/cancelled is accepted and ignored because requests run to completion one at a time.
//

#ifndef FORTI_MCP_PROTOCOL_HPP
#define FORTI_MCP_PROTOCOL_HPP

#include "options.hpp"
#include "tools.hpp"

#include <forti/engine/engine.hpp>

#include <nlohmann/json.hpp>

#include <array>
#include <cstddef>
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

    class Server {
        Tools tools_;
        std::optional<std::string> legacy_version_;   // set by initialize

    public:
        Server(engine::Engine& engine, ServerOptions options);

        // One parsed message. Returns the response, or nullopt for notifications and client responses.
        std::optional<nlohmann::json> handle(const nlohmann::json& message);

        // One raw line (without the newline): parse, guard, handle, serialize.
        std::optional<std::string> handle_line(std::string_view line);

        // The response to a line longer than max_message_bytes (its id cannot be known).
        static std::string oversized_response();

        [[nodiscard]] const Tools& tools() const { return tools_; }
        [[nodiscard]] const std::optional<std::string>& legacy_version() const { return legacy_version_; }

    private:
        nlohmann::json initialize(const nlohmann::json& params);
        nlohmann::json discover_result() const;
        nlohmann::json list_tools(const nlohmann::json& params, bool modern) const;
        nlohmann::json call_tool(const nlohmann::json& params);
    };

}  // namespace forti::mcp

#endif  // FORTI_MCP_PROTOCOL_HPP
