---
title: Connecting AI clients
description: Configure a device profile and register forti-mcp with Claude Code, Codex or another MCP client.
order: 20
status: published
tags:
  - getting-started
---

# Connecting AI clients

Configure a device profile, register forti-mcp with your AI client, and check that the client can
see your devices. The client starts `forti-mcp` itself; there is nothing to run in the background.

:::toc[On this page]{depth="3" theme="compact"}
:::

## Set up a client

:::steps{
  variant="cards"
  layout="stack"
  numbered
}

### Configure a device profile

forti-mcp reads its device profiles from the same configuration as the `forti` CLI. Create one with:

```bash
forti configure
```

It asks for the FortiGate's host and port, the REST API token (stored in the macOS Keychain or in an
owner-only credentials file, never in `config.json`) and the profile's **mode**: `read-only` (the
default), `guarded` or `autonomous`. The mode decides what `forti_apply` may do; see
[Safety and modes](/guides/safety-and-modes) before choosing anything other than `read-only`.

### Register the server

`forti install mcp` (from forti-cli) registers forti-mcp with Claude Code and Codex. It is the
recommended way:

```bash
forti install mcp                                      # interactive: pick clients and scope, confirm
forti install mcp --client claude --scope user --yes
forti mcp status                                       # where it is registered, and is it still right?
forti mcp repair                                       # fix entries whose forti-mcp moved or vanished
forti mcp uninstall --client codex --scope user --yes
```

The server is registered under the name `forti` with the absolute path of `forti-mcp`, no arguments
(or `--profile NAME` when you pass `--profile`) and no environment. Run interactively, forti prints
every command it will run and every file it will write, and changes nothing until you confirm.
Claude Code picks up the server in new sessions; restart Codex after a change to its configuration.
forti-cli's `docs/agent-integration.md` describes every scope, the optional agent skill and how
conflicts are handled.

Without forti-cli, or to keep control of the entry yourself, [register by hand](#registering-by-hand)
instead.

### Check the connection

Ask the agent to list the configured devices. It should call `forti_discover` with `what: devices`,
which answers from the local configuration without contacting a FortiGate. `what: capabilities`
then connects to the device and reports its identity, FortiOS version and the profile's mode.

:::

## Registering by hand

Register the server directly with the client's own command or configuration file.

:::tabs{
  default="claude-code"
  theme="glass"
  tabTheme="muted"
}

:::tab[Claude Code]{value="claude-code"}
```bash
claude mcp add --scope user forti -- forti-mcp
```

`--scope` is `local` (the default: this project, stored in `~/.claude.json`), `user` (every
project) or `project` (`.mcp.json` in the project root, meant to be committed). `claude mcp add`
refuses a name that already exists; replace an entry with `claude mcp remove` and then `add`.

A project entry in `.mcp.json` looks like this:

```json
{"mcpServers": {"forti": {"type": "stdio", "command": "forti-mcp", "args": []}}}
```

Claude Code asks each person to approve a project server from `.mcp.json` once.

:::callout[Do not edit ~/.claude.json by hand]{variant="warning"}
Use the CLI for the local and user scopes and never edit `~/.claude.json` by hand: running sessions
rewrite it.
:::
:::

:::tab[Codex]{value="codex"}
```bash
codex mcp add forti -- forti-mcp
```

This always writes the global `~/.codex/config.toml` (`$CODEX_HOME/config.toml`) and silently
replaces an existing `forti` entry:

```toml
[mcp_servers.forti]
command = "forti-mcp"
args = []
```

A project's `.codex/config.toml` is loaded only for projects Codex trusts. Restart Codex after
changing its configuration.
:::

:::tab[Other MCP clients]{value="manual"}
Any client that launches stdio servers works: the command is `forti-mcp`, with the
[options below](#options-in-the-client-entry) as arguments. The entry needs no `env`.

forti-mcp speaks MCP `2026-07-28` and the `initialize`-based `2025-11-25` and `2025-06-18`
revisions ([Protocol](/reference/protocol)).
:::

:::

## Options in the client entry

[Command-line options](/reference/command-line) go in the entry's arguments:

```bash
claude mcp add --scope user forti -- forti-mcp --read-only
codex mcp add forti -- forti-mcp --profile branch --max-bytes 65536
```

| Option | Effect |
|---|---|
| `--read-only` | Offers only `forti_discover` and `forti_read`, whatever the profile's mode |
| `--profile NAME` | Picks the profile used when a request names none |
| `--max-bytes N` | Sets the JSON budget per tool result ([Large results and paging](/guides/large-results-and-paging)) |

## No secrets in client configuration

The entry needs no `env`. forti-mcp reads its profiles (`~/.config/forti/config.json`) and its secret
store (the macOS Keychain, or `~/.config/forti/credentials/`) itself. The entry holds the command,
its options and nothing else.

:::callout[Never put an API token in a client's configuration]{variant="danger"}
MCP client configuration files are read by other tools, synced and, for project scopes, committed.
Keep the token in forti's secret store.
:::

A profile whose key comes from an environment variable (the `env` key source) reads it from
forti-mcp's own environment, which the client controls. For MCP use, prefer the secret store.
