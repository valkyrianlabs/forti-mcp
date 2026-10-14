#include "budget.hpp"

#include <algorithm>
#include <format>
#include <vector>

namespace forti::mcp {

    using json = nlohmann::json;

    std::string dump(const json& value) { return value.dump(-1, ' ', false, json::error_handler_t::replace); }

    std::string_view utf8_prefix(std::string_view text, std::size_t max) {
        if (text.size() <= max) return text;
        std::size_t end = max;
        while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) --end;   // continuation byte
        return text.substr(0, end);
    }

    namespace {

        constexpr std::size_t max_walk_depth = 16;

        json& meta_of(json& envelope) {
            json& meta = envelope["meta"];
            if (!meta.is_object()) meta = json::object();
            return meta;
        }

        std::size_t start_of(const json& meta) {
            auto it = meta.find("start");
            return it != meta.end() && it->is_number_integer() && it->get<long long>() >= 0 ? it->get<std::size_t>() : 0;
        }

        // ---- data is an array: keep a prefix ----

        void trim_listing(json& envelope, std::size_t max_bytes) {
            json& data = envelope["data"];
            json& meta = meta_of(envelope);
            const std::size_t start = start_of(meta);
            const std::size_t total = data.size();

            std::vector<std::size_t> sizes;
            sizes.reserve(total);
            for (const auto& item: data) sizes.push_back(dump(item).size());

            auto finish = [&](std::size_t kept) {
                meta["truncated"] = true;
                meta["returned"] = kept;
                meta["next_start"] = start + kept;
            };

            // Base size with an empty array and the widest meta values, so the estimate below is an
            // upper bound; the final dump verifies it.
            json items = std::move(data);
            data = json::array();
            finish(total);
            std::size_t base = dump(envelope).size();
            std::size_t kept = 0, used = base;
            while (kept < total && used + sizes[kept] + (kept ? 1 : 0) <= max_bytes) used += sizes[kept] + (kept ? 1 : 0), ++kept;

            items.erase(items.begin() + static_cast<std::ptrdiff_t>(kept), items.end());
            data = std::move(items);
            finish(kept);
            while (kept > 0 && dump(envelope).size() > max_bytes) {
                data.erase(data.end() - 1);
                finish(--kept);
            }
            if (kept == 0)
                meta["hint"] = std::format("the item at start={} alone exceeds the {}-byte budget: select attributes "
                                           "with fields, or read it by key", start, max_bytes);
        }

        // ---- data is an object: shrink its largest nested arrays ----

        void collect_arrays(json& value, const std::string& pointer, std::size_t depth, std::vector<std::pair<std::string, json*>>& out) {
            if (depth > max_walk_depth) return;
            if (value.is_array()) {
                if (!value.empty()) out.emplace_back(pointer, &value);
                std::size_t index = 0;
                for (auto& item: value) collect_arrays(item, std::format("{}/{}", pointer, index++), depth + 1, out);
            } else if (value.is_object()) {
                for (auto it = value.begin(); it != value.end(); ++it) {
                    std::string key = it.key();
                    std::string escaped;
                    for (char c: key) escaped += c == '~' ? "~0" : c == '/' ? "~1" : std::string(1, c);
                    collect_arrays(it.value(), pointer + "/" + escaped, depth + 1, out);
                }
            }
        }

        bool trim_nested(json& envelope, std::size_t max_bytes) {
            json& meta = meta_of(envelope);
            meta["truncated"] = true;
            // Set before trimming so its bytes are counted.
            meta["hint"] = "arrays inside the result were shortened to fit the budget (meta.trimmed: JSON pointer -> "
                           "kept/total): narrow the request with fields, key or filter";
            json trimmed = json::object();
            for (int round = 0; round < 64; ++round) {
                std::size_t size = dump(envelope).size();
                if (size <= max_bytes) break;
                std::vector<std::pair<std::string, json*>> arrays;
                collect_arrays(envelope["data"], "", 0, arrays);
                if (arrays.empty()) break;
                // The largest array by serialized size; nested arrays inside it are reached on later rounds.
                std::size_t best = 0, best_size = 0;
                for (std::size_t i = 0; i < arrays.size(); ++i) {
                    std::size_t s = dump(*arrays[i].second).size();
                    if (s > best_size) best = i, best_size = s;
                }
                auto& [pointer, array] = arrays[best];
                std::size_t total = array->size();
                std::size_t excess = size - max_bytes;
                std::size_t dropped = 0, kept = total;
                while (kept > 0 && dropped < excess) dropped += dump((*array)[--kept]).size() + 1;
                array->erase(array->begin() + static_cast<std::ptrdiff_t>(kept), array->end());
                std::string where = pointer.empty() ? "/" : pointer;
                std::size_t original = trimmed.contains(where) ? trimmed[where]["total"].get<std::size_t>() : total;
                trimmed[where] = {{"kept", kept}, {"total", original}};
                meta["trimmed"] = trimmed;
            }
            return dump(envelope).size() <= max_bytes;
        }

        Bounded bound_error(json envelope, std::size_t max_bytes) {
            json& error = envelope["error"];
            if (!error.is_object()) error = {{"code", "internal"}, {"message", "malformed error"}};
            if (dump(envelope).size() > max_bytes && error.contains("details"))
                error["details"] = {{"truncated", true}};
            std::string text = dump(envelope);
            if (text.size() > max_bytes && error.contains("message") && error["message"].is_string()) {
                std::string message = error["message"].get<std::string>();
                std::size_t overhead = text.size() - dump(error["message"]).size();
                std::size_t room = max_bytes > overhead + 64 ? max_bytes - overhead - 64 : 0;
                error["message"] = std::string(utf8_prefix(message, room)) + " [truncated]";
                text = dump(envelope);
            }
            if (text.size() > max_bytes) {   // pathological: other members of the error
                envelope = {{"ok", false}, {"error", {{"code", error.value("code", "internal")},
                                                      {"message", "error too large for the response budget"}}}};
                text = dump(envelope);
            }
            return {std::move(envelope), std::move(text)};
        }

    }  // namespace

    Bounded bound(json envelope, std::size_t max_bytes) {
        std::string text = dump(envelope);
        if (text.size() <= max_bytes) return {std::move(envelope), std::move(text)};
        if (!envelope.is_object() || !envelope.value("ok", false)) return bound_error(std::move(envelope), max_bytes);

        std::size_t original = text.size();
        meta_of(envelope)["budget_bytes"] = max_bytes;   // before trimming, so it is counted
        json& data = envelope["data"];
        bool fits = false;
        if (data.is_array()) {
            trim_listing(envelope, max_bytes);
            fits = dump(envelope).size() <= max_bytes;
        } else if (data.is_object()) {
            fits = trim_nested(envelope, max_bytes);
        }
        if (!fits) {
            json& meta = meta_of(envelope);
            envelope["data"] = nullptr;
            meta["truncated"] = true;
            meta.erase("trimmed");
            meta["hint"] = std::format("the result ({} bytes) exceeds the {}-byte budget: narrow it with fields, key, "
                                       "filter or a smaller count", original, max_bytes);
        }
        text = dump(envelope);
        if (text.size() > max_bytes) {   // meta itself is huge: keep only what the model needs to react
            json meta = {{"truncated", true}, {"budget_bytes", max_bytes},
                         {"hint", "the result metadata alone exceeds the budget"}};
            envelope = {{"ok", true}, {"data", nullptr}, {"meta", std::move(meta)}};
            text = dump(envelope);
        }
        return {std::move(envelope), std::move(text)};
    }

}  // namespace forti::mcp
