//
// forti-mcp: the FortiOS MCP server. An MCP client starts it on demand and speaks JSON-RPC over stdio;
// it exits when stdin closes. No daemon, no network listener, and no `forti` subprocess: the command
// engine is linked in.
//

#include "options.hpp"
#include "protocol.hpp"
#include "stdio.hpp"

#include <forti_mcp_version.hpp>

#include <forti/engine/engine.hpp>

#include <csignal>
#include <exception>
#include <iostream>
#include <string_view>
#include <vector>

#include <fcntl.h>
#include <sys/resource.h>
#include <unistd.h>

int main(int argc, char** argv) {
    using namespace forti;
    // No core dumps: the process holds API keys and device secrets in memory.
    struct rlimit no_core{0, 0};
    ::setrlimit(RLIMIT_CORE, &no_core);

    std::vector<std::string_view> args(argv + 1, argv + argc);
    mcp::Command command;
    try {
        command = mcp::parse_args(args);
    } catch (const mcp::UsageError& error) {
        std::cerr << "forti-mcp: " << error.what() << "\nTry 'forti-mcp --help'.\n";
        return 2;
    }
    if (command.action == mcp::Command::Action::help) {
        std::cout << mcp::usage();
        return 0;
    }
    if (command.action == mcp::Command::Action::version) {
        std::cout << "forti-mcp " << FORTI_MCP_VERSION << " (" << FORTI_MCP_BUILT_FROM << ")\n";
        return 0;
    }

    // stdout carries protocol messages only. Keep a private descriptor for them and point fd 1 at stderr,
    // so nothing else in the process (or a credential helper it runs) can corrupt the stream.
    int protocol_out = ::fcntl(STDOUT_FILENO, F_DUPFD_CLOEXEC, 3);
    if (protocol_out < 0 || ::dup2(STDERR_FILENO, STDOUT_FILENO) < 0) {
        std::cerr << "forti-mcp: cannot set up stdout\n";
        return 1;
    }
    std::signal(SIGPIPE, SIG_IGN);   // a vanished client surfaces as a write error, not a signal

    try {
        engine::Engine engine;
        mcp::Server server(engine, command.options);
        return mcp::serve(server, STDIN_FILENO, protocol_out);
    } catch (const std::exception& error) {
        std::cerr << "forti-mcp: " << error.what() << "\n";
        return 1;
    }
}
