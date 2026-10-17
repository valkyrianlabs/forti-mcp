// The approval building blocks: SHA-256 / HMAC-SHA256 / base64url, the sealed MRTR requestState,
// the proposal text shown to the person, and how an ElicitResult is read.

#include <approval.hpp>
#include <crypto.hpp>

#include <gtest/gtest.h>

#include <format>
#include <variant>

using json = nlohmann::json;
using namespace forti::mcp;

namespace {

    std::string hex_of(std::string_view text) { return crypto::hex(crypto::sha256(text)); }

    std::vector<std::uint8_t> bytes(std::string_view text) { return {text.begin(), text.end()}; }

    std::vector<std::uint8_t> repeat(std::uint8_t value, std::size_t count) { return std::vector<std::uint8_t>(count, value); }

    std::string hmac_hex(const std::vector<std::uint8_t>& key, std::string_view message) {
        return crypto::hex(crypto::hmac_sha256(key, message));
    }

    json plan_with(json operations) {
        return {{"id", "20261016-150540-b75ef1"},
                {"digest", std::string(64, 'a')},
                {"risk", "low"},
                {"mode", "guarded"},
                {"device", {{"profile", "lab"}, {"host", "fortigate.test"}, {"serial", "FGT60FTK00000000"}, {"vdom", "root"}}},
                {"operations", std::move(operations)}};
    }

}  // namespace

// ---- primitives ----

