// The JSON-RPC / MCP layer: handshakes in both protocol eras, tools/list, and every error path.

#include "fake_transport.hpp"

#include <protocol.hpp>

#include <gtest/gtest.h>

#include <regex>

using json = nlohmann::json;
using forti::mcp::Server;
using forti::mcp::ServerOptions;

namespace {

    json modern_meta(std::string version = "2026-07-28") {
        return {{"io.modelcontextprotocol/protocolVersion", version},
                {"io.modelcontextprotocol/clientCapabilities", json::object()},
                {"io.modelcontextprotocol/clientInfo", {{"name", "test"}, {"version", "1"}}}};
    }

    json request(json id, std::string method, json params = nullptr) {
        json out = {{"jsonrpc", "2.0"}, {"id", std::move(id)}, {"method", std::move(method)}};
        if (!params.is_null()) out["params"] = std::move(params);
        return out;
    }

    json initialize_params(std::string version = "2025-11-25") {
        return {{"protocolVersion", version}, {"capabilities", json::object()}, {"clientInfo", {{"name", "test"}, {"version", "1"}}}};
    }

    struct Fixture : ::testing::Test {
        std::shared_ptr<forti::fake::FakeTransport> transport = std::make_shared<forti::fake::FakeTransport>();
        forti::engine::Engine engine{forti::fake::options(transport)};

        std::unique_ptr<Server> make(ServerOptions options = {}) { return std::make_unique<Server>(engine, options); }

        static void expect_error(const std::optional<json>& response, int code, const json* id = nullptr) {
            ASSERT_TRUE(response.has_value());
            ASSERT_TRUE(response->contains("error")) << response->dump();
            EXPECT_EQ((*response)["jsonrpc"], "2.0");
            EXPECT_EQ((*response)["error"]["code"], code) << response->dump();
            EXPECT_TRUE((*response)["error"]["message"].is_string());
            if (id) { EXPECT_EQ((*response)["id"], *id); }
            else EXPECT_FALSE(response->contains("id")) << response->dump();
        }

