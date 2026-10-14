#include "options.hpp"

#include <forti/engine/error.hpp>
#include <forti/engine/profile.hpp>

#include <charconv>
#include <format>
#include <optional>

namespace forti::mcp {

    namespace {

        std::size_t parse_size(std::string_view text) {
            std::size_t value = 0;
            auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (text.empty() || error != std::errc() || end != text.data() + text.size())
                throw UsageError(std::format("--max-bytes expects a number of bytes, got \"{}\"", text));
            if (value < min_max_bytes || value > max_max_bytes)
                throw UsageError(std::format("--max-bytes must be between {} and {}", min_max_bytes, max_max_bytes));
            return value;
        }

    }  // namespace

    Command parse_args(std::span<const std::string_view> args) {
        Command command;
        bool seen_profile = false, seen_bytes = false, seen_read_only = false;
        for (std::size_t i = 0; i < args.size(); ++i) {
            std::string_view arg = args[i];
            std::string_view name = arg;
            std::optional<std::string_view> inline_value;
            if (auto eq = arg.find('='); arg.starts_with("--") && eq != std::string_view::npos) {
                name = arg.substr(0, eq);
                inline_value = arg.substr(eq + 1);
            }
            auto value = [&]() -> std::string_view {
                if (inline_value) return *inline_value;
                if (i + 1 >= args.size()) throw UsageError(std::format("{} needs a value", name));
                return args[++i];
            };
            auto once = [&](bool& seen) {
                if (seen) throw UsageError(std::format("{} given more than once", name));
                seen = true;
            };

            if (name == "--help" || name == "-h") {
                if (inline_value) throw UsageError("--help takes no value");
                command.action = Command::Action::help;
                return command;
            }
            if (name == "--version") {
                if (inline_value) throw UsageError("--version takes no value");
                command.action = Command::Action::version;
                return command;
            }
            if (name == "--read-only") {
                if (inline_value) throw UsageError("--read-only takes no value");
                once(seen_read_only);
                command.options.read_only = true;
            } else if (name == "--profile") {
                once(seen_profile);
                std::string profile(value());
                try {
                    engine::validate_profile_name(profile);
                } catch (const engine::Failure& failure) {
                    throw UsageError(failure.what());
                }
                command.options.default_profile = std::move(profile);
            } else if (name == "--max-bytes") {
                once(seen_bytes);
                command.options.max_bytes = parse_size(value());
            } else if (arg.starts_with("-")) {
                throw UsageError(std::format("unknown option: {}", name));
            } else {
                throw UsageError(std::format("unexpected argument: {}", arg));
            }
        }
        return command;
    }

    std::string usage() {
        return std::format(
                "usage: forti-mcp [--profile NAME] [--read-only] [--max-bytes N]\n"
                "\n"
                "FortiOS MCP server over stdio (JSON-RPC 2.0, one message per line). An MCP client\n"
                "starts it on demand; it serves until stdin closes. Tools: forti_discover, forti_read,\n"
                "forti_plan, forti_apply. FortiOS REST API only; never SSH or CLI commands.\n"
                "\n"
                "  --profile NAME   default device profile when a request names none\n"
                "                   (else the config default; see `forti devices`)\n"
                "  --read-only      offer only forti_discover and forti_read\n"
                "  --max-bytes N    JSON budget per tool result (default {}, {}..{})\n"
                "  --version        print the version\n"
                "  --help           print this help\n"
                "\n"
                "Profiles, credentials and each profile's mode (read-only, guarded, autonomous) are\n"
                "managed with `forti configure`. Register with AI clients: `forti install mcp`.\n",
                default_max_bytes, min_max_bytes, max_max_bytes);
    }

}  // namespace forti::mcp
