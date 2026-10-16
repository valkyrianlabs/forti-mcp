#include "stdio.hpp"

#include "budget.hpp"

#include <cerrno>
#include <cstring>
#include <deque>
#include <iostream>
#include <string>

#include <poll.h>
#include <unistd.h>

namespace forti::mcp {

    using json = nlohmann::json;

    namespace {

        bool write_all(int fd, std::string text) {
            text += '\n';
            const char* data = text.data();
            std::size_t left = text.size();
            while (left > 0) {
                ssize_t n = ::write(fd, data, left);
                if (n < 0) {
                    if (errno == EINTR) continue;
                    return false;
                }
                data += n;
                left -= static_cast<std::size_t>(n);
            }
            return true;
        }

        struct Line {
            std::string text;
            bool oversized = false;   // longer than max_message_bytes; its content was discarded
        };

        // Splits stdin into lines, with an optional deadline per wait.
        class LineReader {
            int fd_;
            std::string line_;          // the line being assembled
            bool oversized_ = false;    // discarding the rest of a line that is already too long
            std::string chunk_;         // bytes read but not yet split
            std::size_t chunk_at_ = 0;
            bool eof_ = false;

        public:
            enum class Got { line, eof, error, timeout };
            bool failed = false;   // a read error happened (serve exits 1)

            explicit LineReader(int fd) : fd_(fd) {}

            // The next line. `deadline` unset: wait as long as it takes.
            Got next(Line& out, std::optional<std::chrono::steady_clock::time_point> deadline = std::nullopt) {
                for (;;) {
                    while (chunk_at_ < chunk_.size()) {
                        std::string_view rest = std::string_view(chunk_).substr(chunk_at_);
                        auto newline = rest.find('\n');
                        std::string_view piece = rest.substr(0, newline);
                        if (!oversized_) {
                            if (line_.size() + piece.size() > max_message_bytes) {
                                oversized_ = true;
                                line_.clear();
                                line_.shrink_to_fit();
                            } else {
                                line_.append(piece);
                            }
                        }
                        if (newline == std::string_view::npos) {
                            chunk_at_ = chunk_.size();
                            break;
                        }
                        chunk_at_ += newline + 1;
                        return take(out);
                    }
                    chunk_.clear();
                    chunk_at_ = 0;
                    if (eof_) {
                        // A final message without a trailing newline is still a message.
                        if (oversized_ || !line_.empty()) return take(out);
                        return Got::eof;
                    }
                    if (deadline) {
                        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(*deadline - std::chrono::steady_clock::now());
                        if (left.count() <= 0) return Got::timeout;
                        pollfd poll_fd{fd_, POLLIN, 0};
                        int ready = ::poll(&poll_fd, 1, static_cast<int>(std::min<long long>(left.count(), 60'000)));
                        if (ready < 0 && errno != EINTR) {
                            std::cerr << "forti-mcp: cannot poll stdin: " << std::strerror(errno) << std::endl;
                            failed = true;
                            return Got::error;
                        }
                        if (ready <= 0) continue;   // re-check the deadline
                    }
                    char buffer[64 * 1024];
                    ssize_t n = ::read(fd_, buffer, sizeof buffer);
                    if (n < 0) {
                        if (errno == EINTR) continue;
                        std::cerr << "forti-mcp: cannot read stdin: " << std::strerror(errno) << std::endl;
                        failed = true;
                        return Got::error;
                    }
                    if (n == 0) eof_ = true;
                    else chunk_.assign(buffer, static_cast<std::size_t>(n));
                }
            }

        private:
            Got take(Line& out) {
                out.text = std::move(line_);
                out.oversized = oversized_;
                line_.clear();
                oversized_ = false;
                return Got::line;
            }
        };

        class Transport {
            Server& server_;
            int out_;
            std::chrono::milliseconds answer_timeout_;
            LineReader reader_;
            std::deque<Line> pending_;   // read while waiting for an answer; handled afterwards, in order

        public:
            bool write_failed = false;

            Transport(Server& server, int in, int out, std::chrono::milliseconds answer_timeout)
                : server_(server), out_(out), answer_timeout_(answer_timeout), reader_(in) {}

            bool write(std::string text) {
                if (write_all(out_, std::move(text))) return true;
                if (!write_failed) std::cerr << "forti-mcp: cannot write to stdout: " << std::strerror(errno) << std::endl;
                write_failed = true;
                return false;
            }

            int run() {
                server_.set_ask_client([this](const json& request, const json& call_id) { return ask(request, call_id); });
                for (;;) {
                    Line line;
                    if (!pending_.empty()) {
                        line = std::move(pending_.front());
                        pending_.pop_front();
                    } else if (reader_.next(line) != LineReader::Got::line) {
                        break;
                    }
                    if (line.oversized) write(Server::oversized_response());
                    else if (auto response = server_.handle_line(line.text)) write(std::move(*response));
                    if (write_failed) return 1;
                }
                return reader_.failed ? 1 : 0;
            }

        private:
            ClientReply ask(const json& request, const json& call_id) {
                if (!write(dump(request))) return {ClientReply::Status::closed, nullptr};
                auto deadline = std::chrono::steady_clock::now() + answer_timeout_;
                for (;;) {
                    Line line;
                    switch (reader_.next(line, deadline)) {
                        case LineReader::Got::line: break;
                        case LineReader::Got::timeout: return {ClientReply::Status::timed_out, nullptr};
                        case LineReader::Got::eof:
                        case LineReader::Got::error: return {ClientReply::Status::closed, nullptr};
                    }
                    auto message = line.oversized ? std::nullopt : parse_message(line.text);
                    if (message && message->is_object()) {
                        auto id = message->find("id");
                        auto method = message->find("method");
                        if (method == message->end()) {
                            if (id != message->end() && (message->contains("result") || message->contains("error"))) {
                                if (*id == request["id"]) return {ClientReply::Status::answered, std::move(*message)};
                                continue;   // a response nobody waits for
                            }
                        } else if (*method == "ping" && id != message->end()) {
                            if (auto response = server_.handle(*message)) {
                                if (!write(dump(*response))) return {ClientReply::Status::closed, nullptr};
                            }
                            continue;
                        } else if (*method == "notifications/cancelled" && id == message->end()) {
                            const json& params = message->value("params", json::object());
                            if (params.is_object() && params.contains("requestId") && params["requestId"] == call_id)
                                return {ClientReply::Status::cancelled, nullptr};
                        }
                    }
                    pending_.push_back(std::move(line));
                }
            }
        };

    }  // namespace

    int serve(Server& server, int in, int out, std::chrono::milliseconds answer_timeout) {
        Transport transport(server, in, out, answer_timeout);
        int status = transport.run();
        server.set_ask_client({});
        return status;
    }

}  // namespace forti::mcp
