#include "approval.hpp"

#include "budget.hpp"
#include "crypto.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <format>

namespace forti::mcp {

    using json = nlohmann::json;

    namespace {

        constexpr std::size_t max_value_chars = 160;
        constexpr std::size_t max_changes_per_operation = 25;
        // The MAC covers this prefix and the encoded payload, so a token cannot be reused as anything else.
        constexpr std::string_view state_context = "forti-mcp/requestState/1.";

        // Decodes one UTF-8 sequence at `at` (input is valid UTF-8: it came through the JSON parser).
        char32_t next_code_point(std::string_view text, std::size_t& at) {
            auto byte = [&](std::size_t i) { return static_cast<unsigned char>(text[i]); };
            unsigned char lead = byte(at);
            std::size_t length = lead < 0x80 ? 1 : lead >> 5 == 0x6 ? 2 : lead >> 4 == 0xE ? 3 : 4;
            if (at + length > text.size()) length = text.size() - at;
            char32_t cp = length == 1 ? lead : length == 2 ? lead & 0x1F : length == 3 ? lead & 0x0F : lead & 0x07;
            for (std::size_t i = 1; i < length; ++i) cp = (cp << 6) | (byte(at + i) & 0x3F);
            at += length;
            return cp;
        }

        // Characters that can reorder, hide or restyle what the person reads: C0 and C1 controls
        // (ESC starts terminal escape sequences), DEL, zero-width and bidi controls, the BOM.
        bool invisible(char32_t cp) {
            return cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp <= 0x9F) || (cp >= 0x200B && cp <= 0x200F) ||
                   (cp >= 0x202A && cp <= 0x202E) || (cp >= 0x2066 && cp <= 0x2069) || cp == 0xFEFF;
        }

