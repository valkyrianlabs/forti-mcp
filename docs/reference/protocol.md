---
title: Protocol
description: Supported MCP versions, methods, error codes and the stdio transport rules.
order: 95
status: published
tags:
  - reference
---

# Protocol

forti-mcp speaks JSON-RPC 2.0 over stdio, one message per line. It serves both MCP protocol eras in
one process (MCP 2026-07-28, "Versioning and Compatibility"), so current and older clients both work.

:::toc[On this page]{depth="2" theme="compact"}
:::

| Version | Era | How a client uses it |
|---|---|---|
| `2026-07-28` | Modern, stateless | Every request carries `_meta` `io.modelcontextprotocol/protocolVersion` and `io.modelcontextprotocol/clientCapabilities` |
| `2025-11-25` | Legacy | `initialize` starts a session for the rest of the process |
| `2025-06-18` | Legacy | `initialize` starts a session for the rest of the process |

## Modern: 2026-07-28

A request is modern when its `params._meta` contains `io.modelcontextprotocol/protocolVersion`. That
value must be `2026-07-28`, `io.modelcontextprotocol/clientCapabilities` must be an object, and
`io.modelcontextprotocol/clientInfo`, if present, must be an object.

| Method | Result |
|---|---|
| `server/discover` | `supportedVersions`, `capabilities` (`tools`), `instructions`, `ttlMs: 300000`, `cacheScope: "private"` |
| `tools/list` | `tools`, `ttlMs: 300000`, `cacheScope: "private"` |
| `tools/call` | The tool result ([Tools](/reference/tools)) |

Every modern result also carries `resultType: "complete"` and `_meta`
`io.modelcontextprotocol/serverInfo` (`name: forti-mcp`, `title`, `version`, `description`).

```json
{"jsonrpc": "2.0", "id": 1, "method": "tools/list",
 "params": {"_meta": {"io.modelcontextprotocol/protocolVersion": "2026-07-28",
                      "io.modelcontextprotocol/clientCapabilities": {}}}}
```

## Legacy: 2025-11-25 and 2025-06-18

`initialize` requires `protocolVersion` (a string), `capabilities` and `clientInfo` (objects). The
server answers with the requested version when it is `2025-11-25` or `2025-06-18`, and with
`2025-11-25` for anything else, which the client may accept or reject. Its result has
`protocolVersion`, `capabilities` (`tools`), `serverInfo` and `instructions` (`serverInfo.description`
is left out for `2025-06-18`, which predates it). A second `initialize` is an error.

| Method | Notes |
|---|---|
| `initialize` | Starts the session; once per process |
| `ping` | Answers `{}`; allowed before `initialize` |
| `tools/list` | After `initialize` |
| `tools/call` | After `initialize` |

Older versions, such as `2025-03-26`, get `2025-11-25` offered back: `2025-03-26` requires JSON-RPC
batches, which this server rejects. A legacy session and modern requests can share one process; each
request is judged by its own `_meta`.

## Both eras

- `tools/list` returns every tool in one page. No cursor is ever issued, so any `cursor` is invalid.
- Notifications are never answered, even malformed ones. `notifications/initialized` needs no
  action, and `notifications/cancelled` is ignored: requests are handled one at a time, in order, so
  a request has already been answered by the time its cancellation is read.
- Responses from the client (messages with `result` or `error` and no `method`) are ignored; the
  server never sends requests.
- The `instructions` text tells the model the workflow, how each mode answers `forti_apply`, the byte
  budget, and that device data is untrusted.

## Errors

Protocol errors are for messages the server cannot process: malformed JSON-RPC, unknown methods and
unknown tools.

:::callout[Tool errors are not protocol errors]{variant="info"}
Everything the model can fix, including invalid tool arguments and every device error, is a tool
result with `isError: true` instead ([Tools](/reference/tools)).
:::

| Code | When |
|---|---|
| `-32700` | Parse error: the line is not valid JSON |
| `-32600` | Invalid Request: a batch (array), not an object, `jsonrpc` not `"2.0"`, missing or non-string `method`, an `id` that is not a string or integer, a line over 4 MiB, nesting deeper than 64 levels, a second `initialize` |
| `-32601` | Method not found |
| `-32602` | Invalid params: `params` or `_meta` not an object, a missing or malformed field, a legacy method before `initialize` (or `server/discover` without modern `_meta`), any `cursor`, an unknown tool |
| `-32603` | Internal error (details go to stderr, not to the client) |
| `-32022` | Unsupported protocol version: `data.supported` lists the versions, `data.requested` echoes the request |

When the `id` cannot be read, the error response omits it (never `null`). The `-32602` answer to a
legacy method without a session, and to an `initialize` without a version, also lists the supported
versions in `data.supported`.

:::callout[With --read-only]{variant="note"}
Calling `forti_plan` or `forti_apply` on a server started with `--read-only` is `-32602`
"Unknown tool: forti_plan (forti-mcp runs with --read-only; plan and apply are disabled)".
:::

## stdio

- **One message per line** on stdin, one response per line on stdout. Blank lines are ignored; a last
  message without a trailing newline is still handled when stdin closes.
- **At most 4 MiB per message.** A longer line is answered with `-32600` and discarded as it arrives,
  without being buffered.
- **stdout carries protocol messages only.** At startup the server keeps a private descriptor for
  them and points file descriptor 1 at stderr, so nothing else in the process (or a credential helper
  it runs) can corrupt the stream.
- **stderr** gets rare diagnostics, never request content or secrets.
- **Lifetime**: the server runs until stdin closes, then exits with status 0. It exits with 1 when
  it cannot read stdin or write stdout, and with 2 on invalid [command-line options](/reference/command-line).
- **No batches**: MCP removed JSON-RPC batches in 2025-06-18; an array is rejected with `-32600`.

## Try it by hand

The server works without a client. This lists the tools over a legacy session, without contacting a
FortiGate:

```bash
printf '%s\n' \
  '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"shell","version":"0"}}}' \
  '{"jsonrpc":"2.0","method":"notifications/initialized"}' \
  '{"jsonrpc":"2.0","id":2,"method":"tools/list"}' \
  | forti-mcp
```

The first response line is the `initialize` result:

```json
{"id":1,"jsonrpc":"2.0","result":{"capabilities":{"tools":{}},"instructions":"forti-mcp manages FortiGate firewalls through the FortiOS REST API only (never SSH or CLI commands). ...","protocolVersion":"2025-11-25","serverInfo":{"description":"FortiGate configuration and monitoring over the FortiOS REST API","name":"forti-mcp","title":"FortiOS (forti-mcp)","version":"0.1.0"}}}
```
