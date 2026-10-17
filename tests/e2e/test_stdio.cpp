// End to end: spawn the built forti-mcp, pipe JSON-RPC lines in, read every line out. Config, state
// and cache (and FORTI_HOME) point at a fresh temporary directory, so no profile exists, the real forti
// home is never resolved and no device is contacted.

#include <nlohmann/json.hpp>

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

using json = nlohmann::json;

namespace {

    struct Run {
        int status = -1;
        std::string out, err;

        [[nodiscard]] std::vector<json> messages() const {
            std::vector<json> out_messages;
            std::size_t at = 0;
            while (at < out.size()) {
                auto newline = out.find('\n', at);
                EXPECT_NE(newline, std::string::npos) << "unterminated line on stdout";
                if (newline == std::string::npos) break;
                std::string line = out.substr(at, newline - at);
                json message = json::parse(line, nullptr, false);
                EXPECT_FALSE(message.is_discarded()) << "stdout carried a non-JSON line: " << line.substr(0, 200);
                EXPECT_EQ(message["jsonrpc"], "2.0");
                out_messages.push_back(std::move(message));
                at = newline + 1;
            }
            return out_messages;
        }
    };

    std::filesystem::path temp_home() {
        std::string pattern = (std::filesystem::temp_directory_path() / "forti-mcp-e2e.XXXXXX").string();
        if (!::mkdtemp(pattern.data())) throw std::runtime_error("mkdtemp failed");
        return pattern;
    }

    // Runs the binary with `args`, writes `input` to its stdin (concurrently with reading its output, so a
    // large input cannot deadlock), closes stdin, and collects stdout, stderr and the exit status.
    Run run(const std::vector<std::string>& args, const std::string& input) {
        const char* binary = std::getenv("FORTI_MCP_BINARY");
        if (!binary) throw std::runtime_error("FORTI_MCP_BINARY is not set");
        auto home = temp_home();

        std::vector<std::string> env_strings = {"FORTI_HOME=" + (home / "forti-home").string(),
                                                "FORTI_CONFIG_DIR=" + (home / "config").string(),
                                                "FORTI_STATE_DIR=" + (home / "state").string(),
                                                "FORTI_CACHE_DIR=" + (home / "cache").string(),
                                                "XDG_CONFIG_HOME=" + (home / "xdg-config").string(),
                                                "XDG_STATE_HOME=" + (home / "xdg-state").string(),
                                                "XDG_CACHE_HOME=" + (home / "xdg-cache").string(),
                                                "HOME=" + home.string(), "PATH=/usr/bin:/bin"};
        std::vector<char*> envp;
        for (auto& s: env_strings) envp.push_back(s.data());
        envp.push_back(nullptr);
        std::vector<std::string> argv_strings = {binary};
        argv_strings.insert(argv_strings.end(), args.begin(), args.end());
        std::vector<char*> argv;
        for (auto& s: argv_strings) argv.push_back(s.data());
        argv.push_back(nullptr);

        int in[2], out[2], err[2];
        if (::pipe(in) || ::pipe(out) || ::pipe(err)) throw std::runtime_error("pipe failed");
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, in[0], 0);
        posix_spawn_file_actions_adddup2(&actions, out[1], 1);
        posix_spawn_file_actions_adddup2(&actions, err[1], 2);
        for (int fd: {in[0], in[1], out[0], out[1], err[0], err[1]}) posix_spawn_file_actions_addclose(&actions, fd);
        pid_t pid = 0;
        if (posix_spawn(&pid, binary, &actions, nullptr, argv.data(), envp.data()) != 0) throw std::runtime_error("spawn failed");
        posix_spawn_file_actions_destroy(&actions);
        ::close(in[0]);
        ::close(out[1]);
        ::close(err[1]);
        ::fcntl(in[1], F_SETFL, O_NONBLOCK);