        std::size_t code_points(std::string_view text) {
            return static_cast<std::size_t>(std::ranges::count_if(text, [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
        }

        // Untrusted text made safe to show: invisible characters become spaces (line breaks too, so a
        // value cannot fake a line of the proposal), "://" is defanged so no client renders a link, and
        // the result is cut to `limit` code points.
        std::string clean(std::string_view text, std::size_t limit = max_value_chars) {
            std::string out;
            std::size_t count = 0, at = 0;
            while (at < text.size()) {
                std::size_t from = at;
                char32_t cp = next_code_point(text, at);
                if (count == limit) {
                    out += "…";
                    break;
                }
                if (invisible(cp)) out += ' ';
                else out.append(text.substr(from, at - from));
                ++count;
            }
            for (std::size_t pos = out.find("://"); pos != std::string::npos; pos = out.find("://", pos + 5))
                out.replace(pos, 3, "[:]//");
            return out;
        }

        std::string value_text(const json& value) {
            if (value.is_null()) return "(none)";
            if (value.is_string()) {
                const auto& text = value.get_ref<const std::string&>();
                return text.empty() ? "\"\"" : clean(text);
            }
            return clean(dump(value));
        }

        std::string string_of(const json& object, const char* key) {
            auto it = object.find(key);
            if (it == object.end() || it->is_null()) return {};
            return it->is_string() ? it->get<std::string>() : dump(*it);
        }

        std::string names(const json& list) {
            std::string out;
            std::size_t shown = 0;
            for (const auto& item: list) {
                if (shown == 10) {
                    out += std::format(", … {} more", list.size() - shown);
                    break;
                }
                out += (out.empty() ? "" : ", ") + value_text(item);
                ++shown;
            }
            return out;
        }

        // One diff entry ({attribute, change, before, after, unchanged?, table?}) as one line.
        std::string change_line(const json& entry) {
            std::string attribute = clean(string_of(entry, "attribute"), 64);
            std::string change = string_of(entry, "change");
            if (auto table = entry.find("table"); table != entry.end() && table->is_object()) {
                const json added = table->value("added", json::array());
                const json removed = table->value("removed", json::array());
                std::string parts;
                if (!added.empty()) parts += "add " + names(added);
                if (!removed.empty()) parts += (parts.empty() ? "" : "; ") + std::string("remove ") + names(removed);
                if (parts.empty()) parts = change == "add" ? "(empty)" : "(same entries)";
                return std::format("   {}: {}", attribute, parts);
            }
            json before = entry.value("before", json());
            json after = entry.value("after", json());
            if (change == "add") return std::format("   {} = {}", attribute, value_text(after));
            std::string line = std::format("   {}: {} → {}", attribute, value_text(before), value_text(after));
            if (change == "clear") line += " (reset to default)";
            if (entry.value("unchanged", false)) line += " (unchanged)";
            return line;
        }

        // "20.0 MiB" (whole bytes below 1 KiB): the size a person can take in at a glance.
        std::string size_text(std::uint64_t bytes) {
            if (bytes < 1024) return std::format("{} bytes", bytes);
            const char* units[] = {"KiB", "MiB", "GiB", "TiB"};
            double value = static_cast<double>(bytes) / 1024;
            std::size_t unit = 0;
            for (; value >= 1024 && unit + 1 < std::size(units); ++unit) value /= 1024;
            return std::format("{:.1f} {}", value, units[unit]);
        }

        // An action's body as the person is shown it: inline bodies as the value (capped like any
        // value); a body the engine stored by digest (forti-cli 0.6+: over 64 KiB, the wire record
        // carries "body_ref" {sha256, bytes} instead) by its size and SHA-256, never its content.
        std::string body_line(const json& wire) {
            if (!wire.is_object()) return "   request body: (none)";
            if (auto ref = wire.find("body_ref"); ref != wire.end()) {
                auto sha = ref->is_object() ? ref->find("sha256") : ref->end();
                auto bytes = ref->is_object() ? ref->find("bytes") : ref->end();
                bool valid = ref->is_object() && sha != ref->end() && sha->is_string() && sha->get_ref<const std::string&>().size() == 64 &&
                             std::ranges::all_of(sha->get_ref<const std::string&>(), [](char c) {
                                 return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                             }) &&
                             bytes != ref->end() && bytes->is_number_integer() &&
                             (bytes->is_number_unsigned() || bytes->get<std::int64_t>() >= 0);
                if (!valid) return "   request body: stored separately (unreadable reference)";
                std::uint64_t size = bytes->get<std::uint64_t>();
                std::string shown = size < 1024 ? size_text(size) : std::format("{} ({} bytes)", size_text(size), size);
                return std::format("   request body: {}, stored by digest, sha256 {}", shown, sha->get<std::string>());
            }
            return "   request body: " + value_text(wire.value("body", json()));
        }

        std::vector<std::string> operation_lines(const json& op, std::size_t number) {
            std::vector<std::string> lines;
            std::string kind = string_of(op, "op");
            std::string head = std::format("{}. {} {}", number, clean(kind, 16), clean(string_of(op, "resource"), 128));
            // A child table entry: the parent object, its table attribute, then the entry's key.
            std::string child = string_of(op, "child");
            if (std::string parent = string_of(op, "parent"); !parent.empty() && !child.empty())
                head += " \"" + clean(parent, 128) + "\" " + clean(child, 64);
            if (std::string key = string_of(op, "key"); !key.empty()) head += " \"" + clean(key, 128) + "\"";
            if (std::string risk = string_of(op, "risk"); !risk.empty() && risk != "low") head += std::format(" (risk {})", clean(risk, 16));
            lines.push_back(std::move(head));
            if (kind == "delete") {
                lines.emplace_back(child.empty() ? "   deletes the object"
                                                 : std::format("   deletes this {} entry; the others are left alone", clean(child, 64)));
            } else if (kind == "action") {
                lines.push_back(body_line(op.value("wire", json::object())));
            }
            const json diff = op.value("diff", json::array());
            std::size_t shown = 0;
            for (const auto& entry: diff) {
                if (!entry.is_object()) continue;
                if (shown == max_changes_per_operation) {
                    lines.push_back(std::format("   … {} more attribute changes", diff.size() - shown));
                    break;
                }
                lines.push_back(change_line(entry));
                ++shown;
            }
            return lines;
        }

        std::int64_t unix_seconds(RequestState::Clock::time_point time) {
            return std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch()).count();
        }

        std::span<const std::uint8_t> bytes(std::string_view text) {
            return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
        }

    }  // namespace

    std::string client_label(const json& client_info) {
        std::string name;
        if (client_info.is_object())
            if (auto it = client_info.find("name"); it != client_info.end() && it->is_string()) name = it->get<std::string>();
        std::string out;
        for (char c: name) {
            if (out.size() == 64) break;
            bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
            out += plain ? c : '-';
        }
        return out.empty() ? "unknown" : out;
    }

    bool supports_form_elicitation(const json& capabilities) {
        if (!capabilities.is_object()) return false;
        auto elicitation = capabilities.find("elicitation");
        if (elicitation == capabilities.end() || !elicitation->is_object()) return false;
        if (elicitation->empty()) return true;
        auto form = elicitation->find("form");
        return form != elicitation->end() && form->is_object();
    }

    std::string proposal_message(const json& plan) {
        const json device = plan.value("device", json::object());
        const json operations = plan.value("operations", json::array());
        std::string id = clean(string_of(plan, "id"), 64);

        std::vector<std::string> lines;
        lines.push_back(std::format("Apply these changes to FortiGate {}?", clean(string_of(device, "profile"), 64)));
        lines.emplace_back();
        std::string where = std::format("Device: {} ({}", clean(string_of(device, "profile"), 64), clean(string_of(device, "host"), 128));
        if (std::string serial = string_of(device, "serial"); !serial.empty()) where += ", serial " + clean(serial, 64);
        if (std::string vdom = string_of(device, "vdom"); !vdom.empty()) where += ", vdom " + clean(vdom, 32);
        lines.push_back(where + ")");
        std::string mode = plan.contains("mode") && plan["mode"].is_string() ? plan["mode"].get<std::string>() : "unknown";
        lines.push_back(std::format("Mode: {} · Risk: {} · {} operation{}", clean(mode, 16), clean(string_of(plan, "risk"), 16),
                                    operations.size(), operations.size() == 1 ? "" : "s"));
        lines.emplace_back();

        std::string footer = std::format("Plan {}", id);
        // Room for the cut notice and the footer; the rest is the budget for operations.
        const std::size_t budget = max_proposal_chars - 160 - code_points(footer);
        std::size_t used = 0;
        for (const auto& line: lines) used += code_points(line) + 1;

        std::size_t complete = 0;
        bool cut = false;
        for (std::size_t i = 0; i < operations.size() && !cut; ++i) {
            if (!operations[i].is_object()) continue;
            for (auto& line: operation_lines(operations[i], i + 1)) {
                std::size_t size = code_points(line) + 1;
                if (used + size > budget) {
                    cut = true;
                    break;
                }
                used += size;
                lines.push_back(std::move(line));
            }
            if (!cut) ++complete;
        }
        if (cut)
            lines.push_back(std::format("… {} more operation{} not shown. The full plan: forti plan show {}",
                                        operations.size() - complete, operations.size() - complete == 1 ? "" : "s", id));
        lines.emplace_back();
        lines.push_back(std::move(footer));

        std::string out;
        for (const auto& line: lines) out += line + "\n";
        out.pop_back();
        return out;
    }

    json elicitation_params(const json& plan, bool with_mode) {
        const json device = plan.value("device", json::object());
        json params = {{"message", proposal_message(plan)},
                       {"requestedSchema",
                        {{"type", "object"},
                         {"properties",
                          {{std::string(approval_input),
                            {{"type", "boolean"},
                             {"title", "Apply these changes"},
                             {"description", std::format("Check to apply plan {} to {} now. Leave unchecked, decline or "
                                                         "cancel to apply nothing.",
                                                         clean(string_of(plan, "id"), 64),
                                                         clean(string_of(device, "profile"), 64))},
                             {"default", false}}}}},
                         {"required", {std::string(approval_input)}}}}};
        if (with_mode) params["mode"] = "form";
        return params;
    }

    Answer interpret(const json& result) {
        if (!result.is_object()) return Answer::cancelled;
        auto action = result.find("action");
        if (action == result.end() || !action->is_string()) return Answer::cancelled;
        if (*action == "accept") {
            auto content = result.find("content");
            if (content != result.end() && content->is_object()) {
                auto approve = content->find(approval_input);
                if (approve != content->end() && approve->is_boolean() && approve->get<bool>()) return Answer::approved;
            }
            return Answer::declined;   // submitted the form without checking the box
        }
        if (*action == "decline") return Answer::declined;
        return Answer::cancelled;
    }

    // ---- requestState ----

    RequestState::RequestState() : key_(crypto::random_bytes(32)) {}

    std::string RequestState::seal(json payload, Clock::time_point now) {
        payload["exp"] = unix_seconds(now + approval_timeout);
        payload["nonce"] = crypto::hex(crypto::random_bytes(16));
        std::string body = crypto::base64url_encode(bytes(dump(payload)));
        auto mac = crypto::hmac_sha256(key_, std::string(state_context) + body);
        return body + "." + crypto::base64url_encode(mac);
    }

    std::variant<json, std::string> RequestState::open(std::string_view token, Clock::time_point now) {
        if (token.size() > 4096) return std::string("invalid");
        auto dot = token.rfind('.');
        if (dot == std::string_view::npos) return std::string("invalid");
        std::string_view body = token.substr(0, dot);
        auto mac = crypto::base64url_decode(token.substr(dot + 1));
        auto expected = crypto::hmac_sha256(key_, std::string(state_context) + std::string(body));
        if (!mac || !crypto::equal(*mac, expected)) return std::string("invalid");

        auto decoded = crypto::base64url_decode(body);
        if (!decoded) return std::string("invalid");
        json payload = json::parse(decoded->begin(), decoded->end(), nullptr, false);
        if (payload.is_discarded() || !payload.is_object() || !payload["exp"].is_number_integer() || !payload["nonce"].is_string())
            return std::string("invalid");

        std::int64_t seconds = unix_seconds(now);
        std::erase_if(consumed_, [&](const auto& entry) { return entry.second <= seconds; });
        if (payload["exp"].get<std::int64_t>() <= seconds) return std::string("expired");
        if (!consumed_.emplace(payload["nonce"].get<std::string>(), payload["exp"].get<std::int64_t>()).second)
            return std::string("replayed");
        return payload;
    }

}  // namespace forti::mcp
