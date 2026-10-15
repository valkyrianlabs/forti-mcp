// Each tool through the real engine, with the SDK transport scripted (no network).

#include "fake_transport.hpp"

#include <budget.hpp>
#include <protocol.hpp>
#include <tools.hpp>

#include <gtest/gtest.h>

using json = nlohmann::json;
using forti::mcp::Server;
using forti::mcp::ServerOptions;

namespace {

    struct Tools : ::testing::Test {
        std::shared_ptr<forti::fake::FakeTransport> transport = std::make_shared<forti::fake::FakeTransport>();
        forti::engine::Engine engine{forti::fake::options(transport)};

        // tools/call through the protocol layer (modern era); returns the CallToolResult.
        json call(Server& server, std::string name, json arguments) {
            json meta = {{"io.modelcontextprotocol/protocolVersion", "2026-07-28"},
                         {"io.modelcontextprotocol/clientCapabilities", json::object()}};
            auto response = server.handle({{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/call"},
                                           {"params", {{"_meta", meta}, {"name", name}, {"arguments", arguments}}}});
            EXPECT_TRUE(response && response->contains("result")) << (response ? response->dump() : "none");
            return (*response)["result"];
        }

        json call(std::string name, json arguments, ServerOptions options = {}) {
            Server server(engine, options);
            return call(server, std::move(name), std::move(arguments));
        }

        // The result invariants every call must satisfy.
        static json envelope(const json& result) {
            EXPECT_TRUE(result["content"].is_array());
            EXPECT_EQ(result["content"][0]["type"], "text");
            EXPECT_EQ(json::parse(result["content"][0]["text"].get<std::string>()), result["structuredContent"]);
            json env = result["structuredContent"];
            EXPECT_TRUE(env["ok"].is_boolean());
            EXPECT_EQ(result["isError"], !env["ok"].get<bool>());
            if (!env["ok"].get<bool>()) {
                EXPECT_TRUE(env["error"]["code"].is_string());
                EXPECT_TRUE(env["error"]["message"].is_string());
            }
            return env;
        }
    };

