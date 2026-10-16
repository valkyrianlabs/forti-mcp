// forti_apply end to end through Server::handle: one-call plan+apply, the modes, and in-chat approval
// in both protocol eras (legacy: elicitation/create through an injected AskClient; modern: MRTR
// input_required and a sealed requestState). The engine runs on a scripted transport with a temporary
// state directory.

#include "change_fixture.hpp"

#include <crypto.hpp>
#include <protocol.hpp>

#include <forti/engine/approval.hpp>

#include <gtest/gtest.h>

#include <chrono>

using namespace forti::test;
using forti::mcp::ClientReply;
using forti::mcp::Server;
using forti::engine::Mode;

namespace {

    struct Elicitation : ChangeFixture {
        std::unique_ptr<Server> server;
        std::vector<json> asked;   // elicitation/create requests the server sent
        std::function<ClientReply(const json&)> answer = [](const json&) {
            return ClientReply{ClientReply::Status::closed, nullptr};
        };

        void start(Mode mode, forti::mcp::ServerOptions options = {}) {
            make(mode);
            server = std::make_unique<Server>(*engine, options);
            server->set_ask_client([this](const json& request, const json&) {
                asked.push_back(request);
                return answer(request);
            });
        }

        void legacy(json capabilities, std::string version = "2025-11-25") {
            auto response = server->handle(request(0, "initialize", initialize_params(std::move(capabilities), std::move(version))));
            ASSERT_TRUE(response && response->contains("result")) << (response ? response->dump() : "none");
        }

        // The person's answer as the client sends it.
        static ClientReply replied(json elicit_result) {
            return {ClientReply::Status::answered,
                    {{"jsonrpc", "2.0"}, {"id", "forti-mcp-elicit-1"}, {"result", std::move(elicit_result)}}};
        }
        static json accept(bool approve) { return {{"action", "accept"}, {"content", {{"approve", approve}}}}; }

        // tools/call forti_apply, legacy era; nullopt when no response was sent.
        std::optional<json> legacy_apply(json arguments, json id = 1) {
            auto response = server->handle(request(id, "tools/call", {{"name", "forti_apply"}, {"arguments", std::move(arguments)}}));
            if (!response) return std::nullopt;
            EXPECT_TRUE(response->contains("result")) << response->dump();
            return (*response)["result"];
        }

        json modern_apply(json arguments, json capabilities, json extra = json::object()) {
            json params = {{"_meta", modern_meta(std::move(capabilities))}, {"name", "forti_apply"}, {"arguments", std::move(arguments)}};
            params.update(extra);
            auto response = server->handle(request(7, "tools/call", params));
            EXPECT_TRUE(response && response->contains("result")) << (response ? response->dump() : "none");
            return (*response)["result"];
        }

        static json error_of(const json& result) {
            EXPECT_EQ(result["isError"], true) << result.dump();
            return result["structuredContent"]["error"];
        }

        static std::string text_of(const json& result) {
            std::string out;
            for (const auto& block: result["content"]) out += block["text"].get<std::string>() + "\n";
            return out;
        }
    };

}  // namespace

// ---- modes without approval ----

TEST_F(Elicitation, AutonomousPlansAndAppliesInOneCall) {
    start(Mode::autonomous);
    script_plan();
    script_apply();
    json result = modern_apply({{"operations", create_address()}}, form_elicitation);
    ASSERT_EQ(result["isError"], false) << result.dump();
    EXPECT_EQ(result["resultType"], "complete");
    const json& data = result["structuredContent"]["data"];
    EXPECT_EQ(data["verified"], 1);
    EXPECT_EQ(data["plan"]["id"], data["plan_id"]);
    EXPECT_TRUE(data["plan"]["summary"].is_string());
    EXPECT_EQ(data["plan"]["operations"][0]["diff"][0]["change"], "add");
    EXPECT_FALSE(data["plan"]["operations"][0].contains("wire"));
    EXPECT_FALSE(data.contains("approval"));
    EXPECT_TRUE(asked.empty());
    EXPECT_EQ(writes(), 1u);
    // The engine actor comes from clientInfo.
    EXPECT_EQ(last_event("apply")["actor"], "mcp:test");
}

