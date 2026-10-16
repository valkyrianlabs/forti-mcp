# forti-mcp release notes

<!-- vl-release:entry version=0.2.0 -->
## 0.2.0 — The ~/.forti workspace

_Released 2026-10-16_

forti-mcp now builds on the forti-cli 0.2.0 engine. Everything forti keeps lives in one directory,
the **forti home** (`~/.forti`, or `$FORTI_HOME`), and that directory is also the recommended place
to run your AI agent.

### One home for profiles, secrets, certificates and plans

forti-mcp reads its device profiles, file-stored credentials, client certificates, plans,
approvals and audit log from the forti home, the same one the `forti` CLI uses. `forti home` shows
every location.

**Upgrading from 0.1:** nothing breaks. An existing setup in `~/.config/forti` (with state in
`~/.local/state/forti`) keeps working as long as `~/.forti/config.json` does not exist. Run
`forti home migrate` (forti-cli 0.2.0) when you are ready to move it; Keychain items need no change.

### The workspace: firewall tools only where you want them

```bash
forti install mcp --scope workspace
cd ~/.forti && claude      # or: cd ~/.forti && codex
```

`forti install mcp --scope workspace` registers forti-mcp in the forti home only, so an agent started
there gets the four FortiGate tools and an agent started in any other project does not. The
workspace also keeps secrets out of the agent's context: Claude Code deny rules for `credentials/`,
certificate passwords and keys and edits to `config.json`, a Codex permission profile that denies the
same paths to sandboxed commands, and an `AGENTS.md` that tells the agent the rules. Codex reads the
workspace's configuration once it trusts `~/.forti`; `--trust-codex-workspace` adds that trust with
your consent. Registering the server by hand still works as before.

### Client certificates and several FortiGates

- Profiles made with `forti cert create` authenticate with a client certificate kept in
  `~/.forti/certs/<profile>/`, its password read from an owner-only file; forti-mcp uses them like
  any other profile.
- One forti-mcp serves every FortiGate in the forti home. The model names a device with the
  `profile` field of `forti_discover`, `forti_read` and `forti_plan`; `--profile` sets the default
  for a registration. Each profile keeps its own mode.

### Documentation

The docs are rewritten around the forti home: Connecting AI clients leads with the workspace,
Installation covers the home and migration, Security says where secrets and certificates live and
what the deny rules do, and a new guide, Several FortiGates, covers profiles per device.

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
