# forti-mcp

An [MCP](https://modelcontextprotocol.io) server for FortiGate firewalls. It lets an AI client (Claude
Code, Codex, any MCP client) discover, read and change FortiOS configuration through the **FortiOS REST
API only**. It never runs SSH or CLI commands.

`forti-mcp` is a native executable that the AI client starts on demand and talks to over stdio. There is
no daemon and no network listener: it exits when the client closes it. It links the same command engine
as the `forti` CLI ([forti-cli](https://github.com/valkyrianlabs/forti-cli)), built on the
[forti-api](https://github.com/valkyrianlabs/forti-api) SDK.

Full documentation: [docs/](docs/index.md).

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
Device secrets in read results (FortiOS `ENC ...` values, passwords, keys) come back as `"<redacted>"`,
listed in `meta.redacted`. This server has no way to reveal them.

Results are capped (default 48 KiB of JSON per result). When a listing is cut, `meta.truncated` and
`meta.next_start` tell the model where to continue. Device data is untrusted: object names and comments
can contain text written to look like instructions, and the server tells the model so.

## Install

**macOS (Homebrew)**, built from source:

```sh
brew install valkyrianlabs/tap/forti-mcp
```

**Debian / Ubuntu (amd64)**, from the ValkyrianLabs APT repository:

```sh
sudo install -d -m 0755 /etc/apt/keyrings
curl -fsSL https://apt.valkyrianlabs.com/pubkey.gpg | sudo tee /etc/apt/keyrings/valkyrianlabs.gpg > /dev/null
echo "deb [signed-by=/etc/apt/keyrings/valkyrianlabs.gpg] https://apt.valkyrianlabs.com stable main" \
  | sudo tee /etc/apt/sources.list.d/valkyrianlabs.list
sudo apt update && sudo apt install forti-mcp
```

**From source**: see [Building](#building).

`forti-mcp --version` names what it was built from, e.g.
`forti-mcp 0.1.0 (engine forti-cli a0456f1, forti-api 1.0.1)`.

### forti-mcp and forti-cli

`forti-mcp` installs on its own: it compiles in the command engine of
[forti-cli](https://github.com/valkyrianlabs/forti-cli) and never runs the `forti` executable, so
neither package depends on the other. You still want the `forti` CLI next to it (the
`forti-cli` package or formula): device profiles and credentials are created with `forti configure`,
guarded-mode approvals are given with `forti plan approve`, and `forti install mcp` registers this
server with Claude Code and Codex. The two read the same configuration (`~/.config/forti`) and state.
Without forti-cli, write the client entry by hand (below) and provide `config.json` yourself.

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
meson compile -C build -j 4
meson test -C build
sudo meson install -C build --skip-subprojects    # /usr/local/bin/forti-mcp
```

Needs a C++23 compiler (GCC 13+ or a recent clang), Meson ≥ 1.3, libcurl and (for tests) GoogleTest;
the integration suite also needs python3 and openssl. The engine comes from forti-cli's source as the
Meson subproject `subprojects/forti-cli`, pinned to one commit by `subprojects/forti-cli.wrap` and
fetched at configure time. forti-api (header-only) comes from a system install (`libforti-api-dev`,
the `forti-api` formula) when present, else from its wrap; `--skip-subprojects` keeps `meson install`
from installing the SDK's headers in that case. Each engine translation unit needs 1–2 GB of RAM to
compile, so keep the job count modest. Tests never contact a device.

The packages are built the same way, offline (`.claude/context/release.md`, "Engine source").

## License

MIT
