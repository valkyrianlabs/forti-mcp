---
title: Connecting AI clients
description: Set up the ~/.forti workspace with forti install mcp, or register forti-mcp by hand with Claude Code, Codex or another MCP client.
order: 20
status: published
tags:
  - getting-started
---

# Connecting AI clients

The recommended setup is the **forti workspace**: forti-mcp registered in the forti home, `~/.forti`,
and nowhere else. Start your agent there (`cd ~/.forti && claude`, or `codex`) and it gets the four
FortiGate tools; start it in any other project and it does not. The client starts `forti-mcp`
itself; there is nothing to run in the background.

:::toc[On this page]{depth="3" theme="compact"}
:::

## Set up the workspace

:::steps{
  variant="cards"
  layout="stack"
  numbered
}

### Configure a device profile

forti-mcp reads its device profiles from the same forti home as the `forti` CLI. Create one with:

```bash
forti configure
```

It creates `~/.forti` (mode 0700) on first use and asks for the FortiGate's host and port, the REST
API token (stored in the macOS Keychain or in an owner-only file under `~/.forti/credentials/`,
never in `config.json`) and the profile's **mode**: `read-only` (the default), `guarded` or
`autonomous`. The mode decides what `forti_apply` may do; see
[Safety and modes](/guides/safety-and-modes) before choosing anything other than `read-only`. Repeat
it for each FortiGate ([Several FortiGates](/guides/multiple-devices)).

### Install the workspace

```bash
forti install mcp --scope workspace
```

Run interactively, `forti install mcp` offers the workspace scope first, asks which clients to set
up, prints every command it will run and every file it will write, and changes nothing until you
confirm. Without prompts:

```bash
forti install mcp --client all --scope workspace --trust-codex-workspace --yes
forti install mcp --client claude --scope workspace --dry-run      # show the changes, make none
```

### Start the agent in the workspace

```bash
cd ~/.forti && claude      # or: cd ~/.forti && codex
```

