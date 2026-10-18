![forti-mcp](https://media.valkyrianlabs.com/forti-mcp.png)

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
| `forti_plan` | Previews a change: validates it and stores it as a plan (diff, request bodies, risk tier) | Reads only |
| `forti_apply` | Makes a change: `operations` (plans and applies in one call) or a stored `plan_id` | Writes, if the profile's mode allows |

What `forti_apply` may do is set per device profile by the operator in `forti configure`. MCP cannot
change it:

- **read-only** (default): plans work for preview; apply is refused (`policy_denied`).
- **guarded**: apply asks you to approve the exact change. When the client supports MCP elicitation
  (Claude Code, Codex), you get one prompt in the conversation showing the device, mode, risk and
  every attribute change; check **Apply these changes** and it runs in the same call. Otherwise apply
  returns `approval_required` with `forti approve <plan_id>` for your terminal (or keep
  `forti approve --watch` running), and the model applies again after you approve.
- **autonomous**: apply runs immediately. Drift detection, verification, an audit record and a
  rollback plan still apply.

A mode limits what this server's tools can do. It does not limit an agent that can also run shell
commands as you: such an agent could run `forti approve` itself. Keep your client's permission
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

The packages recommend `valkyrianlabs-archive-keyring` (APT installs it by default), which puts the repository key at
`/usr/share/keyrings/valkyrianlabs.gpg` and keeps it current through normal upgrades (for example when its
expiry changes). When the key downloaded above is unmodified, the package replaces it with a symlink to the
packaged key, so the source line above keeps working and you never need to download the key again. Removing
the package turns the symlink back into a plain copy. Signing key fingerprint:
`494D 71A1 EEDD F87B D48E  5048 6D64 CB7B DFC3 0EC6`.

**From source**: see [Building](#building).

`forti-mcp --version` names what it was built from, e.g.
`forti-mcp 0.4.1 (engine forti-cli 468f9e1, forti-api 1.1.1)`.

### forti-mcp and forti-cli

`forti-mcp` installs on its own: it compiles in the command engine of
[forti-cli](https://github.com/valkyrianlabs/forti-cli) and never runs the `forti` executable, so
neither package depends on the other. You still want the `forti` CLI next to it (the
`forti-cli` package or formula): device profiles and credentials are created with `forti configure`,
terminal approvals are given with `forti approve`, and `forti mcp install` sets up the agent
workspace for Claude Code and Codex. The two use the same forti home, `~/.forti` (or `$FORTI_HOME`):
profiles, file-stored credentials, certificates, plans and the audit log. A pre-0.2 setup in
`~/.config/forti` keeps working until `forti home migrate` moves it.
Without forti-cli, write the client entry by hand (below) and provide `config.json` yourself.

## Setup

1. Configure a device profile with the CLI: `forti configure` (host, API token, mode). Repeat it
   for each FortiGate; the model picks one per request with the `profile` field.
2. Set up the forti workspace (recommended): forti-mcp registered in `~/.forti` only, with deny
   rules and a Codex permission profile that keep credentials out of the agent's context.
   ```sh
   forti mcp install --scope workspace      # add --trust-codex-workspace for Codex
   cd ~/.forti && claude                    # or: cd ~/.forti && codex
   ```
   Agents started in any other project do not get the firewall tools.
3. Or register the server by hand:

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
or `~/.forti/credentials`). Never put an API token in client configuration.

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
from installing the SDK's headers in that case. `ci/build-jobs` picks a job count your memory
allows (about 1.2 GB per job with GCC). Tests never contact a device.

The packages are built the same way, offline (`.claude/context/release.md`, "Engine source").

## License

MIT
