//
// A real engine with a private temporary state directory and a scripted transport, for the tests that
// plan and apply (forti_apply, in-chat approval). The profile "lab" runs in the mode the test picks.
//

#ifndef FORTI_MCP_TESTS_CHANGE_FIXTURE_HPP
#define FORTI_MCP_TESTS_CHANGE_FIXTURE_HPP

#include "fake_transport.hpp"

#include <protocol.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <stdlib.h>

namespace forti::test {

    using json = nlohmann::json;
    namespace fs = std::filesystem;

    inline const std::string serial = "FGT60FTK00000000";

    struct TempDir {
        fs::path path;
        TempDir() {
            std::string pattern = (fs::temp_directory_path() / "forti-mcp-test-XXXXXX").string();
            if (!::mkdtemp(pattern.data())) throw std::runtime_error("mkdtemp failed");
            path = pattern;
        }
        ~TempDir() {
            std::error_code ignored;
            fs::remove_all(path, ignored);
        }
        TempDir(const TempDir&) = delete;
        TempDir& operator=(const TempDir&) = delete;
    };

    inline json address(const std::string& name) {
        return {{"name", name}, {"q_origin_key", name}, {"uuid", "8f0c-" + name}, {"subnet", "192.0.2.10 255.255.255.255"},
                {"type", "ipmask"}, {"comment", "from mcp"}, {"color", 0}};
    }

    inline json missing() { return {{"status", "error"}, {"http_status", 404}, {"serial", serial}, {"version", "v7.4.8"}}; }

    inline json written(const std::string& mkey) {
        return {{"status", "success"}, {"http_status", 200}, {"serial", serial}, {"version", "v7.4.8"}, {"vdom", "root"}, {"mkey", mkey}};
    }

    // forti_apply / forti_plan operations that create address `name`.
    inline json create_address(const std::string& name = "web") {
        return json::array({{{"op", "create"}, {"resource", "firewall/address"},
                             {"object", {{"name", name}, {"subnet", "192.0.2.10 255.255.255.255"}, {"comment", "from mcp"}}}}});
    }

    struct ChangeFixture : ::testing::Test {
        TempDir dir;
        std::shared_ptr<fake::FakeTransport> transport = std::make_shared<fake::FakeTransport>();
        engine::Paths paths{dir.path / "config", dir.path / "state", dir.path / "cache", {}};
        std::unique_ptr<engine::Engine> engine;

        void make(engine::Mode mode) {
            auto options = fake::options(transport, mode);
            options.paths = paths;
            engine = std::make_unique<engine::Engine>(options);
        }

        // Planning a create reads the (absent) object; applying it re-reads, POSTs and verifies.
        void script_plan() { transport->reply(404, missing()); }
        void script_apply(const std::string& name = "web") {
            transport->reply(404, missing());
            transport->reply(200, written(name));
            transport->reply(200, fake::envelope(json::array({address(name)})));
        }

        std::size_t writes() const {
            std::size_t n = 0;
            for (const auto& request: transport->requests) n += request.method != fortiapi::Method::Get;
            return n;
        }

        std::vector<json> audit() const {
            std::vector<json> out;
            std::ifstream in(paths.state / "audit.jsonl");
            for (std::string line; std::getline(in, line);)
                if (!line.empty()) out.push_back(json::parse(line));
            return out;
        }

        // The last audit event of a kind ("apply", "approve").
        json last_event(const std::string& kind) const {
            json found;
            for (auto& event: audit())
                if (event.value("event", "") == kind) found = event;
            return found;
        }
    };

    inline json request(json id, std::string method, json params = nullptr) {
        json out = {{"jsonrpc", "2.0"}, {"id", std::move(id)}, {"method", std::move(method)}};
        if (!params.is_null()) out["params"] = std::move(params);
        return out;
    }

    inline json modern_meta(json capabilities = json::object()) {
        return {{"io.modelcontextprotocol/protocolVersion", "2026-07-28"},
                {"io.modelcontextprotocol/clientCapabilities", std::move(capabilities)},
                {"io.modelcontextprotocol/clientInfo", {{"name", "test"}, {"version", "1"}}}};
    }

    inline json initialize_params(json capabilities, std::string version = "2025-11-25") {
        return {{"protocolVersion", std::move(version)}, {"capabilities", std::move(capabilities)},
                {"clientInfo", {{"name", "test"}, {"version", "1"}}}};
    }

    inline const json form_elicitation = {{"elicitation", json::object()}};

}  // namespace forti::test

#endif  // FORTI_MCP_TESTS_CHANGE_FIXTURE_HPP