TEST_F(Elicitation, ReadOnlyRefusesWithTheModeCommand) {
    start(Mode::read_only);
    legacy(form_elicitation);
    script_plan();
    json result = *legacy_apply({{"operations", create_address()}});
    json error = error_of(result);
    EXPECT_EQ(error["code"], "policy_denied");
    EXPECT_EQ(error["details"]["mode_command"], "forti profile mode lab guarded");
    EXPECT_TRUE(error["details"]["plan"]["id"].is_string());
    EXPECT_TRUE(asked.empty());
    EXPECT_EQ(writes(), 0u);
}

TEST_F(Elicitation, ArgumentsNeedExactlyOneOfPlanIdOrOperations) {
    start(Mode::autonomous);
    legacy(form_elicitation);
    for (json arguments: {json::object(),
                          json{{"plan_id", "20261016-150540-b75ef1"}, {"operations", create_address()}},
                          json{{"plan_id", "20261016-150540-b75ef1"}, {"profile", "lab"}},
                          json{{"plan_id", "20261016-150540-b75ef1"}, {"vdom", "root"}},
                          json{{"operations", json::array()}},
                          json{{"operations", create_address()}, {"approve", true}}}) {
        json error = error_of(*legacy_apply(arguments));
        EXPECT_EQ(error["code"], "usage") << arguments.dump();
    }
    EXPECT_TRUE(transport->requests.empty());
}

// ---- guarded, legacy era (elicitation/create during the call) ----

TEST_F(Elicitation, LegacyAcceptAppliesAndRecordsTheApproval) {
    start(Mode::guarded);
    legacy(form_elicitation);
    answer = [](const json&) { return replied(accept(true)); };
    script_plan();
    script_apply();
    json result = *legacy_apply({{"operations", create_address()}});
    ASSERT_EQ(result["isError"], false) << result.dump();

    ASSERT_EQ(asked.size(), 1u);
    const json& ask = asked[0];
    EXPECT_EQ(ask["method"], "elicitation/create");
    EXPECT_EQ(ask["id"], "forti-mcp-elicit-1");
    EXPECT_EQ(ask["params"]["mode"], "form");
    const json& schema = ask["params"]["requestedSchema"];
    EXPECT_EQ(schema["required"], json({"approve"}));
    EXPECT_EQ(schema["properties"]["approve"]["type"], "boolean");
    EXPECT_EQ(schema["properties"]["approve"]["default"], false);
    std::string message = ask["params"]["message"];
    const json& data = result["structuredContent"]["data"];
    std::string id = data["plan_id"];
    EXPECT_NE(message.find("create firewall/address \"web\""), std::string::npos) << message;
    EXPECT_NE(message.find("subnet = 192.0.2.10 255.255.255.255"), std::string::npos) << message;
    EXPECT_NE(message.find("Device: lab (fortigate.test, serial " + serial), std::string::npos) << message;
    EXPECT_NE(message.find("Mode: guarded"), std::string::npos) << message;
    EXPECT_NE(message.find("Plan " + id), std::string::npos) << message;
    EXPECT_EQ(message.find("://"), std::string::npos);

    EXPECT_EQ(data["verified"], 1);
    EXPECT_EQ(data["approval"]["approved_by"], "mcp:test:elicitation");
    EXPECT_EQ(data["approval"]["via"], "elicitation");
    EXPECT_EQ(writes(), 1u);
    json applied = last_event("apply");
    EXPECT_EQ(applied["outcome"], "ok");
    EXPECT_EQ(applied["approved_by"], "mcp:test:elicitation");
    EXPECT_EQ(applied["actor"], "mcp:test");
    EXPECT_EQ(last_event("approve")["approved_by"], "mcp:test:elicitation");
}