Claude Code asks once to approve the project server from `.mcp.json`. Codex loads the workspace's
`.codex/config.toml` only once it trusts the directory ([Codex trust](#codex-trust)).

### Check the connection

Ask the agent to list the configured devices. It should call `forti_discover` with `what: devices`,
which answers from the local configuration without contacting a FortiGate. `what: capabilities`
then connects to the device and reports its identity, FortiOS version and the profile's mode.

```bash
forti mcp status      # where forti-mcp is registered, and the workspace's health
```

:::

## What the workspace contains

A workspace install registers forti-mcp as a project server of the forti home and adds a small
scaffold next to your configuration:

| File in `~/.forti` | What it is |
|---|---|
| `.mcp.json` | Claude Code's project registration: the `forti` server, the absolute path of `forti-mcp` |
| `.codex/config.toml` | Codex's project registration (`[mcp_servers.forti]`) and the `forti-workspace` permission profile |
| `.claude/skills/forti/`, `.agents/skills/forti/` | The forti agent skill, for Claude Code and Codex |
| `AGENTS.md`, `CLAUDE.md` | What the directory is and the rules an agent follows there |
| `.claude/settings.json` | Claude Code `permissions.deny` rules, merged into any settings already there |
| `.forti-workspace.json` | What forti wrote, so a later install never overwrites a file you edited and uninstall removes only forti's files |

The server entry holds the command and its options, never a secret and no `env`. `config.json`,
`credentials/`, `certs/`, `state/` and `cache/` are never touched by the install, and
`forti mcp uninstall --scope workspace` never removes them.

### Why the tools stay out of other projects

A server registered for every project (user scope) is offered to every agent you start, including
one working on unrelated code that has never heard of your firewall. A workspace registration is a
project registration of `~/.forti` only, so the firewall tools exist exactly where you chose to work
on firewalls. That is also where the profiles, plans and audit log are, and where `AGENTS.md`
tells the agent how to behave.

:::callout[Not your home directory]{variant="warning"}
A project or local registration in `$HOME` itself would give the tools to every agent started
anywhere under it. `forti install mcp` warns and asks "Are you sure?" (default no), and without a
terminal refuses unless `--force` is given. Use the workspace instead.
:::

### Keeping secrets out of the agent's context

forti-mcp itself reads the credentials; the agent never needs to. The workspace keeps the agent's
own file tools away from them:

- **Claude Code**: `.claude/settings.json` denies `Read(/credentials/**)`,
  `Read(/certs/**/*.pass)`, `Read(/certs/**/*.p12)`, `Read(/certs/**/ca/**)` and
  `Edit(/config.json)`. A leading `/` anchors each rule to the workspace.
- **Codex**: `.codex/config.toml` sets `default_permissions = "forti-workspace"`, a permission
  profile that extends `:workspace`, denies `credentials`, `certs/**/*.pass`, `certs/**/*.p12` and
  `certs/**/ca/**` to sandboxed commands, and makes `config.json` read-only.
- **Both**: `AGENTS.md` tells the agent never to read `credentials/`, `certs/`, `config.json` or
  `state/`, and never to run `forti plan approve`, `forti configure`, `forti profile` or
  `forti cert`.

:::callout[They stop accidents, not a determined process]{variant="note"}
The deny rules govern the agent's file tools and sandboxed commands, not forti-mcp, which runs
outside the sandbox and needs the credentials. An agent allowed to run arbitrary shell commands as
you can still do what you can do ([Security](/reference/security)).
:::

### Codex trust

Codex reads a project's `.codex/config.toml`, and with it both the `forti` server and the
`forti-workspace` permission profile, only for projects it trusts. Either start `codex` in
`~/.forti` and accept its trust prompt, or let forti add the trust entry:

```bash
forti install mcp --client codex --scope workspace --trust-codex-workspace
```

That adds `[projects."<forti home>"] trust_level = "trusted"` to the global Codex configuration
(`~/.codex/config.toml`). Interactively, forti asks first (default no). It never overrides a trust
entry you wrote yourself, and uninstall removes the entry only if forti added it. Restart Codex
after a change to its configuration.

## Other scopes

`forti install mcp` also writes the other scopes, for when the workspace does not fit:

```bash
forti install mcp --client all --scope project --project-dir ~/src/netops   # a repository to commit
forti install mcp --client claude --scope user --yes                         # every project
forti mcp repair                                    # fix entries whose forti-mcp moved or vanished
forti mcp uninstall --client codex --scope user --yes
```

`project` writes `.mcp.json` and `.codex/config.toml` in `--project-dir`, `user` registers the
server for every project, and `local` (Claude Code only) is this project, for you alone. Every scope
registers the server under the name `forti` with the absolute path of `forti-mcp`, no arguments
(or `--profile NAME` when you pass `--profile`) and no environment. The
[forti-cli documentation](https://github.com/valkyrianlabs/forti-cli) describes every scope, the
agent skill and how conflicts are handled.

## Registering by hand

Without forti-cli, or to keep control of the entry yourself, register the server with the client's
own command or configuration file. To get the workspace's effect by hand, run these in `~/.forti`
with the project scope.

:::tabs{
  default="claude-code"
  theme="glass"
  tabTheme="muted"
}

:::tab[Claude Code]{value="claude-code"}
```bash
cd ~/.forti && claude mcp add --scope project forti -- forti-mcp
```

`--scope` is `local` (the default: this project, stored in `~/.claude.json`), `user` (every
project) or `project` (`.mcp.json` in the project root). `claude mcp add` refuses a name that
already exists; replace an entry with `claude mcp remove` and then `add`.

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

This always writes the global `~/.codex/config.toml` (`$CODEX_HOME/config.toml`), which every
project sees, and silently replaces an existing `forti` entry. For a project entry, write the table
into the project's `.codex/config.toml` yourself:

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

A hand-written registration gets no deny rules, permission profile or `AGENTS.md`; add your own if
the agent works in a directory that holds forti's files.

## Options in the client entry

[Command-line options](/reference/command-line) go in the entry's arguments:

```bash
forti install mcp --client claude --scope workspace --profile branch
claude mcp add --scope user forti -- forti-mcp --read-only
codex mcp add forti -- forti-mcp --profile branch --max-bytes 65536
```

| Option | Effect |
|---|---|
| `--read-only` | Offers only `forti_discover` and `forti_read`, whatever the profile's mode |
| `--profile NAME` | Picks the profile used when a request names none ([Several FortiGates](/guides/multiple-devices)) |
| `--max-bytes N` | Sets the JSON budget per tool result ([Large results and paging](/guides/large-results-and-paging)) |

## No secrets in client configuration

The entry needs no `env`. forti-mcp finds the forti home itself (`~/.forti`, or `$FORTI_HOME`) and
reads its profiles (`config.json`) and its secret store (the macOS Keychain, or
`~/.forti/credentials/`). The entry holds the command, its options and nothing else.

:::callout[Never put an API token in a client's configuration]{variant="danger"}
MCP client configuration files are read by other tools, synced and, for project scopes, committed.
Keep the token in forti's secret store.
:::

A profile whose key comes from an environment variable (the `env` key source) reads it from
forti-mcp's own environment, which the client controls. For MCP use, prefer the secret store.
