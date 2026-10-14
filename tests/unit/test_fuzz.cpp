// Fuzz-style robustness tests for the protocol core (security-audit.md, S-16). Seeded and deterministic:
// valid messages are mutated (byte flips, truncation, splices, invalid UTF-8, duplicate keys, extreme
// numbers, deep nesting, huge strings) and fed to Server::handle_line; random JSON values go to
// Server::handle. Every input must produce either nothing (a notification) or one well-formed JSON-RPC
// response, and never crash. Meant to run under -Db_sanitize=address,undefined as well.

#include "fake_transport.hpp"

#include <budget.hpp>
#include <protocol.hpp>

#include <gtest/gtest.h>

#include <random>

using json = nlohmann::json;
using forti::mcp::Server;
using forti::mcp::ServerOptions;

namespace {

    const char* meta = R"("_meta":{"io.modelcontextprotocol/protocolVersion":"2026-07-28","io.modelcontextprotocol/clientCapabilities":{}})";

    std::vector<std::string> seeds() {
        std::string m = meta;
        return {
                R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"f","version":"1"}}})",
                R"({"jsonrpc":"2.0","id":2,"method":"ping"})",
                R"({"jsonrpc":"2.0","method":"notifications/initialized"})",
                R"({"jsonrpc":"2.0","id":"s","method":"tools/list","params":{)" + m + "}}",
                R"({"jsonrpc":"2.0","id":3,"method":"server/discover","params":{)" + m + "}}",
                R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{)" + m +
                        R"(,"name":"forti_discover","arguments":{"what":"resources","query":"addr","count":5}}})",
                R"({"jsonrpc":"2.0","id":5,"method":"tools/call","params":{)" + m +
                        R"(,"name":"forti_discover","arguments":{"resource":"monitor/system/os/reboot"}}})",
                R"({"jsonrpc":"2.0","id":6,"method":"tools/call","params":{)" + m +
                        R"(,"name":"forti_read","arguments":{"resource":"firewall/address","key":"a/../b","params":{"x":"1"}}}})",
                R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{)" + m +
                        R"(,"name":"forti_plan","arguments":{"operations":[{"op":"update","resource":"firewall/address","key":"x","set":{"comment":"c"}}]}}})",
                R"({"jsonrpc":"2.0","id":8,"method":"tools/call","params":{)" + m +
                        R"(,"name":"forti_apply","arguments":{"plan_id":"20261014-120000-abcdef"}}})",
                R"({"jsonrpc":"2.0","id":9,"method":"tools/call","params":{"name":"forti_read","arguments":{"resource":"monitor/../cmdb/system/admin"}}})",
                R"([{"jsonrpc":"2.0","id":10,"method":"ping"}])",
        };
    }

    // The response contract: an object with jsonrpc 2.0, exactly one of result/error, a valid or absent id.
    void check_response(const std::optional<std::string>& line, const std::string& input) {
        if (!line) return;
        ASSERT_EQ(line->find('\n'), std::string::npos) << input;   // one line per message on stdout
        json response = json::parse(*line, nullptr, false);
        ASSERT_FALSE(response.is_discarded()) << "invalid JSON out for: " << input.substr(0, 200);
        ASSERT_TRUE(response.is_object());
        EXPECT_EQ(response.value("jsonrpc", ""), "2.0");
        EXPECT_NE(response.contains("result"), response.contains("error")) << *line;
        if (response.contains("id")) EXPECT_TRUE(response["id"].is_string() || response["id"].is_number_integer()) << *line;
        if (response.contains("error")) {
            EXPECT_TRUE(response["error"]["code"].is_number_integer());
            EXPECT_TRUE(response["error"]["message"].is_string());
        }
    }

    std::string mutate(std::string text, std::mt19937& rng) {
        auto pick = [&](std::size_t n) { return n == 0 ? 0 : std::uniform_int_distribution<std::size_t>(0, n - 1)(rng); };
        static const std::vector<std::string> fragments = {
                "\xff", "\xc3\x28", "\xed\xa0\x80", "\x00", "\\u0000", "\\ud800", "1e400", "-1e400", "18446744073709551616",
                "-9223372036854775809", "0.0000001", "-0", "NaN", "Infinity", "null", "true", "[]", "{}", "\"\"",
                "\"id\":1,\"id\":\"dup\"", "\"method\":\"tools/call\"", "\"name\":\"forti_apply\"", "\x1b[2J", "%2e%2e/",
                "../", "?action=move", "#frag", "\\\"", "\xe2\x80\xae",
        };
        int edits = 1 + static_cast<int>(pick(4));
        for (int e = 0; e < edits; ++e) {
            switch (pick(7)) {
                case 0:
                    if (!text.empty()) text[pick(text.size())] = static_cast<char>(pick(256));
                    break;
                case 1: text.resize(pick(text.size() + 1)); break;
                case 2: text.insert(pick(text.size() + 1), fragments[pick(fragments.size())]); break;
                case 3:
                    if (text.size() > 2) text.erase(pick(text.size()), 1 + pick(8));
                    break;
                case 4: {   // duplicate a slice somewhere else
                    if (text.empty()) break;
                    std::size_t from = pick(text.size()), length = 1 + pick(std::min<std::size_t>(32, text.size() - from));
                    text.insert(pick(text.size() + 1), text.substr(from, length));
                    break;
                }
                case 5: text.insert(pick(text.size() + 1), std::string(pick(64), '[')); break;
                default: text.insert(pick(text.size() + 1), std::string(pick(4096), 'A')); break;
            }
        }
        return text;
    }

    json random_json(std::mt19937& rng, int depth) {
        auto pick = [&](int n) { return std::uniform_int_distribution<int>(0, n - 1)(rng); };
        switch (depth > 4 ? pick(6) : pick(8)) {
            case 0: return nullptr;
            case 1: return pick(2) == 1;
            case 2: return std::uniform_int_distribution<std::int64_t>()(rng);
            case 3: return std::uniform_real_distribution<double>(-1e300, 1e300)(rng);
            case 4: return std::string(static_cast<std::size_t>(pick(40)), static_cast<char>('a' + pick(26)));
            case 5: {
                static const std::vector<std::string> words = {"jsonrpc", "2.0", "id", "method", "params", "tools/call",
                                                               "name", "forti_read", "forti_apply", "arguments", "_meta",
                                                               "initialize", "ping", "result", "error"};
                return words[static_cast<std::size_t>(pick(static_cast<int>(words.size())))];
            }
            case 6: {
                json array = json::array();
                for (int i = pick(4); i > 0; --i) array.push_back(random_json(rng, depth + 1));
                return array;
            }
            default: {
                static const std::vector<std::string> keys = {"jsonrpc", "id", "method", "params", "name", "arguments",
                                                              "_meta", "resource", "plan_id", "profile", "x"};
                json object = json::object();
                for (int i = pick(5); i > 0; --i)
                    object[keys[static_cast<std::size_t>(pick(static_cast<int>(keys.size())))]] = random_json(rng, depth + 1);
                return object;
            }
        }
    }

    struct Fuzz : ::testing::Test {
        std::shared_ptr<forti::fake::FakeTransport> transport = std::make_shared<forti::fake::FakeTransport>();
        forti::engine::Engine engine{forti::fake::options(transport, forti::engine::Mode::guarded)};
    };

}  // namespace

