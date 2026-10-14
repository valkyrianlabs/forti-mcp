// The response budget: results shrink by whole items, never by bytes, and say what was cut.

#include <budget.hpp>

#include <gtest/gtest.h>

#include <format>

using json = nlohmann::json;
using forti::mcp::bound;
using forti::mcp::dump;

namespace {

    json items(std::size_t n, std::size_t bytes) {
        json out = json::array();
        for (std::size_t i = 0; i < n; ++i) out.push_back({{"id", i}, {"pad", std::string(bytes, 'x')}});
        return out;
    }

}  // namespace

TEST(Budget, SmallResultsAreUntouched) {
    json env = {{"ok", true}, {"data", items(3, 10)}, {"meta", {{"start", 0}}}};
    auto bounded = bound(env, 4096);
    EXPECT_EQ(bounded.envelope, env);
    EXPECT_EQ(bounded.text, dump(env));
}

TEST(Budget, ListingsKeepTheLongestPrefixThatFits) {
    for (std::size_t budget: {4096u, 5000u, 8192u, 20000u}) {
        json env = {{"ok", true}, {"data", items(500, 50)}, {"meta", {{"start", 100}, {"next_start", 600}, {"total", 900}}}};
        auto bounded = bound(env, budget);
        EXPECT_LE(bounded.text.size(), budget);
        EXPECT_EQ(json::parse(bounded.text), bounded.envelope);
        const json& meta = bounded.envelope["meta"];
        std::size_t kept = bounded.envelope["data"].size();
        EXPECT_GT(kept, 0u);
        EXPECT_EQ(meta["returned"], kept);
        EXPECT_EQ(meta["next_start"], 100 + kept);
        EXPECT_EQ(meta["truncated"], true);
        EXPECT_EQ(meta["total"], 900);
        EXPECT_EQ(bounded.envelope["data"].back()["id"], kept - 1);
        // One more item would not have fit.
        json bigger = bounded.envelope;
        bigger["data"].push_back(env["data"][kept]);
        EXPECT_GT(dump(bigger).size(), budget - 8) << budget;
    }
}

TEST(Budget, AnOversizedSingleItemLeavesAHint) {
    json env = {{"ok", true}, {"data", items(2, 10000)}, {"meta", {{"start", 4}}}};
    auto bounded = bound(env, 4096);
    EXPECT_LE(bounded.text.size(), 4096u);
    EXPECT_EQ(bounded.envelope["data"], json::array());
    EXPECT_EQ(bounded.envelope["meta"]["next_start"], 4);
    EXPECT_NE(bounded.envelope["meta"]["hint"].get<std::string>().find("fields"), std::string::npos);
}

TEST(Budget, ObjectsShrinkTheirLargestArrays) {
    json env = {{"ok", true},
                {"data", {{"resource", "firewall/policy"}, {"fields", items(400, 40)}, {"small", items(3, 5)}}},
                {"meta", {{"live", false}}}};
    auto bounded = bound(env, 4096);
    EXPECT_LE(bounded.text.size(), 4096u);
    const json& meta = bounded.envelope["meta"];
    EXPECT_EQ(meta["truncated"], true);
    EXPECT_EQ(meta["trimmed"]["/fields"]["total"], 400);
    EXPECT_EQ(meta["trimmed"]["/fields"]["kept"], bounded.envelope["data"]["fields"].size());
    EXPECT_EQ(bounded.envelope["data"]["small"].size(), 3u);
    EXPECT_EQ(bounded.envelope["data"]["resource"], "firewall/policy");
}

TEST(Budget, UnshrinkableResultsBecomeNullWithAHint) {
    json env = {{"ok", true}, {"data", {{"blob", std::string(10000, 'b')}}}, {"meta", json::object()}};
    auto bounded = bound(env, 4096);
    EXPECT_LE(bounded.text.size(), 4096u);
    EXPECT_TRUE(bounded.envelope["data"].is_null());
    EXPECT_EQ(bounded.envelope["meta"]["truncated"], true);
    EXPECT_TRUE(bounded.envelope["meta"]["hint"].is_string());

    json scalar = {{"ok", true}, {"data", std::string(10000, 's')}};
    auto bounded_scalar = bound(scalar, 4096);
    EXPECT_LE(bounded_scalar.text.size(), 4096u);
    EXPECT_TRUE(bounded_scalar.envelope["data"].is_null());
}

TEST(Budget, ErrorsDropDetailsThenShortenTheMessage) {
    json env = {{"ok", false}, {"error", {{"code", "validation"}, {"message", "bad"}, {"details", items(500, 50)}}}};
    auto bounded = bound(env, 4096);
    EXPECT_LE(bounded.text.size(), 4096u);
    EXPECT_EQ(bounded.envelope["error"]["code"], "validation");
    EXPECT_EQ(bounded.envelope["error"]["message"], "bad");
    EXPECT_EQ(bounded.envelope["error"]["details"]["truncated"], true);

    // A message with multi-byte characters is cut on a character boundary.
    std::string long_message;
    for (int i = 0; i < 4000; ++i) long_message += "\xc3\xa9";
    json long_env = {{"ok", false}, {"error", {{"code", "api"}, {"message", long_message}}}};
    auto shortened = bound(long_env, 4096);
    EXPECT_LE(shortened.text.size(), 4096u);
    EXPECT_FALSE(json::parse(shortened.text, nullptr, false).is_discarded());
    EXPECT_NE(shortened.envelope["error"]["message"].get<std::string>().find("[truncated]"), std::string::npos);
}

TEST(Budget, Utf8Prefix) {
    using forti::mcp::utf8_prefix;
    EXPECT_EQ(utf8_prefix("abc", 10), "abc");
    EXPECT_EQ(utf8_prefix("ab\xc3\xa9", 3), "ab");
    EXPECT_EQ(utf8_prefix("ab\xc3\xa9", 4), "ab\xc3\xa9");
    EXPECT_EQ(utf8_prefix("\xe2\x82\xac", 2), "");
}