        Run result;
        std::size_t written = 0;
        bool in_open = true, out_open = true, err_open = true;
        if (input.empty()) ::close(in[1]), in_open = false;
        char buffer[65536];
        while (out_open || err_open) {
            std::vector<pollfd> fds;
            if (in_open) fds.push_back({in[1], POLLOUT, 0});
            if (out_open) fds.push_back({out[0], POLLIN, 0});
            if (err_open) fds.push_back({err[0], POLLIN, 0});
            if (::poll(fds.data(), fds.size(), 20000) <= 0) break;   // a hung server fails the test below
            for (auto& p: fds) {
                if (!p.revents) continue;
                if (p.fd == in[1]) {
                    ssize_t n = ::write(in[1], input.data() + written, std::min<std::size_t>(input.size() - written, 65536));
                    if (n > 0) written += static_cast<std::size_t>(n);
                    if (n < 0 || written == input.size()) ::close(in[1]), in_open = false;
                } else {
                    ssize_t n = ::read(p.fd, buffer, sizeof buffer);
                    std::string& sink = p.fd == out[0] ? result.out : result.err;
                    if (n > 0) sink.append(buffer, static_cast<std::size_t>(n));
                    else (p.fd == out[0] ? out_open : err_open) = false;
                }
            }
        }
        if (in_open) ::close(in[1]);
        ::close(out[0]);
        ::close(err[0]);
        int status = 0;
        ::waitpid(pid, &status, 0);
        result.status = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
        std::filesystem::remove_all(home);
        return result;
    }

    std::string lines(const std::vector<json>& messages) {
        std::string out;
        for (const auto& m: messages) out += m.dump() + "\n";
        return out;
    }

    json initialize() {
        return {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"},
                {"params", {{"protocolVersion", "2025-11-25"}, {"capabilities", json::object()},
                            {"clientInfo", {{"name", "e2e"}, {"version", "1"}}}}}};
    }

}  // namespace