TEST_F(Fuzz, MutatedMessagesAlwaysGetAWellFormedAnswer) {
    std::mt19937 rng(20261014);
    auto inputs = seeds();
    for (int round = 0; round < 4000; ++round) {
        Server server(engine, ServerOptions{});
        // Sometimes start a legacy session so tools/call is reachable without _meta.
        if (round % 3 == 0) (void) server.handle_line(inputs[0]);
        std::string line = mutate(inputs[static_cast<std::size_t>(round) % inputs.size()], rng);
        auto response = server.handle_line(line);
        check_response(response, line);
        if (HasFatalFailure()) return;
        transport->requests.clear();
        transport->script.clear();
    }
}

TEST_F(Fuzz, RandomJsonValuesAlwaysGetAWellFormedAnswer) {
    std::mt19937 rng(7);
    Server server(engine, ServerOptions{});
    (void) server.handle_line(seeds()[0]);
    for (int round = 0; round < 4000; ++round) {
        json message = random_json(rng, 0);
        if (round % 2 == 0 && message.is_object()) message["jsonrpc"] = "2.0";
        std::optional<json> response = server.handle(message);
        if (response) check_response(forti::mcp::dump(*response), message.dump());
        if (HasFatalFailure()) return;
        transport->requests.clear();
        transport->script.clear();
    }
}