        void initialize(Server& server) {
            auto response = server.handle(request(0, "initialize", initialize_params()));
            ASSERT_TRUE(response && response->contains("result"));
            EXPECT_FALSE(server.handle({{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}));
        }
    };

}  // namespace

// ---- legacy handshake ----

TEST_F(Fixture, InitializeEchoesSupportedLegacyVersions) {
    for (std::string version: {"2025-11-25", "2025-06-18"}) {
        auto server = make();
        auto response = server->handle(request(1, "initialize", initialize_params(version)));
        ASSERT_TRUE(response);
        const json& result = (*response)["result"];
        EXPECT_EQ((*response)["id"], 1);
        EXPECT_EQ(result["protocolVersion"], version);
        EXPECT_EQ(result["capabilities"], json({{"tools", json::object()}}));
        EXPECT_EQ(result["serverInfo"]["name"], "forti-mcp");
        EXPECT_TRUE(result["serverInfo"]["version"].is_string());
        EXPECT_FALSE(result.contains("resultType"));
        std::string instructions = result["instructions"];
        EXPECT_NE(instructions.find("forti plan approve"), std::string::npos);
        EXPECT_NE(instructions.find("untrusted"), std::string::npos);
        EXPECT_NE(instructions.find("REST API only"), std::string::npos);
        EXPECT_EQ(server->legacy_version(), version);
    }
}

TEST_F(Fixture, InitializeCounterOffersLatestLegacyVersion) {
    for (std::string version: {"2024-11-05", "2026-07-28", "1.0"}) {
        auto server = make();
        auto response = server->handle(request(1, "initialize", initialize_params(version)));
        EXPECT_EQ((*response)["result"]["protocolVersion"], "2025-11-25") << version;
    }
}

TEST_F(Fixture, InitializeIsStrict) {
    auto server = make();
    json id = 7;
    expect_error(server->handle(request(7, "initialize")), -32602, &id);
    expect_error(server->handle(request(7, "initialize", {{"protocolVersion", 20251125}})), -32602, &id);
    json no_client = initialize_params();
    no_client.erase("clientInfo");
    expect_error(server->handle(request(7, "initialize", no_client)), -32602, &id);
    EXPECT_FALSE(server->legacy_version());

    initialize(*server);
    expect_error(server->handle(request(7, "initialize", initialize_params())), -32600, &id);
}

TEST_F(Fixture, PingWorksBeforeAndAfterInitialize) {
    auto server = make();
    auto before = server->handle(request("p", "ping"));
    EXPECT_EQ((*before)["result"], json::object());
    EXPECT_EQ((*before)["id"], "p");
    initialize(*server);
    EXPECT_EQ((*server->handle(request(2, "ping")))["result"], json::object());
}

TEST_F(Fixture, RequestsWithoutSessionOrMetaAreRejected) {
    auto server = make();
    json id = 3;
    for (std::string method: {"tools/list", "tools/call", "server/discover"}) {
        auto response = server->handle(request(3, method));
        expect_error(response, -32602, &id);
        EXPECT_TRUE((*response)["error"]["data"]["supported"].is_array());
    }
    expect_error(server->handle(request(3, "resources/list")), -32601, &id);
}

TEST_F(Fixture, LegacyToolsListHasNoModernFields) {
    auto server = make();
    initialize(*server);
    auto response = server->handle(request(2, "tools/list"));
    const json& result = (*response)["result"];
    EXPECT_EQ(result["tools"].size(), 4u);
    EXPECT_FALSE(result.contains("resultType"));
    EXPECT_FALSE(result.contains("nextCursor"));
    EXPECT_FALSE(result.contains("ttlMs"));
}

TEST_F(Fixture, LegacySessionRejectsModernOnlyAndUnknownMethods) {
    auto server = make();
    initialize(*server);
    json id = 4;
    expect_error(server->handle(request(4, "prompts/list")), -32601, &id);
    expect_error(server->handle(request(4, "resources/read")), -32601, &id);
}

// ---- modern (stateless) era ----

TEST_F(Fixture, ServerDiscover) {
    auto server = make();
    auto response = server->handle(request("d", "server/discover", {{"_meta", modern_meta()}}));
    const json& result = (*response)["result"];
    EXPECT_EQ(result["resultType"], "complete");
    EXPECT_EQ(result["supportedVersions"], json({"2026-07-28", "2025-11-25", "2025-06-18"}));
    EXPECT_EQ(result["capabilities"], json({{"tools", json::object()}}));
    EXPECT_EQ(result["_meta"]["io.modelcontextprotocol/serverInfo"]["name"], "forti-mcp");
    EXPECT_TRUE(result["instructions"].is_string());
    EXPECT_GT(result["ttlMs"].get<long long>(), 0);
    EXPECT_EQ(result["cacheScope"], "private");
    EXPECT_FALSE(server->legacy_version());   // stateless: no session was created
}

TEST_F(Fixture, ModernToolsListAndCallNeedNoHandshake) {
    auto server = make();
    auto list = server->handle(request(1, "tools/list", {{"_meta", modern_meta()}}));
    EXPECT_EQ((*list)["result"]["resultType"], "complete");
    EXPECT_EQ((*list)["result"]["tools"].size(), 4u);
    EXPECT_TRUE((*list)["result"].contains("ttlMs"));
    EXPECT_TRUE((*list)["result"].contains("cacheScope"));

    auto call = server->handle(request(2, "tools/call", {{"_meta", modern_meta()}, {"name", "forti_discover"},
                                                         {"arguments", {{"what", "resources"}, {"query", "firewall/address"}}}}));
    const json& result = (*call)["result"];
    EXPECT_EQ(result["resultType"], "complete");
    EXPECT_EQ(result["isError"], false);
    EXPECT_TRUE(result["_meta"].contains("io.modelcontextprotocol/serverInfo"));
    EXPECT_TRUE(transport->requests.empty());
}

TEST_F(Fixture, ModernVersionNegotiation) {
    auto server = make();
    json id = 9;
    auto response = server->handle(request(9, "tools/list", {{"_meta", modern_meta("1900-01-01")}}));
    expect_error(response, -32022, &id);
    EXPECT_EQ((*response)["error"]["data"]["requested"], "1900-01-01");
    EXPECT_EQ((*response)["error"]["data"]["supported"][0], "2026-07-28");
    // Legacy versions are negotiated by initialize, not per request.
    expect_error(server->handle(request(9, "tools/list", {{"_meta", modern_meta("2025-11-25")}})), -32022, &id);
}

TEST_F(Fixture, ModernMetaIsValidated) {
    auto server = make();
    json id = 5;
    json meta = modern_meta();
    meta.erase("io.modelcontextprotocol/clientCapabilities");
    expect_error(server->handle(request(5, "tools/list", {{"_meta", meta}})), -32602, &id);
    meta = modern_meta();
    meta["io.modelcontextprotocol/protocolVersion"] = 2026;
    expect_error(server->handle(request(5, "tools/list", {{"_meta", meta}})), -32602, &id);
    meta = modern_meta();
    meta["io.modelcontextprotocol/clientInfo"] = "x";
    expect_error(server->handle(request(5, "tools/list", {{"_meta", meta}})), -32602, &id);
    expect_error(server->handle(request(5, "tools/list", {{"_meta", "x"}})), -32602, &id);
}

TEST_F(Fixture, ModernUnknownMethodsAndPing) {
    auto server = make();
    json id = 6;
    expect_error(server->handle(request(6, "ping", {{"_meta", modern_meta()}})), -32601, &id);
    expect_error(server->handle(request(6, "prompts/list", {{"_meta", modern_meta()}})), -32601, &id);
}

// ---- tools/list contents ----

TEST_F(Fixture, ToolDefinitions) {
    auto server = make();
    const json& tools = server->tools().definitions();
    ASSERT_EQ(tools.size(), 4u);
    std::vector<std::string> names;
    for (const auto& tool: tools) {
        names.push_back(tool["name"]);
        EXPECT_TRUE(std::regex_match(tool["name"].get<std::string>(), std::regex("^[A-Za-z0-9_.-]{1,128}$")));
        EXPECT_TRUE(tool["title"].is_string());
        EXPECT_GT(tool["description"].get<std::string>().size(), 80u);
        EXPECT_EQ(tool["inputSchema"]["type"], "object");
        EXPECT_EQ(tool["inputSchema"]["additionalProperties"], false) << tool["name"];
        EXPECT_EQ(tool["outputSchema"]["type"], "object");
        EXPECT_EQ(tool["outputSchema"]["required"], json({"ok"}));
        EXPECT_TRUE(tool["annotations"].is_object());
    }
    EXPECT_EQ(names, (std::vector<std::string>{"forti_discover", "forti_read", "forti_plan", "forti_apply"}));

    for (int i: {0, 1, 2}) { EXPECT_EQ(tools[i]["annotations"]["readOnlyHint"], true) << names[i]; }
    const json& apply = tools[3];
    EXPECT_EQ(apply["annotations"]["readOnlyHint"], false);
    EXPECT_EQ(apply["annotations"]["destructiveHint"], true);
    EXPECT_EQ(apply["annotations"]["idempotentHint"], false);
    EXPECT_EQ(apply["inputSchema"]["properties"].size(), 1u);
    EXPECT_TRUE(apply["inputSchema"]["properties"].contains("plan_id"));
    EXPECT_EQ(apply["inputSchema"]["required"], json({"plan_id"}));
    EXPECT_EQ(tools[1]["inputSchema"]["required"], json({"resource"}));
    EXPECT_EQ(tools[2]["inputSchema"]["required"], json({"operations"}));
    EXPECT_EQ(tools[2]["inputSchema"]["properties"]["operations"]["items"]["additionalProperties"], false);

    // Workflow and safety are taught where the model reads them.
    std::string apply_text = apply["description"];
    EXPECT_NE(apply_text.find("forti plan approve"), std::string::npos);
    EXPECT_NE(apply_text.find("autonomous"), std::string::npos);
    EXPECT_NE(apply_text.find("cannot approve"), std::string::npos);
    EXPECT_NE(tools[1]["description"].get<std::string>().find("untrusted"), std::string::npos);
    EXPECT_NE(tools[0]["description"].get<std::string>().find("mode"), std::string::npos);
}

TEST_F(Fixture, ToolsListCursorIsInvalid) {
    auto server = make();
    json id = 8;
    expect_error(server->handle(request(8, "tools/list", {{"_meta", modern_meta()}, {"cursor", "abc"}})), -32602, &id);
    expect_error(server->handle(request(8, "tools/list", {{"_meta", modern_meta()}, {"cursor", 1}})), -32602, &id);
}

// ---- read-only mode ----

TEST_F(Fixture, ReadOnlyModeHidesAndRefusesPlanAndApply) {
    auto server = make({.default_profile = {}, .read_only = true});
    const json& tools = server->tools().definitions();
    ASSERT_EQ(tools.size(), 2u);
    EXPECT_EQ(tools[0]["name"], "forti_discover");
    EXPECT_EQ(tools[1]["name"], "forti_read");
    EXPECT_NE(server->tools().instructions().find("--read-only"), std::string::npos);

    json id = 1;
    for (std::string name: {"forti_plan", "forti_apply"}) {
        auto response = server->handle(request(1, "tools/call", {{"_meta", modern_meta()}, {"name", name},
                                                                 {"arguments", {{"plan_id", "x"}}}}));
        expect_error(response, -32602, &id);
        EXPECT_NE((*response)["error"]["message"].get<std::string>().find("read-only"), std::string::npos);
    }
}

// ---- tools/call protocol errors ----

TEST_F(Fixture, ToolsCallProtocolErrors) {
    auto server = make();
    json id = 2;
    auto call = [&](json params) {
        params["_meta"] = modern_meta();
        return server->handle(request(2, "tools/call", params));
    };
    expect_error(call(json::object()), -32602, &id);
    expect_error(call({{"name", 5}}), -32602, &id);
    expect_error(call({{"name", "forti_read"}, {"arguments", json::array()}}), -32602, &id);
    expect_error(call({{"name", "forti_read"}, {"arguments", "x"}}), -32602, &id);
    auto unknown = call({{"name", "forti_ssh"}});
    expect_error(unknown, -32602, &id);
    EXPECT_NE((*unknown)["error"]["message"].get<std::string>().find("Unknown tool"), std::string::npos);
}

// ---- JSON-RPC message validation ----

TEST_F(Fixture, InvalidMessages) {
    auto server = make();
    expect_error(server->handle(json::array({request(1, "ping")})), -32600);   // batch
    expect_error(server->handle(json::array()), -32600);
    expect_error(server->handle(json("ping")), -32600);
    expect_error(server->handle(json(42)), -32600);

    json id = 1;
    expect_error(server->handle({{"id", 1}, {"method", "ping"}}), -32600, &id);   // no jsonrpc
    expect_error(server->handle({{"jsonrpc", "1.0"}, {"id", 1}, {"method", "ping"}}), -32600, &id);
    expect_error(server->handle({{"jsonrpc", "2.0"}, {"id", 1}, {"method", 7}}), -32600, &id);
    expect_error(server->handle({{"jsonrpc", "2.0"}, {"id", 1}}), -32600, &id);   // neither request nor response
    for (json bad_id: {json(nullptr), json(1.5), json(true), json::object(), json::array()})
        expect_error(server->handle({{"jsonrpc", "2.0"}, {"id", bad_id}, {"method", "ping"}}), -32600);
    expect_error(server->handle(request(1, "ping", json::array({1, 2}))), -32602, &id);
    expect_error(server->handle(request(1, "ping", "x")), -32602, &id);
}

TEST_F(Fixture, NotificationsAndClientResponsesGetNoReply) {
    auto server = make();
    EXPECT_FALSE(server->handle({{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}));
    EXPECT_FALSE(server->handle({{"jsonrpc", "2.0"}, {"method", "notifications/cancelled"},
                                 {"params", {{"requestId", 3}, {"reason", "user"}}}}));
    EXPECT_FALSE(server->handle({{"jsonrpc", "2.0"}, {"method", "notifications/cancelled"}, {"params", "garbage"}}));
    EXPECT_FALSE(server->handle({{"jsonrpc", "2.0"}, {"method", "notifications/whatever"}}));
    EXPECT_FALSE(server->handle({{"jsonrpc", "2.0"}, {"id", 4}, {"result", json::object()}}));
    EXPECT_FALSE(server->handle({{"jsonrpc", "2.0"}, {"id", 4}, {"error", {{"code", 1}, {"message", "x"}}}}));
}

TEST_F(Fixture, StringAndIntegerIdsAreEchoed) {
    auto server = make();
    for (json id: {json("abc"), json(0), json(-5), json(9007199254740993ULL)}) {
        auto response = server->handle(request(id, "ping"));
        EXPECT_EQ((*response)["id"], id);
    }
}

// ---- raw lines ----

TEST_F(Fixture, HandleLine) {
    auto server = make();
    EXPECT_FALSE(server->handle_line(""));
    EXPECT_FALSE(server->handle_line("  \r"));

    auto parse = json::parse(*server->handle_line("{not json"));
    EXPECT_EQ(parse["error"]["code"], -32700);
    EXPECT_FALSE(parse.contains("id"));
    EXPECT_EQ(json::parse(*server->handle_line("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}\r"))["result"], json::object());
    EXPECT_EQ(json::parse(*server->handle_line("{\"a\":\"\xff\"}"))["error"]["code"], -32700);   // invalid UTF-8
    EXPECT_EQ(json::parse(*server->handle_line("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"} x"))["error"]["code"], -32700);

    std::string deep = std::string(100000, '[') + std::string(100000, ']');
    auto nested = json::parse(*server->handle_line(deep));
    EXPECT_EQ(nested["error"]["code"], -32600);
    // Brackets inside strings do not count.
    std::string brackets = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\",\"params\":{\"x\":\"" + std::string(1000, '[') + "\"}}";
    EXPECT_EQ(json::parse(*server->handle_line(brackets))["result"], json::object());

    std::string huge(forti::mcp::max_message_bytes + 1, ' ');
    huge[0] = '{';
    EXPECT_EQ(json::parse(*server->handle_line(huge))["error"]["code"], -32600);
    EXPECT_EQ(json::parse(Server::oversized_response())["error"]["code"], -32600);
}

TEST_F(Fixture, ResponsesAreSingleLines) {
    auto server = make();
    initialize(*server);
    auto line = server->handle_line(request(1, "tools/list").dump());
    ASSERT_TRUE(line);
    EXPECT_EQ(line->find('\n'), std::string::npos);
    auto call = server->handle_line(request(2, "tools/call", {{"name", "forti_discover"},
                                                               {"arguments", {{"what", "resource"}, {"resource", "firewall/address"}}}}).dump());
    EXPECT_EQ(call->find('\n'), std::string::npos);
    EXPECT_EQ(json::parse(*call)["result"]["isError"], false);
}
