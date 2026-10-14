#include "stdio.hpp"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>

#include <unistd.h>

namespace forti::mcp {

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

    }  // namespace

    int serve(Server& server, int in, int out) {
        std::string line;
        bool oversized = false;   // discarding the rest of a line that is already too long
        char chunk[64 * 1024];

        auto finish_line = [&]() -> bool {
            bool ok = true;
            if (oversized) {
                ok = write_all(out, Server::oversized_response());
            } else if (auto response = server.handle_line(line)) {
                ok = write_all(out, std::move(*response));
            }
            line.clear();
            oversized = false;
            if (!ok) std::cerr << "forti-mcp: cannot write to stdout: " << std::strerror(errno) << std::endl;
            return ok;
        };

        for (;;) {
            ssize_t n = ::read(in, chunk, sizeof chunk);
            if (n < 0) {
                if (errno == EINTR) continue;
                std::cerr << "forti-mcp: cannot read stdin: " << std::strerror(errno) << std::endl;
                return 1;
            }
            if (n == 0) break;
            std::string_view rest(chunk, static_cast<std::size_t>(n));
            while (!rest.empty()) {
                auto newline = rest.find('\n');
                std::string_view piece = rest.substr(0, newline);
                if (!oversized) {
                    if (line.size() + piece.size() > max_message_bytes) {
                        oversized = true;
                        line.clear();
                        line.shrink_to_fit();
                    } else {
                        line.append(piece);
                    }
                }
                if (newline == std::string_view::npos) break;
                if (!finish_line()) return 1;
                rest.remove_prefix(newline + 1);
            }
        }
        // End of input: a final message without a trailing newline is still a message.
        if ((oversized || !line.empty()) && !finish_line()) return 1;
        return 0;
    }

}  // namespace forti::mcp
