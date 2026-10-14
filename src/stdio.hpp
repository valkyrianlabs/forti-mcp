//
// The stdio transport: newline-delimited JSON-RPC on two file descriptors. Lines longer than
// max_message_bytes are discarded as they stream in (memory stays bounded) and answered with an error.
//

#ifndef FORTI_MCP_STDIO_HPP
#define FORTI_MCP_STDIO_HPP

#include "protocol.hpp"

namespace forti::mcp {

    // Serves until `in` reaches end of file (exit status 0) or a read/write fails (1).
    int serve(Server& server, int in, int out);

}  // namespace forti::mcp

#endif  // FORTI_MCP_STDIO_HPP
