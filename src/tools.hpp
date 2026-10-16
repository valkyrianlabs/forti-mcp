//
// The four MCP tools, each a thin mapping onto engine operations:
//
//   forti_discover → Engine::discover     forti_plan  → Engine::plan
//   forti_read     → Engine::read         forti_apply → Engine::apply  ({plan_id}), or Engine::plan then
//                                                       Engine::apply ({operations}: one call)
//
// Arguments are validated against the advertised inputSchema before the engine sees them (the engine
// validates again), the engine envelope becomes structuredContent plus a compact JSON text block, and
// every result is held to the byte budget (budget.hpp).
//
// In guarded mode forti_apply asks the person for approval in the conversation when the client
// supports form elicitation (approval.hpp), and records the approval (engine::approve_plan) only from
// their answer. Without elicitation it relays `forti approve <plan_id>` for a terminal.
//

#ifndef FORTI_MCP_TOOLS_HPP
#define FORTI_MCP_TOOLS_HPP

#include "approval.hpp"
#include "options.hpp"

#include <forti/engine/engine.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <functional>
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

    // What came back from a request the server sent the client (legacy era, elicitation/create).
    struct ClientReply {
        enum class Status {
            answered,    // `message` is the client's JSON-RPC response (result or error)
            cancelled,   // the client cancelled the tools/call this request belongs to
            closed,      // stdin ended or failed before the response arrived
            timed_out,   // no response within approval_timeout
        };
        Status status = Status::closed;
        nlohmann::json message;
    };

    // The tools/call was cancelled by the client while it waited: per the spec no response is sent.
    struct CallAbandoned {};

    // Everything forti_apply needs to know about the request beyond its arguments.
    struct CallContext {
        bool modern = false;
        // The client declared form elicitation (initialize capabilities, or this request's _meta).
        bool form_elicitation = false;
        // client_label() of the client, "" when it did not say.
        std::string client;
        // Legacy era: sends elicitation/create with these params and waits for the reply. Empty when the
        // transport cannot (then no in-chat approval is offered).
        std::function<ClientReply(const nlohmann::json& params)> elicit;
        // 2025-06-18 predates elicitation's "mode" field.
        bool elicit_with_mode = true;
        // Modern era, a retried call (Multi Round-Trip Requests): params.inputResponses / requestState.
        const nlohmann::json* input_responses = nullptr;
        const nlohmann::json* request_state = nullptr;
    };

    class Tools {
        engine::Engine& engine_;
        ServerOptions options_;
        nlohmann::json definitions_;   // the tools/list array
        RequestState state_;
        std::function<RequestState::Clock::time_point()> now_ = [] { return RequestState::Clock::now(); };
        bool actor_set_ = false;

        // One forti_apply call: the envelope, extra text for the model, or an InputRequiredResult.
        struct Applied {
            nlohmann::json envelope;
            std::string note;
            std::optional<nlohmann::json> input_required;
        };
        Applied apply(const nlohmann::json& arguments, const CallContext& context);
        Applied resume(const nlohmann::json& arguments, const CallContext& context);
        Applied ask(const std::string& plan_id, const nlohmann::json& arguments, const CallContext& context);
        static Applied not_approved(const std::string& plan_id, std::string reason);
        Applied approve_and_apply(const std::string& plan_id, const std::string& shown_digest, const CallContext& context);

    public:
        Tools(engine::Engine& engine, ServerOptions options);

        [[nodiscard]] const nlohmann::json& definitions() const { return definitions_; }
        [[nodiscard]] const nlohmann::json* find(std::string_view name) const;

        // Natural-language guidance for the client (initialize / server/discover `instructions`).
        [[nodiscard]] std::string instructions() const;

        // A CallToolResult ({content, structuredContent, isError}) for a listed tool, or for forti_apply
        // in the modern era an InputRequiredResult ({resultType: "input_required", ...}). Engine and
        // argument failures are tool errors (isError: true) so the model can correct itself. Throws
        // CallAbandoned when the client cancelled the call while the person was being asked.
        nlohmann::json call(const nlohmann::json& tool, const nlohmann::json& arguments, const CallContext& context = {});

        // Records who drives the engine ("mcp:<client>") in audit events. The first known name wins: one
        // process serves one client.
        void identify_client(const std::string& label);

        // Tests: the clock requestState expiry is checked against.
        void set_clock(std::function<RequestState::Clock::time_point()> now) { now_ = std::move(now); }
    };

}  // namespace forti::mcp

#endif  // FORTI_MCP_TOOLS_HPP