TEST(Stdio, LegacyHandshakeListAndDiscover) {
    auto result = run({}, lines({initialize(),
                                 {{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}},
                                 {{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/list"}},
                                 {{"jsonrpc", "2.0"}, {"id", 3}, {"method", "tools/call"},
                                  {"params", {{"name", "forti_discover"}, {"arguments", {{"what", "resources"}, {"query", "firewall/address"}}}}}},
                                 {{"jsonrpc", "2.0"}, {"id", 4}, {"method", "tools/call"},
                                  {"params", {{"name", "forti_discover"}, {"arguments", {{"what", "devices"}}}}}}}));
    EXPECT_EQ(result.status, 0) << result.err;
    auto messages = result.messages();
    ASSERT_EQ(messages.size(), 4u) << result.out;
    EXPECT_EQ(messages[0]["result"]["protocolVersion"], "2025-11-25");
    EXPECT_EQ(messages[1]["result"]["tools"].size(), 4u);
    EXPECT_EQ(messages[2]["id"], 3);
    EXPECT_EQ(messages[2]["result"]["isError"], false);
    EXPECT_EQ(messages[2]["result"]["structuredContent"]["data"][0]["resource"], "firewall/address");
    EXPECT_EQ(messages[3]["result"]["structuredContent"]["data"], json::array());   // fresh config: no profiles
    EXPECT_TRUE(result.err.empty()) << result.err;
}

TEST(Stdio, ModernStatelessRequests) {
    json meta = {{"io.modelcontextprotocol/protocolVersion", "2026-07-28"},
                 {"io.modelcontextprotocol/clientCapabilities", json::object()}};
    auto result = run({"--read-only"}, lines({{{"jsonrpc", "2.0"}, {"id", "a"}, {"method", "server/discover"}, {"params", {{"_meta", meta}}}},
                                              {{"jsonrpc", "2.0"}, {"id", "b"}, {"method", "tools/list"}, {"params", {{"_meta", meta}}}}}));
    EXPECT_EQ(result.status, 0);
    auto messages = result.messages();
    ASSERT_EQ(messages.size(), 2u);
    EXPECT_EQ(messages[0]["result"]["supportedVersions"][0], "2026-07-28");
    EXPECT_EQ(messages[1]["result"]["tools"].size(), 2u);
}

// The published schemas take child tables (strictly: still additionalProperties false), and a call
// using them gets past schema validation to the engine.
TEST(Stdio, ChildTableArgumentsAreInTheSchemas) {
    auto result = run({}, lines({initialize(),
                                 {{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/list"}},
                                 {{"jsonrpc", "2.0"}, {"id", 3}, {"method", "tools/call"},
                                  {"params", {{"name", "forti_read"},
                                              {"arguments", {{"resource", "system.dhcp/server"}, {"key", 1},
                                                             {"child", "reserved-address"}, {"child_key", 21}}}}}}}));
    EXPECT_EQ(result.status, 0) << result.err;
    auto messages = result.messages();
    ASSERT_EQ(messages.size(), 3u) << result.out;
    json read, plan;
    for (const auto& tool: messages[1]["result"]["tools"]) {
        if (tool["name"] == "forti_read") read = tool["inputSchema"];
        if (tool["name"] == "forti_plan") plan = tool["inputSchema"];
    }
    EXPECT_EQ(read["additionalProperties"], false);
    EXPECT_TRUE(read["properties"].contains("child"));
    EXPECT_TRUE(read["properties"].contains("child_key"));
    const json& operation = plan["properties"]["operations"]["items"];
    EXPECT_EQ(operation["additionalProperties"], false);
    EXPECT_TRUE(operation["properties"].contains("parent"));
    EXPECT_TRUE(operation["properties"].contains("child"));
    EXPECT_FALSE(operation["properties"].contains("child_key"));
    // No profile is configured: the engine answers (config), not the schema check (usage).
    const json& envelope = messages[2]["result"]["structuredContent"];
    EXPECT_EQ(envelope["error"]["code"], "config") << envelope.dump();
}

TEST(Stdio, MalformedAndHugeLinesGetErrorsAndServingContinues) {
    std::string input = "not json\n";
    input += "[]\n";
    input += "\n";
    input += "{" + std::string(5 * 1024 * 1024, ' ') + "}\n";   // over the 4 MiB message limit
    input += std::string(200, '[') + "\n";
    input += R"({"jsonrpc":"2.0","id":9,"method":"ping"})";       // last line without a newline
    auto result = run({}, input);
    EXPECT_EQ(result.status, 0) << result.err;
    auto messages = result.messages();
    ASSERT_EQ(messages.size(), 5u) << result.out.substr(0, 1000);
    EXPECT_EQ(messages[0]["error"]["code"], -32700);
    EXPECT_EQ(messages[1]["error"]["code"], -32600);
    EXPECT_EQ(messages[2]["error"]["code"], -32600);
    EXPECT_EQ(messages[3]["error"]["code"], -32600);
    EXPECT_EQ(messages[4]["id"], 9);
    EXPECT_EQ(messages[4]["result"], json::object());
}

TEST(Stdio, EofExitsCleanly) {
    auto result = run({}, "");
    EXPECT_EQ(result.status, 0);
    EXPECT_TRUE(result.out.empty());
}

TEST(Stdio, CommandLine) {
    auto version = run({"--version"}, "");
    EXPECT_EQ(version.status, 0);
    EXPECT_EQ(version.out.rfind("forti-mcp ", 0), 0u);
    EXPECT_NE(version.out.find(" (engine forti-cli "), std::string::npos) << version.out;   // what it was built from
    EXPECT_NE(version.out.find(", forti-api "), std::string::npos) << version.out;
    auto help = run({"--help"}, "");
    EXPECT_EQ(help.status, 0);
    EXPECT_NE(help.out.find("--read-only"), std::string::npos);
    auto bad = run({"--bogus"}, "");
    EXPECT_EQ(bad.status, 2);
    EXPECT_TRUE(bad.out.empty());
    EXPECT_NE(bad.err.find("unknown option"), std::string::npos);
    EXPECT_EQ(run({"--max-bytes", "10"}, "").status, 2);
}

// security-audit.md S-16: a client that goes away mid-session (its end of stdout closed) ends the
// server with a write error and status 1, not death by SIGPIPE.
TEST(Stdio, VanishedClientIsAWriteErrorNotSigpipe) {
    const char* binary = std::getenv("FORTI_MCP_BINARY");
    ASSERT_NE(binary, nullptr);
    auto home = temp_home();
    std::vector<std::string> env_strings = {"FORTI_HOME=" + (home / "forti-home").string(),
                                            "FORTI_CONFIG_DIR=" + (home / "config").string(),
                                            "FORTI_STATE_DIR=" + (home / "state").string(),
                                            "FORTI_CACHE_DIR=" + (home / "cache").string(), "HOME=" + home.string(),
                                            "PATH=/usr/bin:/bin"};
    std::vector<char*> envp;
    for (auto& s: env_strings) envp.push_back(s.data());
    envp.push_back(nullptr);
    std::string path = binary;
    std::vector<char*> argv = {path.data(), nullptr};
    int in[2], out[2];
    ASSERT_EQ(::pipe(in), 0);
    ASSERT_EQ(::pipe(out), 0);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, in[0], 0);
    posix_spawn_file_actions_adddup2(&actions, out[1], 1);
    posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
    for (int fd: {in[0], in[1], out[0], out[1]}) posix_spawn_file_actions_addclose(&actions, fd);
    pid_t pid = 0;
    ASSERT_EQ(posix_spawn(&pid, binary, &actions, nullptr, argv.data(), envp.data()), 0);
    posix_spawn_file_actions_destroy(&actions);
    ::close(in[0]);
    ::close(out[1]);
    ::close(out[0]);   // the client is gone
    std::string ping = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}\n";
    for (int i = 0; i < 4; ++i) (void) !::write(in[1], ping.data(), ping.size());
    ::close(in[1]);
    int status = 0;
    ::waitpid(pid, &status, 0);
    std::filesystem::remove_all(home);
    ASSERT_TRUE(WIFEXITED(status)) << "terminated by signal " << WTERMSIG(status);
    EXPECT_EQ(WEXITSTATUS(status), 1);
}
