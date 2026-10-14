//
// The four MCP tools, each a thin mapping onto one engine operation:
//
//   forti_discover → Engine::discover     forti_plan  → Engine::plan
//   forti_read     → Engine::read         forti_apply → Engine::apply  ({plan_id} only)
//
// Arguments are validated against the advertised inputSchema before the engine sees them (the engine
// validates again), the engine envelope becomes structuredContent plus a compact JSON text block, and
// every result is held to the byte budget (budget.hpp).
//

#ifndef FORTI_MCP_TOOLS_HPP
#define FORTI_MCP_TOOLS_HPP

#include "options.hpp"

#include <forti/engine/engine.hpp>

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace forti::mcp {

    // Validates `value` against the JSON Schema subset the tool schemas use (type, enum, properties,
    // required, additionalProperties, items, min/maxItems, min/maxLength, minimum/maximum, pattern).
    // Returns the first problem as "<JSON pointer>: <what>", or nullopt when valid.
    std::optional<std::string> validate(const nlohmann::json& schema, const nlohmann::json& value);

    // Guarded mode: makes sure an approval_required envelope names the command the user runs
    // (details.approve_command, built from a shell-safe plan_id when the engine gave none). Returns the
    // command, or nullopt when the envelope is not approval_required or no safe command exists.
    std::optional<std::string> annotate_approval(nlohmann::json& envelope, const nlohmann::json& arguments);

    class Tools {
        engine::Engine& engine_;
        ServerOptions options_;
        nlohmann::json definitions_;   // the tools/list array

    public:
        Tools(engine::Engine& engine, ServerOptions options);

        [[nodiscard]] const nlohmann::json& definitions() const { return definitions_; }
        [[nodiscard]] const nlohmann::json* find(std::string_view name) const;

        // Natural-language guidance for the client (initialize / server/discover `instructions`).
        [[nodiscard]] std::string instructions() const;

        // A CallToolResult ({content, structuredContent, isError}) for a listed tool. Engine and argument
        // failures are tool errors (isError: true) so the model can correct itself.
        nlohmann::json call(const nlohmann::json& tool, const nlohmann::json& arguments);
    };

}  // namespace forti::mcp

#endif  // FORTI_MCP_TOOLS_HPP