    json addresses(std::size_t n, std::size_t comment_bytes = 0) {
        json out = json::array();
        for (std::size_t i = 0; i < n; ++i)
            out.push_back({{"name", std::format("host-{}", i)}, {"subnet", "192.0.2.1 255.255.255.255"},
                           {"type", "ipmask"}, {"comment", std::string(comment_bytes, 'c')}});
        return out;
    }

}  // namespace

// ---- forti_discover ----

TEST_F(Tools, DiscoverDevicesAndResourcesAreLocal) {
    json devices = envelope(call("forti_discover", {{"what", "devices"}}));
    EXPECT_TRUE(devices["ok"]);
    EXPECT_EQ(devices["data"][0]["name"], "lab");
    EXPECT_EQ(devices["data"][0].dump().find("test-key"), std::string::npos);   // never a secret

    json result = call("forti_discover", {{"what", "resources"}, {"query", "address"}, {"kind", "table"}, {"count", 5}});
    json resources = envelope(result);
    EXPECT_TRUE(resources["ok"]);
    EXPECT_LE(resources["data"].size(), 5u);
    EXPECT_EQ(resources["data"][0]["kind"], "table");

    json resource = envelope(call("forti_discover", {{"resource", "firewall/address"}, {"fields", {"name", "subnet"}}}));
    EXPECT_TRUE(resource["ok"]) << resource.dump();
    EXPECT_EQ(resource["data"]["fields"].size(), 2u);
    EXPECT_TRUE(transport->requests.empty());
}

TEST_F(Tools, DiscoverCapabilitiesQueriesTheDevice) {
    transport->reply(200, forti::fake::envelope({{"hostname", "fw1"}, {"model", "FGT60F"}}));
    json env = envelope(call("forti_discover", {{"what", "capabilities"}}));
    EXPECT_TRUE(env["ok"]) << env.dump();
    EXPECT_EQ(env["data"]["profile"], "lab");
    EXPECT_EQ(env["data"]["mode"], "read-only");
    ASSERT_EQ(transport->requests.size(), 1u);
    EXPECT_NE(transport->requests[0].url.find("monitor/system/status"), std::string::npos);
}

TEST_F(Tools, DiscoverErrorsAreToolErrors) {
    json result = call("forti_discover", {{"resource", "firewall/adress"}});
    json env = envelope(result);
    EXPECT_EQ(result["isError"], true);
    EXPECT_EQ(env["error"]["code"], "unknown_resource");
}

// ---- forti_read ----

TEST_F(Tools, ReadListing) {
    transport->reply(200, forti::fake::envelope(addresses(3)));
    json result = call("forti_read", {{"resource", "firewall/address"}, {"fields", {"name", "subnet"}},
                                      {"filter", {"type==ipmask"}}, {"count", 3}});
    json env = envelope(result);
    ASSERT_TRUE(env["ok"]) << env.dump();
    EXPECT_EQ(env["data"].size(), 3u);
    EXPECT_EQ(env["meta"]["resource"], "firewall/address");
    EXPECT_EQ(env["meta"]["profile"], "lab");
    EXPECT_FALSE(env["meta"].contains("truncated"));
    ASSERT_EQ(transport->requests.size(), 1u);
    const std::string& url = transport->requests[0].url;
    EXPECT_NE(url.find("cmdb/firewall/address"), std::string::npos);
    EXPECT_NE(url.find("count=3"), std::string::npos);
}

TEST_F(Tools, ReadOneObjectByIntegerKey) {
    transport->reply(200, forti::fake::envelope(json::array({{{"policyid", 7}, {"name", "allow"}}})));
    json env = envelope(call("forti_read", {{"resource", "firewall/policy"}, {"key", 7}}));
    ASSERT_TRUE(env["ok"]) << env.dump();
    EXPECT_EQ(env["data"]["policyid"], 7);
    EXPECT_NE(transport->requests[0].url.find("firewall/policy/7"), std::string::npos);
}

TEST_F(Tools, ReadArgumentsAreValidatedBeforeTheEngine) {
    for (json arguments: {json{{"resource", "firewall/address"}, {"bogus", 1}},
                          json{{"resource", "firewall/address"}, {"count", "5"}},
                          json{{"resource", "firewall/address"}, {"count", 0}},
                          json{{"resource", "firewall/address"}, {"count", 1001}},
                          json{{"resource", "firewall/address"}, {"fields", "name"}},
                          json{{"resource", "firewall/address"}, {"profile", "bad profile"}},
                          json{{"resource", "firewall/address"}, {"vdom", "a b"}},
                          json{{"resource", "firewall/address"}, {"params", {{"x", json::object()}}}},
                          json{{"key", "x"}},
                          json::object()}) {
        json result = call("forti_read", arguments);
        json env = envelope(result);
        EXPECT_EQ(env["error"]["code"], "usage") << arguments.dump();
        EXPECT_NE(env["error"]["message"].get<std::string>().find("invalid arguments"), std::string::npos);
    }
    EXPECT_TRUE(transport->requests.empty());
}

TEST_F(Tools, ReadDeviceFailures) {
    transport->reply(404, forti::fake::envelope(json::array(), 404));
    EXPECT_EQ(envelope(call("forti_read", {{"resource", "firewall/address"}, {"key", "nope"}}))["error"]["code"], "not_found");
    transport->fail(7, true).fail(7, true).fail(7, true);   // a GET is retried, then gives up
    EXPECT_EQ(envelope(call("forti_read", {{"resource", "firewall/address"}}))["error"]["code"], "connection");
    transport->reply(401, forti::fake::envelope(json::array(), 401));
    EXPECT_EQ(envelope(call("forti_read", {{"resource", "system/global"}}))["error"]["code"], "auth");
}

TEST_F(Tools, DefaultProfileIsInjected) {
    // --profile names a profile that does not exist: the request reaches the engine with it.
    json missing = envelope(call("forti_read", {{"resource", "system/global"}}, {.default_profile = "other"}));
    EXPECT_EQ(missing["error"]["code"], "config") << missing.dump();
    EXPECT_TRUE(transport->requests.empty());
    // An explicit profile wins over the default.
    transport->reply(200, forti::fake::envelope({{"hostname", "fw1"}}));
    json explicit_profile = envelope(call("forti_read", {{"resource", "system/global"}, {"profile", "lab"}},
                                                 {.default_profile = "other"}));
    EXPECT_TRUE(explicit_profile["ok"]) << explicit_profile.dump();
}

// ---- truncation ----

TEST_F(Tools, ReadIsHeldToTheByteBudget) {
    transport->reply(200, forti::fake::envelope(addresses(200, 100)));
    json result = call("forti_read", {{"resource", "firewall/address"}, {"count", 200}}, {.max_bytes = 4096});
    json env = envelope(result);
    ASSERT_TRUE(env["ok"]);
    EXPECT_LE(result["content"][0]["text"].get<std::string>().size(), 4096u);
    const json& meta = env["meta"];
    EXPECT_EQ(meta["truncated"], true);
    std::size_t returned = meta["returned"];
    EXPECT_GT(returned, 5u);
    EXPECT_LT(returned, 200u);
    EXPECT_EQ(env["data"].size(), returned);
    EXPECT_EQ(meta["next_start"], returned);
    EXPECT_EQ(meta["budget_bytes"], 4096);
    EXPECT_EQ(env["data"].back()["name"], std::format("host-{}", returned - 1));   // a prefix, in order

    // The next page starts where the budget stopped.
    transport->reply(200, forti::fake::envelope(addresses(3)));
    call("forti_read", {{"resource", "firewall/address"}, {"start", returned}}, {.max_bytes = 4096});
    EXPECT_NE(transport->requests.back().url.find(std::format("start={}", returned)), std::string::npos);
}

TEST_F(Tools, DiscoverListingIsHeldToTheByteBudget) {
    json result = call("forti_discover", {{"what", "resources"}, {"count", 1000}}, {.max_bytes = 4096});
    json env = envelope(result);
    EXPECT_LE(result["content"][0]["text"].get<std::string>().size(), 4096u);
    EXPECT_EQ(env["meta"]["truncated"], true);
    EXPECT_EQ(env["meta"]["next_start"], env["data"].size());
}

// ---- forti_plan / forti_apply ----

TEST_F(Tools, PlanReturnsAnEnvelope) {
    // Planning is implemented in the engine in parallel; whatever it returns must be a well-formed result.
    transport->reply(200, forti::fake::envelope(json::array()));
    transport->reply(200, forti::fake::envelope(json::array()));
    json result = call("forti_plan", {{"operations", {{{"op", "create"}, {"resource", "firewall/address"},
                                                       {"object", {{"name", "x"}, {"subnet", "192.0.2.0 255.255.255.0"}}}}}}});
    envelope(result);
}

TEST_F(Tools, PlanArgumentsAreStrict) {
    for (json arguments: {json::object(),
                          json{{"operations", json::array()}},
                          json{{"operations", {{{"op", "replace"}, {"resource", "firewall/address"}}}}},
                          json{{"operations", {{{"op", "create"}}}}},
                          json{{"operations", {{{"op", "delete"}, {"resource", "firewall/address"}, {"extra", 1}}}}}}) {
        json env = envelope(call("forti_plan", arguments));
        EXPECT_EQ(env["error"]["code"], "usage") << arguments.dump();
    }
}

TEST_F(Tools, ApplyTakesOnlyAPlanId) {
    for (json arguments: {json::object(), json{{"plan_id", ""}}, json{{"plan_id", 5}},
                          json{{"plan_id", "p1"}, {"profile", "lab"}},
                          json{{"plan_id", "p1"}, {"operations", json::array()}}}) {
        json env = envelope(call("forti_apply", arguments));
        EXPECT_EQ(env["error"]["code"], "usage") << arguments.dump();
    }
    EXPECT_TRUE(transport->requests.empty());
    // A well-formed call reaches the engine (whatever it answers for an unknown plan).
    json result = call("forti_apply", {{"plan_id", "does-not-exist"}}, {.default_profile = "lab"});
    EXPECT_EQ(result["isError"], true);
    EXPECT_TRUE(transport->requests.empty());
}

TEST_F(Tools, ApprovalRequiredCarriesTheHumanCommand) {
    json env = {{"ok", false}, {"error", {{"code", "approval_required"}, {"message", "needs approval"}}}};
    auto command = forti::mcp::annotate_approval(env, {{"plan_id", "p-123_x.y"}});
    ASSERT_TRUE(command);
    EXPECT_EQ(*command, "forti plan approve p-123_x.y");
    EXPECT_EQ(env["error"]["details"]["approve_command"], *command);

    // The engine's own command wins.
    json engine_env = {{"ok", false}, {"error", {{"code", "approval_required"}, {"message", "m"},
                                                 {"details", {{"command", "forti plan approve abc"}}}}}};
    EXPECT_EQ(*forti::mcp::annotate_approval(engine_env, {{"plan_id", "zzz"}}), "forti plan approve abc");

    // Never builds a command from an id a shell would interpret.
    json unsafe = {{"ok", false}, {"error", {{"code", "approval_required"}, {"message", "m"}}}};
    EXPECT_FALSE(forti::mcp::annotate_approval(unsafe, {{"plan_id", "x; rm -rf ~"}}));
    EXPECT_FALSE(unsafe["error"]["details"].contains("approve_command"));

    json other = {{"ok", false}, {"error", {{"code", "policy_denied"}, {"message", "m"}}}};
    EXPECT_FALSE(forti::mcp::annotate_approval(other, {{"plan_id", "p"}}));
    json success = {{"ok", true}, {"data", nullptr}};
    EXPECT_FALSE(forti::mcp::annotate_approval(success, {{"plan_id", "p"}}));
}

// ---- schema validator ----

TEST(Validate, Subset) {
    using forti::mcp::validate;
    json schema = {{"type", "object"},
                   {"additionalProperties", false},
                   {"required", {"a"}},
                   {"properties", {{"a", {{"type", {"string", "integer"}}}},
                                   {"b", {{"type", "array"}, {"items", {{"type", "string"}, {"maxLength", 2}}}, {"maxItems", 2}}},
                                   {"c", {{"type", "integer"}, {"minimum", 1}, {"maximum", 3}}},
                                   {"d", {{"type", "string"}, {"pattern", "^[a-z]+$"}, {"minLength", 2}}},
                                   {"e", {{"type", "object"}, {"additionalProperties", {{"type", "boolean"}}}}},
                                   {"f", {{"enum", {"x", "y"}}}}}}};
    EXPECT_FALSE(validate(schema, {{"a", "s"}}));
    EXPECT_FALSE(validate(schema, {{"a", 1}, {"b", {"ab"}}, {"c", 3}, {"d", "ok"}, {"e", {{"k", true}}}, {"f", "y"}}));
    EXPECT_EQ(*validate(schema, json::object()), "/: missing required property \"a\"");
    EXPECT_TRUE(validate(schema, {{"a", 1.5}}));
    EXPECT_EQ(*validate(schema, {{"a", 1}, {"b", {"abc"}}}), "/b/0: must be at most 2 characters");
    EXPECT_TRUE(validate(schema, {{"a", 1}, {"b", {"a", "b", "c"}}}));
    EXPECT_TRUE(validate(schema, {{"a", 1}, {"c", 0}}));
    EXPECT_TRUE(validate(schema, {{"a", 1}, {"d", "AB"}}));
    EXPECT_TRUE(validate(schema, {{"a", 1}, {"d", "a"}}));
    EXPECT_FALSE(validate({{"type", "string"}, {"minLength", 2}, {"maxLength", 2}}, json("\xc3\xa9\xc3\xa9")));   // code points
    EXPECT_EQ(*validate(schema, {{"a", 1}, {"e", {{"k", 1}}}}), "/e/k: must be boolean");
    EXPECT_TRUE(validate(schema, {{"a", 1}, {"f", "z"}}));
    EXPECT_NE(validate(schema, {{"a", 1}, {"zz", 1}})->find("unknown property \"zz\""), std::string::npos);
    EXPECT_TRUE(validate(schema, json::array()));
}

// security-audit.md S-03: secrets are redacted in every forti_read result, and no argument reveals them.
TEST_F(Tools, ReadRedactsDeviceSecretsAndCannotReveal) {
    json row = {{"name", "hq"}, {"psksecret", "ENC c2VjcmV0"}, {"remote-gw", "198.51.100.1"}};
    transport->reply(200, forti::fake::envelope(json::array({row})));
    json result = call("forti_read", {{"resource", "vpn.ipsec/phase1-interface"}});
    EXPECT_EQ(result["content"][0]["text"].get<std::string>().find("c2VjcmV0"), std::string::npos);
    json env = envelope(result);
    EXPECT_EQ(env["data"][0]["psksecret"], "<redacted>");
    EXPECT_EQ(env["meta"]["redacted"], json({"/*/psksecret"}));

    json refused = envelope(call("forti_read", {{"resource", "vpn.ipsec/phase1-interface"}, {"reveal", true}}));
    EXPECT_EQ(refused["error"]["code"], "usage");
}

