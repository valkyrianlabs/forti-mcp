//
// Command-line options. An MCP client launches `forti-mcp` with no arguments and talks JSON-RPC over
// stdio; the flags only adjust policy for this process (default profile, read-only, response budget).
//

#ifndef FORTI_MCP_OPTIONS_HPP
#define FORTI_MCP_OPTIONS_HPP

#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace forti::mcp {

    inline constexpr std::size_t default_max_bytes = 48 * 1024;
    inline constexpr std::size_t min_max_bytes = 4 * 1024;
    inline constexpr std::size_t max_max_bytes = 16 * 1024 * 1024;

    struct ServerOptions {
        std::string default_profile;               // used when a request names no profile; empty: config default
        bool read_only = false;                    // plan/apply are not offered and are refused
        std::size_t max_bytes = default_max_bytes;  // budget for one tool result's JSON (see budget.hpp)
    };

    struct Command {
        enum class Action { serve, help, version };
        Action action = Action::serve;
        ServerOptions options;
    };

    // Bad arguments: the message is printed to stderr and the process exits 2.
    class UsageError : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };

    Command parse_args(std::span<const std::string_view> args);
    std::string usage();

}  // namespace forti::mcp

#endif  // FORTI_MCP_OPTIONS_HPP
