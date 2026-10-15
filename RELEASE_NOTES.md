# forti-mcp release notes

<!-- vl-release:entry version=0.1.1 -->
## 0.1.1 — Documentation in Payload Markdown

_Released 2026-10-15_

The forti-mcp documentation now uses Payload Markdown directives, so the published pages are easier
to scan and follow. The content is the same; only its presentation changed:

- **Installation** puts APT, Homebrew and the source build side by side in tabs.
- **Connecting AI clients** walks through setup as numbered steps, with Claude Code, Codex and
  manual registration in tabs.
- **Safety and modes** calls out what guarded mode does not stop and the limits of recovery after a
  management lockout.
- The overview page maps features and next pages as cards, and the reference pages have a table of
  contents and cleaner tables.

The server itself is unchanged.

<!-- vl-release:entry version=0.1.0 -->
## 0.1.0 — forti-mcp: FortiOS for AI clients

_Released 2026-10-15_

The first release of `forti-mcp`, an MCP server that lets an AI client (Claude Code, Codex or any
other MCP client) discover, read and change FortiGate configuration through the FortiOS REST API. It
never runs SSH or CLI commands.

- **Native and on demand.** The client starts `forti-mcp` over stdio and it exits with the client: no
  daemon, no network listener. It speaks MCP `2026-07-28` and the `initialize`-based `2025-11-25` and
  `2025-06-18` revisions.
- **Four tools.** `forti_discover` (profiles, device identity and mode, resource search, attributes),
  `forti_read` (configuration and monitor data, bounded and paginated), `forti_plan` (validates a
  change and records the diff, request bodies and risk tier) and `forti_apply` (executes a stored plan
  by id).
- **The operator decides what may change.** Each device profile's mode is set in `forti configure`
  and cannot be changed through MCP: `read-only` (default) refuses apply, `guarded` needs you to run
  `forti plan approve <id>`, `autonomous` applies with drift detection, verification, an audit record
  and a rollback plan.
- **Secrets stay out of the conversation.** Device secrets in read results come back as
  `"<redacted>"`, and API tokens never appear in client configuration.
- **The same engine as `forti`.** forti-mcp compiles in the command engine of
  [forti-cli](https://github.com/valkyrianlabs/forti-cli) and installs on its own;
  `forti-mcp --version` names the engine revision and forti-api version it was built from.

### Install

- Debian/Ubuntu (amd64), from the ValkyrianLabs APT repository: `sudo apt install forti-mcp`.
- macOS (Homebrew): `brew install valkyrianlabs/tap/forti-mcp` (builds from source).

Configure a device profile with `forti configure` and register the server with your AI client with
`forti install mcp` (both from forti-cli), or add the client entry by hand (README, "Setup").