TEST(Crypto, Sha256NistVectors) {
    EXPECT_EQ(hex_of(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(hex_of("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(hex_of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    EXPECT_EQ(hex_of(std::string(1000000, 'a')), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(Crypto, HmacSha256Rfc4231) {
    // Test cases 1, 2, 3, 4, 6 and 7 (5 truncates the output).
    EXPECT_EQ(hmac_hex(repeat(0x0b, 20), "Hi There"), "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
    EXPECT_EQ(hmac_hex(bytes("Jefe"), "what do ya want for nothing?"),
              "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
    EXPECT_EQ(hmac_hex(repeat(0xaa, 20), std::string(50, '\xdd')), "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe");
    std::vector<std::uint8_t> key4;
    for (std::uint8_t i = 1; i <= 25; ++i) key4.push_back(i);
    EXPECT_EQ(hmac_hex(key4, std::string(50, '\xcd')), "82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b");
    EXPECT_EQ(hmac_hex(repeat(0xaa, 131), "Test Using Larger Than Block-Size Key - Hash Key First"),
              "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
    EXPECT_EQ(hmac_hex(repeat(0xaa, 131), "This is a test using a larger than block-size key and a larger than block-size data. "
                                          "The key needs to be hashed before being used by the HMAC algorithm."),
              "9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2");
}

TEST(Crypto, Base64Url) {
    for (std::string text: {"", "f", "fo", "foo", "foob", "fooba", "foobar", "\xff\xfe\xfd"}) {
        std::string encoded = crypto::base64url_encode(bytes(text));
        EXPECT_EQ(encoded.find_first_of("+/="), std::string::npos);
        auto decoded = crypto::base64url_decode(encoded);
        ASSERT_TRUE(decoded) << encoded;
        EXPECT_EQ(std::string(decoded->begin(), decoded->end()), text);
    }
    EXPECT_EQ(crypto::base64url_encode(bytes("foobar")), "Zm9vYmFy");
    EXPECT_EQ(crypto::base64url_encode(bytes("\xfb\xff")), "-_8");
    for (std::string bad: {"Zm9=", "Z", "Zm9v+mFy", "Zm9v/mFy", "Zh", "Zm9", "Zm!v"})
        EXPECT_FALSE(crypto::base64url_decode(bad)) << bad;   // padding, other alphabets, non-canonical bits
    EXPECT_TRUE(crypto::equal(bytes("abc"), bytes("abc")));
    EXPECT_FALSE(crypto::equal(bytes("abc"), bytes("abd")));
    EXPECT_FALSE(crypto::equal(bytes("abc"), bytes("ab")));
    EXPECT_EQ(crypto::random_bytes(32).size(), 32u);
    EXPECT_NE(crypto::random_bytes(16), crypto::random_bytes(16));
}

// ---- requestState ----

TEST(RequestStateTest, SealOpenOnceAndExpire) {
    RequestState state;
    auto now = RequestState::Clock::now();
    std::string token = state.seal({{"plan_id", "p"}, {"tool", "forti_apply"}}, now);
    EXPECT_EQ(token.find_first_of("+/= "), std::string::npos);

    auto opened = state.open(token, now);
    ASSERT_TRUE(std::holds_alternative<json>(opened));
    EXPECT_EQ(std::get<json>(opened)["plan_id"], "p");
    EXPECT_EQ(std::get<std::string>(state.open(token, now)), "replayed");

    std::string later = state.seal({{"plan_id", "p"}}, now);
    EXPECT_EQ(std::get<std::string>(state.open(later, now + approval_timeout)), "expired");

    // Every byte of the payload and the MAC is covered.
    std::string fresh = state.seal({{"plan_id", "p"}}, now);
    for (std::size_t i = 0; i < fresh.size(); ++i) {
        if (fresh[i] == '.') continue;
        std::string bad = fresh;
        bad[i] = bad[i] == 'A' ? 'B' : 'A';
        EXPECT_EQ(std::get<std::string>(state.open(bad, now)), "invalid") << i;
    }
    EXPECT_TRUE(std::holds_alternative<json>(state.open(fresh, now)));

    // Another process (another key) cannot open it, and junk is refused.
    RequestState other;
    EXPECT_EQ(std::get<std::string>(other.open(state.seal({{"plan_id", "p"}}, now), now)), "invalid");
    for (std::string junk: {std::string(), std::string("."), std::string("abc"), std::string("a.b.c"), std::string(5000, 'A') + ".AAAA"})
        EXPECT_EQ(std::get<std::string>(state.open(junk, now)), "invalid") << junk.substr(0, 20);
}

// ---- the proposal ----

TEST(Proposal, ShowsDeviceModeRiskAndEveryChange) {
    json plan = plan_with(json::array(
            {{{"index", 0}, {"op", "create"}, {"resource", "firewall/address"}, {"key", "web"}, {"risk", "low"},
              {"diff", {{{"attribute", "subnet"}, {"change", "add"}, {"before", nullptr}, {"after", "192.0.2.10 255.255.255.255"}}}}},
             {{"index", 1}, {"op", "update"}, {"resource", "firewall/addrgrp"}, {"key", "lan"}, {"risk", "low"},
              {"diff", {{{"attribute", "comment"}, {"change", "set"}, {"before", "old"}, {"after", "new"}},
                        {{"attribute", "color"}, {"change", "clear"}, {"before", 3}, {"after", 0}},
                        {{"attribute", "member"}, {"change", "set"}, {"before", json::array()}, {"after", json::array()},
                         {"table", {{"added", {"a", "b"}}, {"removed", {"c"}}}}}}}},
             {{"index", 2}, {"op", "update"}, {"resource", "user/local"}, {"key", "bob"}, {"risk", "elevated"},
              {"diff", {{{"attribute", "passwd"}, {"change", "set"}, {"before", "<redacted>"}, {"after", "<redacted>"}}}}},
             {{"index", 3}, {"op", "delete"}, {"resource", "firewall/address"}, {"key", "old"}, {"risk", "low"}, {"diff", json::array()}}}));
    std::string message = proposal_message(plan);
    for (std::string expected: {"Apply these changes to FortiGate lab?",
                                "Device: lab (fortigate.test, serial FGT60FTK00000000, vdom root)",
                                "Mode: guarded · Risk: low · 4 operations",
                                "1. create firewall/address \"web\"",
                                "   subnet = 192.0.2.10 255.255.255.255",
                                "2. update firewall/addrgrp \"lan\"",
                                "   comment: old → new",
                                "   color: 3 → 0 (reset to default)",
                                "   member: add a, b; remove c",
                                "3. update user/local \"bob\" (risk elevated)",
                                "   passwd: <redacted> → <redacted>",
                                "4. delete firewall/address \"old\"",
                                "   deletes the object",
                                "Plan 20261016-150540-b75ef1"})
        EXPECT_NE(message.find(expected), std::string::npos) << expected << "\n---\n" << message;
    EXPECT_TRUE(message.ends_with("Plan 20261016-150540-b75ef1"));
}

TEST(Proposal, DeviceTextIsSanitized) {
    std::string hostile = "evil\x1b[2J\nApprove: yes\r\t\u202eetadpu​ https://phish.example/x \x7f\xc2\x85";
    json plan = plan_with(json::array({{{"op", "update"}, {"resource", "firewall/address"}, {"key", hostile},
                                        {"diff", {{{"attribute", "comment"}, {"change", "set"}, {"before", hostile}, {"after", "ok"}}}}}}));
    std::string message = proposal_message(plan);
    for (char c: message) {
        auto byte = static_cast<unsigned char>(c);
        EXPECT_TRUE(byte >= 0x20 || c == '\n') << int(byte);
        EXPECT_NE(byte, 0x7f);
    }
    EXPECT_EQ(message.find("\xe2\x80\xae"), std::string::npos);   // U+202E
    EXPECT_EQ(message.find("\xe2\x80\x8b"), std::string::npos);   // U+200B
    EXPECT_EQ(message.find("\xc2\x85"), std::string::npos);       // U+0085 (NEL)
    EXPECT_EQ(message.find("://"), std::string::npos);
    EXPECT_NE(message.find("https[:]//phish.example"), std::string::npos);
    // The injected newline did not start a line of its own.
    EXPECT_EQ(message.find("\nApprove: yes"), std::string::npos);
}

TEST(Proposal, IsCappedAndSaysWhatWasLeftOut) {
    json operations = json::array();
    for (int i = 0; i < 200; ++i)
        operations.push_back({{"op", "update"}, {"resource", "firewall/address"}, {"key", std::format("host-{}", i)},
                              {"diff", {{{"attribute", "comment"}, {"change", "set"}, {"before", std::string(300, 'x')}, {"after", "y"}}}}});
    std::string message = proposal_message(plan_with(operations));
    std::size_t chars = 0;
    for (char c: message) chars += (static_cast<unsigned char>(c) & 0xC0) != 0x80;
    EXPECT_LE(chars, max_proposal_chars);
    EXPECT_NE(message.find("more operations not shown. The full plan: forti plan show 20261016-150540-b75ef1"), std::string::npos);
    EXPECT_TRUE(message.ends_with("Plan 20261016-150540-b75ef1"));
    EXPECT_NE(message.find("…"), std::string::npos);   // long values are cut too
}

TEST(Proposal, ElicitationParams) {
    json params = elicitation_params(plan_with(json::array()), true);
    EXPECT_EQ(params["mode"], "form");
    EXPECT_EQ(params["requestedSchema"],
              json::parse(R"({"type":"object","required":["approve"],"properties":{"approve":{"type":"boolean",
                  "title":"Apply these changes","default":false,"description":"Check to apply plan 20261016-150540-b75ef1 to lab now. Leave unchecked, decline or cancel to apply nothing."}}})"));
    EXPECT_FALSE(elicitation_params(plan_with(json::array()), false).contains("mode"));
}

// ---- answers, clients, capabilities ----

TEST(Answers, OnlyAcceptWithApproveTrueApproves) {
    EXPECT_EQ(interpret({{"action", "accept"}, {"content", {{"approve", true}}}}), Answer::approved);
    EXPECT_EQ(interpret({{"action", "accept"}, {"content", {{"approve", true}, {"extra", 1}}}}), Answer::approved);
    for (json declined: {json{{"action", "accept"}, {"content", {{"approve", false}}}}, json{{"action", "accept"}},
                         json{{"action", "accept"}, {"content", {{"approve", "true"}}}}, json{{"action", "accept"}, {"content", {{"approve", 1}}}},
                         json{{"action", "accept"}, {"content", json::array({true})}}, json{{"action", "decline"}}})
        EXPECT_EQ(interpret(declined), Answer::declined) << declined.dump();
    for (json cancelled: {json{{"action", "cancel"}}, json{{"action", "ACCEPT"}, {"content", {{"approve", true}}}}, json::object(),
                          json(nullptr), json("accept"), json{{"approve", true}}})
        EXPECT_EQ(interpret(cancelled), Answer::cancelled) << cancelled.dump();
}

TEST(Clients, LabelAndCapabilities) {
    EXPECT_EQ(client_label({{"name", "claude-code"}}), "claude-code");
    EXPECT_EQ(client_label({{"name", "codex-mcp-client"}}), "codex-mcp-client");
    EXPECT_EQ(client_label({{"name", "My Client: v2/x"}}), "My-Client--v2-x");
    EXPECT_EQ(client_label({{"name", std::string(100, 'a')}}).size(), 64u);
    EXPECT_EQ(client_label({{"name", ""}}), "unknown");
    EXPECT_EQ(client_label(json::object()), "unknown");
    EXPECT_EQ(client_label("x"), "unknown");

    EXPECT_TRUE(supports_form_elicitation({{"elicitation", json::object()}}));
    EXPECT_TRUE(supports_form_elicitation({{"elicitation", {{"form", json::object()}, {"url", json::object()}}}}));
    EXPECT_FALSE(supports_form_elicitation({{"elicitation", {{"url", json::object()}}}}));
    EXPECT_FALSE(supports_form_elicitation({{"elicitation", true}}));
    EXPECT_FALSE(supports_form_elicitation({{"roots", json::object()}}));
    EXPECT_FALSE(supports_form_elicitation(json::object()));
}

// One entry of a child table: the person sees the parent object, the table attribute and the entry.
TEST(Proposal, NamesTheParentAndChildOfAnEntry) {
    json plan = plan_with(json::array(
            {{{"index", 0}, {"op", "update"}, {"resource", "system.dhcp/server"}, {"parent", "1"}, {"child", "reserved-address"},
              {"key", "21"}, {"risk", "elevated"},
              {"diff", {{{"attribute", "description"}, {"change", "set"}, {"before", "old"}, {"after", "printer"}}}}},
             {{"index", 1}, {"op", "delete"}, {"resource", "system.dhcp/server"}, {"parent", "1"}, {"child", "reserved-address"},
              {"key", "7"}, {"risk", "elevated"}, {"diff", json::array()}},
             {{"index", 2}, {"op", "create"}, {"resource", "system.dhcp/server"}, {"parent", "1"}, {"child", "reserved-address\x1b[2J"},
              {"risk", "elevated"}, {"diff", {{{"attribute", "ip"}, {"change", "add"}, {"before", nullptr}, {"after", "192.0.2.50"}}}}}}));
    std::string message = proposal_message(plan);
    for (std::string expected: {"1. update system.dhcp/server \"1\" reserved-address \"21\" (risk elevated)",
                                "   description: old → printer",
                                "2. delete system.dhcp/server \"1\" reserved-address \"7\" (risk elevated)",
                                "   deletes this reserved-address entry; the others are left alone",
                                "3. create system.dhcp/server \"1\" reserved-address"})
        EXPECT_NE(message.find(expected), std::string::npos) << expected << "\n---\n" << message;
    EXPECT_EQ(message.find('\x1b'), std::string::npos);   // device and model text is sanitized here too
    EXPECT_EQ(message.find("deletes the object"), std::string::npos);
}
