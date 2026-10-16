# forti-mcp

`forti-mcp`, the FortiOS MCP server: a thin, native stdio adapter over **forti-engine**, the command
engine it shares with `forti` (forti-cli). C++23, Meson. The engine is built from forti-cli's source as
a subproject and linked statically: no daemon, no network listener, no `forti` subprocess. The
ecosystem architecture and contracts are in `../.claude/context/architecture.md` in the parent
workspace ("Engine contract", "Operating modes", "MCP surface").

```sh
ln -sfn "$(cd ../forti-cli && pwd)" subprojects/forti-cli   # engine co-development (else the wrap clones the pinned commit)
ln -sfn "$(cd ../forti-api && pwd)" subprojects/forti-api
meson setup build/dev -Dtests=enabled --buildtype=debugoptimized
meson compile -C build/dev   # no TU needs more than ~1.2 GB (GCC)
meson test -C build/dev --print-errorlogs    # suites: unit (protocol, tools, budget), e2e (spawns the binary),
                                             # integration (forti-mcp + forti vs forti-cli's mock FortiOS; python3 + openssl)
```

## Layout

| Path | What |
|---|---|
| `src/protocol.*` | JSON-RPC 2.0 / MCP core, no I/O: `Server::handle(json)`, `handle_line(string)`. Both protocol eras |
| `src/tools.*` | The four tools (schemas, descriptions, annotations), argument validation, engine calls, the forti_apply approval flow |
| `src/approval.*`, `src/crypto.*` | In-chat approval: the proposal text, ElicitResult reading, the HMAC-sealed MRTR `requestState` (self-contained SHA-256/HMAC) |
| `src/budget.*` | The per-result byte budget: trims whole items, never bytes, and reports it in `meta` |
| `src/options.*` | Command-line flags |
| `src/stdio.*`, `src/main.cpp` | The newline-delimited stdin/stdout loop (with the wait for an elicitation answer: pings answered, other messages queued) and process setup |
| `tests/unit/` | GoogleTest over `Server::handle` and the tools, engine on a scripted transport |
| `tests/e2e/` | Spawns the built binary with a temporary `FORTI_HOME` and config/state/cache directories |
| `tests/integration/` | Real forti-mcp over stdio and real forti against forti-cli's `tests/mock-fortios` (needs a forti-cli with `subproject_cli`) |
| `tests/support/fake_transport.hpp` | Scripted `fortiapi::Transport` (copied from forti-cli; keep in sync) |
| `release.toml`, `debian/`, `packaging/`, `.release/` | vl-release contract, Debian and Homebrew packaging, staged release docs (`.claude/context/release.md`) |
| `packaging/forti-cli-pin.sh` | Keeps package builds on the wrap's forti-cli revision (`vendor` for Debian, `check`/`update` for the formula's resource) |
| `ci/`, `.github/` | CI container, smoke test, CI and release workflows |

## Packaging

The engine pin is `subprojects/forti-cli.wrap` (a full forti-cli commit id). Changing it: edit the
`revision`, run `packaging/forti-cli-pin.sh update` (the Homebrew formula's resource follows), commit
both. Release builds never use a symlinked checkout. Keep `.release/*_NEXT.md` current with
user-visible changes and run `vlr check` (`.claude/skills/vl-release/`). Release details:
`.claude/context/release.md`.

## Protocol

- Dual-era (spec 2026-07-28, "Versioning and Compatibility"). **Modern** `2026-07-28`: stateless; every
  request carries `_meta` `io.modelcontextprotocol/protocolVersion` and `clientCapabilities`; methods
  `server/discover`, `tools/list`, `tools/call`; results carry `resultType` and `_meta` serverInfo.
  **Legacy** `2025-11-25` / `2025-06-18`: `initialize` starts a session for the process; `ping`,
  `tools/list`, `tools/call`. Older versions get `2025-11-25` offered back (2025-03-26 would require
  JSON-RPC batches, which this server rejects).
- Protocol errors (-32700, -32600, -32601, -32602, -32022) are for malformed messages, unknown
  methods and unknown tools. Everything the model can fix (bad arguments, engine and device errors) is a
  tool result with `isError: true` and the engine envelope as `structuredContent`.
- stdout carries protocol messages only: `main` keeps a private descriptor for them and points fd 1 at
  stderr. stderr gets only rare diagnostics, never request content or secrets.

## Rules

1. **The engine owns semantics; forti-api owns FortiOS.** This repo maps MCP to engine requests and
   back. Never construct URLs, call the SDK, or reimplement engine validation, policy or modes here. If
   the engine needs a change, ask the orchestrator; do not patch the forti-cli subproject.
2. **Tool schemas mirror the engine requests** (architecture.md "Requests"), strictly
   (`additionalProperties: false`). `forti_apply` takes exactly one of `plan_id` or `operations`
   (+ `profile`, `vdom`: the plan request, planned then applied). Change them only with the engine
   contract.
3. **Only a person approves, and only in answer to our prompt.** There is no approve tool and none
   may be added; the server never changes a profile's mode. forti-mcp records an approval
   (`engine::approve_plan`, `approved_by: mcp:<client>:elicitation`) only from a person's elicitation
   response, action `accept` with `content.approve === true`, to the prompt it showed, and only for
   the plan digest it showed. Never from tool arguments or anything else the model can produce.
   Without form elicitation, `approval_required` must reach the user with `forti approve <id>`.
4. **No live firewall in tests or development.** Tests use the fake transport and temporary config
   directories. Never touch the user's real config, secret store or AI-client configuration.
5. **Secrets never leak**: not in tool results, logs, errors or client configuration.
6. **Every result is bounded** by `--max-bytes` (default 48 KiB of envelope JSON), and is always
   complete JSON.
7. Match the style: 4-space indent, `snake_case`, comments that explain *why*, no framework where a
   function will do (no MCP SDK, no JSON-Schema library). Keep `-Wall -Wextra -Wpedantic`
   (warning_level=3) clean on Apple clang and GCC 13+.