TEST_F(Elicitation, LegacyAnythingButApproveTrueAppliesNothing) {
    start(Mode::guarded);
    legacy({{"elicitation", {{"form", json::object()}, {"url", json::object()}}}});
    struct Case {
        ClientReply reply;
        std::string reason;
    };
    std::vector<Case> cases = {
            {replied({{"action", "decline"}}), "declined"},
            {replied(accept(false)), "declined"},
            {replied({{"action", "accept"}}), "declined"},
            {replied({{"action", "accept"}, {"content", {{"approve", "true"}}}}), "declined"},
            {replied({{"action", "cancel"}}), "cancelled"},
            {replied({{"action", "maybe"}}), "cancelled"},
            {replied("accept"), "cancelled"},
            {ClientReply{ClientReply::Status::answered, {{"jsonrpc", "2.0"}, {"id", "x"}, {"error", {{"code", -1}, {"message", "no"}}}}},
             "cancelled"},
            {ClientReply{ClientReply::Status::closed, nullptr}, "cancelled"},
            {ClientReply{ClientReply::Status::timed_out, nullptr}, "timeout"},
    };
    for (const auto& c: cases) {
        answer = [&](const json&) { return c.reply; };
        script_plan();
        json result = *legacy_apply({{"operations", create_address()}});
        json error = error_of(result);
        EXPECT_EQ(error["code"], "approval_required") << c.reply.message.dump();
        EXPECT_EQ(error["details"]["reason"], c.reason) << c.reply.message.dump();
        EXPECT_FALSE(error["details"].contains("approve_command"));
        EXPECT_NE(text_of(result).find("do not ask for approval again"), std::string::npos);
    }
    EXPECT_EQ(asked.size(), cases.size());
    EXPECT_EQ(writes(), 0u);
    EXPECT_TRUE(transport->script.empty());
}

TEST_F(Elicitation, LegacyCancelledCallGetsNoResponse) {
    start(Mode::guarded);
    legacy(form_elicitation);
    answer = [](const json&) { return ClientReply{ClientReply::Status::cancelled, nullptr}; };
    script_plan();
    EXPECT_FALSE(legacy_apply({{"operations", create_address()}}, 9));
    EXPECT_EQ(asked.size(), 1u);
    EXPECT_EQ(writes(), 0u);
}

TEST_F(Elicitation, ProtocolVersion20250618GetsNoModeField) {
    start(Mode::guarded);
    legacy(form_elicitation, "2025-06-18");
    answer = [](const json&) { return replied({{"action", "decline"}}); };
    script_plan();
    legacy_apply({{"operations", create_address()}});
    ASSERT_EQ(asked.size(), 1u);
    EXPECT_FALSE(asked[0]["params"].contains("mode"));
    EXPECT_TRUE(asked[0]["params"].contains("requestedSchema"));
}

TEST_F(Elicitation, WithoutFormElicitationTheTerminalCommandIsRelayed) {
    for (json capabilities: {json::object(), json{{"elicitation", {{"url", json::object()}}}}, json{{"elicitation", true}}}) {
        start(Mode::guarded);
        legacy(capabilities);
        script_plan();
        json result = *legacy_apply({{"operations", create_address()}});
        json error = error_of(result);
        EXPECT_EQ(error["code"], "approval_required");
        std::string id = error["details"]["plan_id"];
        EXPECT_EQ(error["details"]["approve_command"], "forti approve " + id);
        EXPECT_EQ(error["details"]["watch_command"], "forti approve --watch");
        EXPECT_EQ(error["details"]["plan"]["id"], id);
        std::string text = text_of(result);
        EXPECT_NE(text.find("forti approve " + id), std::string::npos) << text;
        EXPECT_NE(text.find("forti approve --watch"), std::string::npos) << text;
        EXPECT_NE(text.find("{\"plan_id\": \"" + id + "\"}"), std::string::npos) << text;
        EXPECT_TRUE(asked.empty()) << capabilities.dump();
        EXPECT_EQ(writes(), 0u);
    }
}

