//
// The stdio transport: newline-delimited JSON-RPC on two file descriptors. Lines longer than
// max_message_bytes are discarded as they stream in (memory stays bounded) and answered with an error.
//
// While a tools/call waits for the client's answer to elicitation/create (legacy era, approval.hpp),
// the transport keeps reading stdin itself:
//
//   the awaited response              returned to the tool (responses with other ids are ignored)
//   ping                              answered at once, so the client does not think the server hung
//   notifications/cancelled for the   the wait ends with nothing approved, and no response is sent for
//   waiting tools/call                that tools/call (the spec's rule for cancelled requests)
//   anything else                     queued, then handled in order once the tools/call is answered
//   end of input, a read error,       the wait ends with nothing approved
//   or answer_timeout
//

#ifndef FORTI_MCP_STDIO_HPP
#define FORTI_MCP_STDIO_HPP

#include "protocol.hpp"

#include <chrono>

namespace forti::mcp {

    // Serves until `in` reaches end of file (exit status 0) or a read/write fails (1).
    int serve(Server& server, int in, int out,
              std::chrono::milliseconds answer_timeout = std::chrono::duration_cast<std::chrono::milliseconds>(approval_timeout));

}  // namespace forti::mcp

#endif  // FORTI_MCP_STDIO_HPP
