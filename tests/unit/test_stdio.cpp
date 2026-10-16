// The stdio transport over pipes, with serve() on its own thread: the legacy-era wait for an
// elicitation/create answer (pings answered, other requests queued, cancellation, end of input,
// timeout). The engine runs in guarded mode on a scripted transport.

#include "change_fixture.hpp"

#include <protocol.hpp>
#include <stdio.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include <poll.h>
#include <unistd.h>

using namespace forti::test;
using forti::engine::Mode;
using forti::mcp::Server;

namespace {

    struct Stdio : ChangeFixture {
        std::unique_ptr<Server> server;
        int to_server[2] = {-1, -1}, from_server[2] = {-1, -1};
        std::thread thread;
        int status = -1;
        std::string buffer;

        void start(std::chrono::milliseconds answer_timeout = std::chrono::minutes(1)) {
            make(Mode::guarded);
            server = std::make_unique<Server>(*engine, forti::mcp::ServerOptions{});
            ASSERT_EQ(::pipe(to_server), 0);
            ASSERT_EQ(::pipe(from_server), 0);
            thread = std::thread([this, answer_timeout] {
                status = forti::mcp::serve(*server, to_server[0], from_server[1], answer_timeout);
                ::close(from_server[1]);
            });
        }

        ~Stdio() override {
            close_input();
            if (thread.joinable()) thread.join();
            for (int fd: {to_server[0], from_server[0]})
                if (fd >= 0) ::close(fd);
        }

        void send(const json& message) { send_raw(message.dump() + "\n"); }
        void send_raw(const std::string& text) { ASSERT_EQ(::write(to_server[1], text.data(), text.size()), static_cast<ssize_t>(text.size())); }

        void close_input() {
            if (to_server[1] >= 0) ::close(to_server[1]);
            to_server[1] = -1;
        }

        // The next line from the server, or null after `timeout`.
        json receive(std::chrono::milliseconds timeout = std::chrono::seconds(20)) {
            auto deadline = std::chrono::steady_clock::now() + timeout;
            for (;;) {
                if (auto newline = buffer.find('\n'); newline != std::string::npos) {
                    json message = json::parse(buffer.substr(0, newline));
                    buffer.erase(0, newline + 1);
                    return message;
                }
                auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
                if (left.count() <= 0) return nullptr;
                pollfd fd{from_server[0], POLLIN, 0};
                if (::poll(&fd, 1, static_cast<int>(left.count())) <= 0) continue;
                char chunk[4096];
                ssize_t n = ::read(from_server[0], chunk, sizeof chunk);
                if (n <= 0) return nullptr;
                buffer.append(chunk, static_cast<std::size_t>(n));
            }
        }

        int finish() {
            close_input();
            thread.join();
            EXPECT_TRUE(receive(std::chrono::milliseconds(200)).is_null());   // nothing after the last answer
            return status;
        }

        // initialize with form elicitation, then a forti_apply {operations} call with id 1. Returns the
        // elicitation/create request the server sent.
        json begin_apply() {
            send(request(0, "initialize", initialize_params(form_elicitation)));
            EXPECT_EQ(receive()["id"], 0);
            send({{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}});
            send(request(1, "tools/call", {{"name", "forti_apply"}, {"arguments", {{"operations", create_address()}}}}));
            json ask = receive();
            EXPECT_EQ(ask["method"], "elicitation/create") << ask.dump();
            EXPECT_FALSE(ask.contains("result"));
            return ask;
        }
    };

}  // namespace

TEST_F(Stdio, AnswerWhileServingPingsAndQueueingOtherRequests) {
    script_plan();
    script_apply();
    start();
    json ask = begin_apply();

    send(request("p", "ping"));
    json pong = receive();
    EXPECT_EQ(pong["id"], "p");
    EXPECT_EQ(pong["result"], json::object());

    send(request(5, "tools/list"));                                                   // queued
    send({{"jsonrpc", "2.0"}, {"id", "forti-mcp-elicit-99"}, {"result", {{"action", "accept"}}}});   // unknown id: ignored
    send_raw("{not json\n");                                                          // queued, answered later
    EXPECT_TRUE(receive(std::chrono::milliseconds(200)).is_null());

    send({{"jsonrpc", "2.0"}, {"id", ask["id"]}, {"result", {{"action", "accept"}, {"content", {{"approve", true}}}}}});
    json applied = receive();
    EXPECT_EQ(applied["id"], 1);
    EXPECT_EQ(applied["result"]["isError"], false) << applied.dump();
    EXPECT_EQ(applied["result"]["structuredContent"]["data"]["approval"]["approved_by"], "mcp:test:elicitation");
    EXPECT_EQ(writes(), 1u);

    // Then the queued messages, in order.
    json listed = receive();
    EXPECT_EQ(listed["id"], 5);
    EXPECT_EQ(listed["result"]["tools"].size(), 4u);
    EXPECT_EQ(receive()["error"]["code"], -32700);
    EXPECT_EQ(finish(), 0);
}

TEST_F(Stdio, CancellingTheCallAbandonsItWithoutAResponse) {
    script_plan();
    start();
    json ask = begin_apply();
    send({{"jsonrpc", "2.0"}, {"method", "notifications/cancelled"}, {"params", {{"requestId", 99}}}});   // another request
    send({{"jsonrpc", "2.0"}, {"method", "notifications/cancelled"}, {"params", {{"requestId", 1}, {"reason", "user"}}}});
    send(request(2, "ping"));
    json next = receive();
    EXPECT_EQ(next["id"], 2) << next.dump();   // no response for id 1
    // A late answer to the abandoned prompt changes nothing.
    send({{"jsonrpc", "2.0"}, {"id", ask["id"]}, {"result", {{"action", "accept"}, {"content", {{"approve", true}}}}}});
    EXPECT_EQ(finish(), 0);
    EXPECT_EQ(writes(), 0u);
    EXPECT_TRUE(last_event("approve").is_null());
}

TEST_F(Stdio, EndOfInputWhileWaitingAppliesNothing) {
    script_plan();
    start();
    begin_apply();
    close_input();
    json result = receive();
    EXPECT_EQ(result["id"], 1);
    EXPECT_EQ(result["result"]["structuredContent"]["error"]["details"]["reason"], "cancelled");
    EXPECT_EQ(finish(), 0);
    EXPECT_EQ(writes(), 0u);
}

TEST_F(Stdio, UnansweredPromptTimesOut) {
    script_plan();
    start(std::chrono::milliseconds(300));
    json ask = begin_apply();
    json result = receive();
    EXPECT_EQ(result["id"], 1);
    EXPECT_EQ(result["result"]["structuredContent"]["error"]["details"]["reason"], "timeout");
    // The late answer is ignored; the server keeps serving.
    send({{"jsonrpc", "2.0"}, {"id", ask["id"]}, {"result", {{"action", "accept"}, {"content", {{"approve", true}}}}}});
    send(request(3, "ping"));
    EXPECT_EQ(receive()["id"], 3);
    EXPECT_EQ(finish(), 0);
    EXPECT_EQ(writes(), 0u);
}

TEST_F(Stdio, OversizedLinesAndAFinalLineWithoutNewline) {
    start();
    std::string huge(forti::mcp::max_message_bytes + 10, 'x');
    send_raw(huge + "\n");
    EXPECT_EQ(receive()["error"]["code"], -32600);
    send_raw(request(1, "ping").dump());   // no trailing newline
    close_input();
    EXPECT_EQ(receive()["id"], 1);
    EXPECT_EQ(finish(), 0);
}