TEST_F(Elicitation, AnExistingApprovalIsUsedWithoutAsking) {
    start(Mode::guarded);
    legacy(form_elicitation);
    script_plan();
    auto planned = server->handle(request(1, "tools/call", {{"name", "forti_plan"}, {"arguments", {{"operations", create_address()}}}}));
    std::string id = (*planned)["result"]["structuredContent"]["data"]["id"];
    json approved = forti::engine::approve_plan(*engine, id, "cli:alice");
    ASSERT_TRUE(approved["ok"]) << approved.dump();
    script_apply();
    json result = *legacy_apply({{"plan_id", id}});
    ASSERT_EQ(result["isError"], false) << result.dump();
    EXPECT_TRUE(asked.empty());
    EXPECT_FALSE(result["structuredContent"]["data"].contains("plan"));   // the model already has it
    EXPECT_EQ(last_event("apply")["approved_by"], "cli:alice");
}

// The approval is bound to the digest the person was shown: a plan file rewritten (consistently, with
// a recomputed digest) while the prompt is open is refused, not applied.
TEST_F(Elicitation, ApprovalIsBoundToTheShownDigest) {
    start(Mode::guarded);
    legacy(form_elicitation);
    answer = [&](const json& ask) {
        std::string message = ask["params"]["message"];
        std::string id = message.substr(message.rfind("Plan ") + 5);
        auto file = paths.state / "plans" / (id + ".json");
        std::ifstream in(file);
        json plan = json::parse(in);
        in.close();
        plan["operations"][0]["object"]["comment"] = "swapped";
        plan["operations"][0]["wire"]["body"]["comment"] = "swapped";
        json digested = {{"format", plan["format"]}, {"device", plan["device"]}, {"operations", plan["operations"]}};
        plan["digest"] = forti::mcp::crypto::hex(forti::mcp::crypto::sha256(digested.dump(-1, ' ', true)));
        std::ofstream(file) << plan.dump(2);
        return replied(accept(true));
    };
    script_plan();
    json error = error_of(*legacy_apply({{"operations", create_address()}}));
    EXPECT_EQ(error["code"], "stale_plan");   // the engine refuses the approval itself
    EXPECT_EQ(error["details"]["reason"], "plan_changed");
    EXPECT_EQ(writes(), 0u);
    // ... and writes none: the swapped plan cannot be applied later on this answer.
    std::error_code ec;
    for (const auto& entry: std::filesystem::directory_iterator(paths.state / "approvals", ec))
        ADD_FAILURE() << "approval written: " << entry.path();
}

// ---- guarded, modern era (Multi Round-Trip Requests) ----

TEST_F(Elicitation, ModernInputRequiredThenRetryApplies) {
    start(Mode::guarded);
    script_plan();
    json arguments = {{"operations", create_address()}};
    json first = modern_apply(arguments, form_elicitation);
    EXPECT_EQ(first["resultType"], "input_required") << first.dump();
    EXPECT_FALSE(first.contains("content"));
    EXPECT_FALSE(first.contains("isError"));
    const json& input = first["inputRequests"]["approve"];
    EXPECT_EQ(input["method"], "elicitation/create");
    EXPECT_EQ(input["params"]["mode"], "form");
    EXPECT_NE(input["params"]["message"].get<std::string>().find("create firewall/address \"web\""), std::string::npos);
    std::string state = first["requestState"];
    EXPECT_EQ(writes(), 0u);
    EXPECT_TRUE(asked.empty());   // never a server-to-client request in the modern era

    // The retry is the identical call plus the answer: no second plan is made.
    script_apply();
    json retry = modern_apply(arguments, form_elicitation, {{"inputResponses", {{"approve", accept(true)}}}, {"requestState", state}});
    ASSERT_EQ(retry["isError"], false) << retry.dump();
    EXPECT_EQ(retry["resultType"], "complete");
    const json& data = retry["structuredContent"]["data"];
    EXPECT_EQ(data["approval"]["approved_by"], "mcp:test:elicitation");
    EXPECT_EQ(data["plan"]["id"], data["plan_id"]);
    EXPECT_EQ(writes(), 1u);
    EXPECT_EQ(last_event("apply")["approved_by"], "mcp:test:elicitation");

    // The same answer cannot be replayed.
    json replay = modern_apply(arguments, form_elicitation, {{"inputResponses", {{"approve", accept(true)}}}, {"requestState", state}});
    EXPECT_EQ(error_of(replay)["details"]["reason"], "approval_replayed");
    EXPECT_EQ(writes(), 1u);
}

