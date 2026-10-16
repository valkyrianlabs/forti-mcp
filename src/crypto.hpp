//
// The few primitives the MRTR request state needs (approval.hpp): SHA-256, HMAC-SHA256, the OS
// CSPRNG, base64url and a constant-time comparison. Self-contained on purpose: nothing in the
// dependency graph provides libcrypto (forti-api is header-only over libcurl), and adding OpenSSL to
// both package recipes for one MAC is not worth it. Checked against the FIPS 180-4 and RFC 4231 vectors
// in tests/unit/test_approval.cpp.
//

#ifndef FORTI_MCP_CRYPTO_HPP
#define FORTI_MCP_CRYPTO_HPP

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace forti::mcp::crypto {

    using Digest = std::array<std::uint8_t, 32>;

    Digest sha256(std::string_view data);
    Digest hmac_sha256(std::span<const std::uint8_t> key, std::string_view message);

    std::string hex(std::span<const std::uint8_t> bytes);

    // Bytes from the OS CSPRNG (arc4random_buf on Apple/BSD, getentropy elsewhere). Throws when the
    // system generator is unavailable: the caller never falls back to weaker randomness.
    std::vector<std::uint8_t> random_bytes(std::size_t count);

    // Unpadded base64url (RFC 4648 section 5). decode rejects padding, other alphabets and
    // non-canonical trailing bits.
    std::string base64url_encode(std::span<const std::uint8_t> bytes);
    std::optional<std::vector<std::uint8_t>> base64url_decode(std::string_view text);

    // Compares without an early exit, so the time taken does not reveal where a MAC first differs.
    bool equal(std::span<const std::uint8_t> a, std::span<const std::uint8_t> b);

}  // namespace forti::mcp::crypto

#endif  // FORTI_MCP_CRYPTO_HPP
