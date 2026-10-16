//
// In-chat approval for guarded mode (MCP elicitation). forti_apply asks the person in the agent's
// conversation to approve one exact plan, and records an approval only from that person's answer:
//
//   legacy era  (initialize)  the server sends elicitation/create to the client during the tools/call
//                             and waits for the response (stdio.cpp)
//   modern era  (2026-07-28)  the tools/call answers resultType "input_required" with the
//                             elicitation request and an HMAC-sealed requestState; the client asks
//                             the person and retries the identical call with inputResponses and the
//                             requestState (Multi Round-Trip Requests)
//
// Only action "accept" with content.approve === true approves. Anything else (decline, cancel,
// approve false or absent, a malformed answer, a timeout) applies nothing. Approval is never read from
// tool arguments: the model cannot produce an elicitation response, only the client can.
//
// The proposal text quotes device data, which is untrusted: control and bidi characters are removed,
// URL schemes are defanged so clients do not render links, and the message is capped.
//

#ifndef FORTI_MCP_APPROVAL_HPP
#define FORTI_MCP_APPROVAL_HPP

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace forti::mcp {

    // The key under inputRequests / inputResponses, and the boolean the person sets.
    inline constexpr std::string_view approval_input = "approve";
    // How long a person has to answer (legacy wait, modern requestState lifetime).
    inline constexpr std::chrono::seconds approval_timeout{15 * 60};
    inline constexpr std::size_t max_proposal_chars = 4000;

    // A client name reduced to [A-Za-z0-9._-] (anything else becomes '-'), at most 64 characters;
    // "unknown" when nothing is left. Used in the engine actor and approved_by.
    std::string client_label(const nlohmann::json& client_info);

    // Does a client capabilities object allow form-mode elicitation? `elicitation: {}` means form
    // (backward compatibility); `{form: {...}}` says so explicitly; `{url: {}}` alone does not.
    bool supports_form_elicitation(const nlohmann::json& capabilities);

    // The proposal shown to the person, from show_plan's data (device, mode, risk, operations with
    // their redacted diffs): sanitized and at most max_proposal_chars characters.
    std::string proposal_message(const nlohmann::json& plan);

    // elicitation/create params. `with_mode` is false for 2025-06-18, which predates the field.
    nlohmann::json elicitation_params(const nlohmann::json& plan, bool with_mode);

    // What an ElicitResult means.
    enum class Answer { approved, declined, cancelled };
    Answer interpret(const nlohmann::json& elicit_result);

    // Integrity-protected MRTR requestState: base64url(payload) "." base64url(HMAC-SHA256). The key is
    // random per process, so a state outlives neither the process nor its 15-minute expiry, and each
    // state is accepted once (an in-process set of consumed nonces).
    class RequestState {
        std::vector<std::uint8_t> key_;
        std::map<std::string, std::int64_t> consumed_;   // nonce -> expiry, pruned as they lapse

    public:
        RequestState();

        using Clock = std::chrono::system_clock;

        // Adds "exp" (now + approval_timeout) and "nonce" to `payload` and seals it.
        std::string seal(nlohmann::json payload, Clock::time_point now);

        // The verified payload, or why it was refused: "invalid" (malformed or a bad MAC), "expired",
        // "replayed". A verified state is consumed: the same token is refused afterwards.
        std::variant<nlohmann::json, std::string> open(std::string_view token, Clock::time_point now);
    };

}  // namespace forti::mcp

#endif  // FORTI_MCP_APPROVAL_HPP
