//
// Response budget. A tool result is the engine envelope ({"ok", "data", "meta"} or {"ok", "error"});
// its compact JSON must fit `max_bytes`. When it does not, items are dropped, never bytes: the result
// is always complete JSON, and meta says what was cut so the model can page or narrow the request.
//
//   data is an array (listings)   keep the longest prefix that fits; meta.truncated = true,
//                                 meta.returned = kept, meta.next_start = start + kept
//   data is an object             shrink its largest nested arrays (meta.trimmed lists each JSON pointer
//                                 with kept/total); no paging exists there, so meta.hint says how to narrow
//   still too large               data = null, meta.hint
//   error envelope                details dropped, then the message shortened
//
// structuredContent and the text block carry the same JSON, so the JSON-RPC line is about twice the
// budget plus a fixed overhead.
//

#ifndef FORTI_MCP_BUDGET_HPP
#define FORTI_MCP_BUDGET_HPP

#include <nlohmann/json.hpp>

#include <cstddef>
#include <string>
#include <string_view>

namespace forti::mcp {

    // Compact JSON. Invalid UTF-8 (never expected: every string came through a JSON parser) is replaced,
    // so serialization cannot throw.
    std::string dump(const nlohmann::json& value);

    // The longest prefix of `text` of at most `max` bytes that does not split a UTF-8 sequence.
    std::string_view utf8_prefix(std::string_view text, std::size_t max);

    struct Bounded {
        nlohmann::json envelope;   // possibly trimmed
        std::string text;          // dump(envelope), at most max_bytes
    };

    Bounded bound(nlohmann::json envelope, std::size_t max_bytes);

}  // namespace forti::mcp

#endif  // FORTI_MCP_BUDGET_HPP
