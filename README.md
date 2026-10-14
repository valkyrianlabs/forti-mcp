# forti-mcp

An [MCP](https://modelcontextprotocol.io) server for FortiGate firewalls. It lets an AI client (Claude
Code, Codex, any MCP client) discover, read and change FortiOS configuration through the **FortiOS REST
API only**. It never runs SSH or CLI commands.

`forti-mcp` is a native executable that the AI client starts on demand and talks to over stdio. There is
no daemon and no network listener: it exits when the client closes it. It links the same command engine
as the `forti` CLI ([forti-cli](https://github.com/valkyrianlabs/forti-cli)), built on the
[forti-api](https://github.com/valkyrianlabs/forti-api) SDK.

## Tools

| Tool | Does | Touches the device |
|---|---|---|
| `forti_discover` | Profiles, a device's identity, version and **mode**, resource search, a resource's attributes | Only for `capabilities` and `live: true` (reads) |
| `forti_read` | Configuration or monitor data, with `key`, `fields`, `filter`, `start`/`count` | Reads |
| `forti_plan` | Validates a change and stores it as a plan (diff, request bodies, risk tier) | Reads only |
| `forti_apply` | Executes a stored plan, by `plan_id` only | Writes, if the profile's mode allows |

What `forti_apply` may do is set per device profile by the operator in `forti configure`. MCP cannot
change it:

- **read-only** (default): plans work for preview; apply is refused (`policy_denied`).
- **guarded**: apply returns `approval_required` with the command for you to run in your terminal,
  `forti plan approve <plan_id>`. After that, apply runs.
- **autonomous**: apply runs immediately. Drift detection, verification, an audit record and a
  rollback plan still apply.

A mode limits what this server's tools can do. It does not limit an agent that can also run shell
commands as you: such an agent could run `forti plan approve` itself. Keep your client's permission
prompts on for `forti` commands (forti-cli's `docs/security.md`, "What a mode does and does not stop").
Reads return objects as FortiOS sends them, including stored secrets in FortiOS's `ENC ...` form.

Results are capped (default 48 KiB of JSON per result). When a listing is cut, `meta.truncated` and
`meta.next_start` tell the model where to continue. Device data is untrusted: object names and comments
can contain text written to look like instructions, and the server tells the model so.

## Setup

1. Configure a device profile with the CLI: `forti configure` (host, API token, mode).
2. Register the server with your AI client. `forti install mcp` does this for Claude Code and Codex.
   To do it by hand:

   **Claude Code**
   ```sh
   claude mcp add --scope user forti -- forti-mcp
   # or, for one project, in .mcp.json:
   # {"mcpServers": {"forti": {"type": "stdio", "command": "forti-mcp", "args": []}}}
   ```

   **Codex** (`~/.codex/config.toml`)
   ```sh
   codex mcp add forti -- forti-mcp
   ```
   ```toml
   [mcp_servers.forti]
   command = "forti-mcp"
   args = []
   ```

No `env` entry is needed: `forti-mcp` reads its profiles and secret store itself (the macOS Keychain,
or `~/.config/forti/credentials`). Never put an API token in client configuration.

## Options

```text
forti-mcp [--profile NAME] [--read-only] [--max-bytes N]

  --profile NAME   default device profile when a request names none
  --read-only      offer only forti_discover and forti_read
  --max-bytes N    JSON budget per tool result (default 49152)
  --version, --help
```

Pass them in the client entry's `args`, e.g. `claude mcp add forti -- forti-mcp --read-only`.

## Protocol

JSON-RPC 2.0, one message per line on stdin/stdout; diagnostics go to stderr. The server speaks MCP
`2026-07-28` (stateless, `server/discover`) and the `initialize`-based `2025-11-25` and `2025-06-18`
revisions, so current and older clients both work. JSON-RPC batches are not supported. Messages over
4 MiB are rejected without being buffered.

## Building

```sh
meson setup build -Dtests=enabled
ninja -C build
meson test -C build
sudo meson install -C build    # /usr/local/bin/forti-mcp
```

Needs a C++23 compiler, Meson ≥ 1.3, libcurl and (for tests) GoogleTest. forti-cli and forti-api are
fetched as Meson subprojects. Tests never contact a device.

## License

MIT