TEST_F(Elicitation, ModernDeclineAppliesNothing) {
    start(Mode::guarded);
    script_plan();
    json arguments = {{"operations", create_address()}};
    std::string state = modern_apply(arguments, form_elicitation)["requestState"];
    json retry = modern_apply(arguments, form_elicitation, {{"inputResponses", {{"approve", {{"action", "decline"}}}}}, {"requestState", state}});
    json error = error_of(retry);
    EXPECT_EQ(error["details"]["reason"], "declined");
    EXPECT_TRUE(error["details"]["plan"]["id"].is_string());
    EXPECT_EQ(writes(), 0u);
}

TEST_F(Elicitation, ModernStateIsVerified) {
    start(Mode::guarded);
    json arguments = {{"operations", create_address()}};
    json approve = {{"approve", accept(true)}};
    auto fresh_state = [&] {
        script_plan();
        return modern_apply(arguments, form_elicitation)["requestState"].get<std::string>();
    };

    std::string state = fresh_state();
    std::string tampered = state;
    tampered[3] = tampered[3] == 'A' ? 'B' : 'A';
    EXPECT_EQ(error_of(modern_apply(arguments, form_elicitation, {{"inputResponses", approve}, {"requestState", tampered}}))["details"]["reason"],
              "invalid_request_state");
    EXPECT_EQ(error_of(modern_apply(arguments, form_elicitation, {{"inputResponses", approve}, {"requestState", "x.y"}}))["details"]["reason"],
              "invalid_request_state");

    // Another change cannot borrow the answer.
    json other = {{"operations", create_address("other")}};
    EXPECT_EQ(error_of(modern_apply(other, form_elicitation, {{"inputResponses", approve}, {"requestState", fresh_state()}}))["details"]["reason"],
              "request_mismatch");

    // Nor another tool.
    auto list = server->handle(request(3, "tools/call", {{"_meta", modern_meta(form_elicitation)}, {"name", "forti_discover"},
                                                         {"arguments", {{"what", "devices"}}},
                                                         {"inputResponses", approve}, {"requestState", fresh_state()}}));
    EXPECT_EQ((*list)["result"]["isError"], false);   // forti_discover ignores MRTR fields; nothing is applied

    // Expired after 15 minutes.
    state = fresh_state();
    server->tools().set_clock([] { return std::chrono::system_clock::now() + std::chrono::minutes(16); });
    EXPECT_EQ(error_of(modern_apply(arguments, form_elicitation, {{"inputResponses", approve}, {"requestState", state}}))["details"]["reason"],
              "approval_expired");
    EXPECT_EQ(writes(), 0u);

    // Malformed MRTR fields are protocol errors.
    for (json extra: {json{{"requestState", 5}}, json{{"inputResponses", "x"}}}) {
        json params = {{"_meta", modern_meta(form_elicitation)}, {"name", "forti_apply"}, {"arguments", arguments}};
        params.update(extra);
        auto response = server->handle(request(4, "tools/call", params));
        EXPECT_EQ((*response)["error"]["code"], -32602) << extra.dump();
    }
}

TEST_F(Elicitation, ModernRetryWithoutTheAnswerAsksAgain) {
    start(Mode::guarded);
    script_plan();
    json arguments = {{"operations", create_address()}};
    std::string state = modern_apply(arguments, form_elicitation)["requestState"];
    json again = modern_apply(arguments, form_elicitation, {{"requestState", state}});
    EXPECT_EQ(again["resultType"], "input_required");
    EXPECT_NE(again["requestState"], state);
    EXPECT_EQ(transport->requests.size(), 1u);   // no second plan
}

TEST_F(Elicitation, ModernWithoutElicitationRelaysTheCommand) {
    start(Mode::guarded);
    script_plan();
    json result = modern_apply({{"operations", create_address()}}, json::object());
    EXPECT_EQ(result["resultType"], "complete");
    json error = error_of(result);
    EXPECT_EQ(error["details"]["approve_command"], "forti approve " + error["details"]["plan_id"].get<std::string>());
}
