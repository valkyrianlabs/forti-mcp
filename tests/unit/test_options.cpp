// Command-line parsing: an MCP client passes no arguments; anything unknown is an error.

#include <options.hpp>

#include <gtest/gtest.h>

#include <vector>

using forti::mcp::Command;
using forti::mcp::parse_args;
using forti::mcp::UsageError;

namespace {

    Command parse(std::vector<std::string_view> args) { return parse_args(args); }

}  // namespace

TEST(Options, NoArgumentsServes) {
    Command command = parse({});
    EXPECT_EQ(command.action, Command::Action::serve);
    EXPECT_EQ(command.options.max_bytes, 48u * 1024);
    EXPECT_FALSE(command.options.read_only);
    EXPECT_TRUE(command.options.default_profile.empty());
}

TEST(Options, Flags) {
    Command command = parse({"--profile", "home", "--read-only", "--max-bytes", "65536"});
    EXPECT_EQ(command.options.default_profile, "home");
    EXPECT_TRUE(command.options.read_only);
    EXPECT_EQ(command.options.max_bytes, 65536u);
    Command inline_values = parse({"--profile=lab-2", "--max-bytes=4096"});
    EXPECT_EQ(inline_values.options.default_profile, "lab-2");
    EXPECT_EQ(inline_values.options.max_bytes, 4096u);
    EXPECT_EQ(parse({"--help"}).action, Command::Action::help);
    EXPECT_EQ(parse({"-h"}).action, Command::Action::help);
    EXPECT_EQ(parse({"--version"}).action, Command::Action::version);
}

TEST(Options, Errors) {
    for (std::vector<std::string_view> args: {std::vector<std::string_view>{"--bogus"},
                                              {"-x"},
                                              {"serve"},
                                              {"--profile"},
                                              {"--profile", "bad name"},
                                              {"--profile", "a", "--profile", "b"},
                                              {"--max-bytes", "lots"},
                                              {"--max-bytes", "100"},
                                              {"--max-bytes", "99999999999"},
                                              {"--max-bytes", "4096x"},
                                              {"--read-only=yes"},
                                              {"--read-only", "--read-only"}})
        EXPECT_THROW(parse(args), UsageError) << args[0];
}