TEST_F(Fuzz, HostileShapesAreRejectedCleanly) {
    Server server(engine, ServerOptions{});
    std::vector<std::string> lines = {
            std::string(100000, '['),                                         // nesting bomb
            "{\"a\":" + std::string(65, '[') + std::string(65, ']') + "}",    // just over the nesting limit
            "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"" + std::string(1 << 20, 'x') + "\"}",   // 1 MiB method name
            "{\"jsonrpc\":\"2.0\",\"id\":1e400,\"method\":\"ping\"}",
            "{\"jsonrpc\":\"2.0\",\"id\":18446744073709551616,\"method\":\"ping\"}",
            "{\"jsonrpc\":\"2.0\",\"id\":1,\"id\":2,\"method\":\"ping\"}",
            "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\",\"unknown\":{\"x\":1}}",
            std::string("{\"jsonrpc\":\"2.0\",\"id\":\"\xff\xfe\",\"method\":\"ping\"}"),
            std::string("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}\0garbage", 39),
            "\xef\xbb\xbf{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}",   // UTF-8 BOM
            "   ",
            "",
    };
    for (const auto& line: lines) {
        auto response = server.handle_line(line);
        check_response(response, line);
    }
}

TEST_F(Fuzz, HugeAndDeepDeviceRepliesStayWithinTheBudget) {
    ServerOptions options;
    Server server(engine, options);
    (void) server.handle_line(seeds()[0]);
    auto call = [&](const json& arguments) {
        json message = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/call"},
                        {"params", {{"name", "forti_read"}, {"arguments", arguments}}}};
        auto line = server.handle_line(message.dump());
        check_response(line, message.dump());
        return json::parse(*line);
    };

    // 20 000 objects with long comments: trimmed by whole items, still complete JSON.
    json rows = json::array();
    for (int i = 0; i < 20000; ++i) rows.push_back({{"name", std::format("obj{}", i)}, {"comment", std::string(200, 'c')}});
    transport->reply(200, forti::fake::envelope(rows));
    json response = call({{"resource", "monitor/firewall/session"}, {"count", 1000}});
    std::string text = response["result"]["content"][0]["text"];
    EXPECT_LE(text.size(), options.max_bytes);
    EXPECT_TRUE(response["result"]["structuredContent"]["meta"]["truncated"]);

    // One huge string value.
    transport->reply(200, forti::fake::envelope(json{{"blob", std::string(8 << 20, 'x')}}));
    response = call({{"resource", "monitor/system/status"}});
    EXPECT_LE(response["result"]["content"][0]["text"].get<std::string>().size(), options.max_bytes);

    // Deeply nested device JSON (the device side has no nesting limit).
    std::string deep = "{\"http_status\":200,\"status\":\"success\",\"serial\":\"FGT60FTK00000000\",\"results\":" +
                       std::string(5000, '[') + std::string(5000, ']') + "}";
    transport->script.emplace_back(fortiapi::HttpResponse{200, deep});
    response = call({{"resource", "monitor/system/status"}});
    EXPECT_LE(response["result"]["content"][0]["text"].get<std::string>().size(), options.max_bytes);
}
