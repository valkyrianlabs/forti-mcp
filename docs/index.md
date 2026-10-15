---
title: forti-mcp
description: An MCP server for FortiGate firewalls, over the FortiOS REST API only.
order: 0
status: published
tags:
  - overview
---

# forti-mcp

forti-mcp is an [MCP](https://modelcontextprotocol.io) server for FortiGate firewalls. It lets an AI
client (Claude Code, Codex or any other MCP client) discover, read and change FortiOS configuration
through the **FortiOS REST API only**. It never runs SSH or CLI commands on the device.

```bash
claude mcp add --scope user forti -- forti-mcp     # or: forti install mcp
```

## What you get

- **Native and on demand**: one executable that the AI client starts over stdio when it needs it and
  that exits when the client closes it. No daemon, no network listener, no background service.
- **Four tools**: `forti_discover` (profiles, device identity and mode, resource search, attributes),
  `forti_read` (configuration and monitor data), `forti_plan` (validates a change and stores it as a
  plan) and `forti_apply` (executes a stored plan, by id only).
- **The operator decides what may change**: each device profile has a mode, `read-only` (the
  default), `guarded` or `autonomous`, set in `forti configure`. Nothing an MCP client sends can
  change a mode or approve a plan.
- **Bounded results**: every tool result fits a byte budget (48 KiB of JSON by default) and is always
  complete JSON; when a listing is cut, the result says where to continue.
- **Secrets stay out of the conversation**: device secrets in read results come back as
  `"<redacted>"`, and API tokens never appear in the client's configuration.
- **Both protocol eras**: MCP `2026-07-28` (stateless) and the `initialize`-based `2025-11-25` and
  `2025-06-18` revisions.

## How it relates to forti-cli

forti-mcp compiles in the command engine of [forti-cli](https://github.com/valkyrianlabs/forti-cli),
the `forti` command-line tool, which is built on the
[forti-api](https://github.com/valkyrianlabs/forti-api) SDK. The engine owns the semantics (resource
catalog, validation, plans, modes, approvals, audit); forti-mcp maps MCP tool calls to engine
requests and back. It never runs the `forti` executable, so the two packages install independently.

You still want `forti` next to it. Device profiles and credentials are created with
`forti configure`, guarded-mode approvals are given with `forti plan approve`, and `forti install mcp`
registers this server with Claude Code and Codex. Both programs read the same configuration
(`~/.config/forti`) and state, so a plan made in one can be inspected, approved or applied in the
other.

## Where to go next

- [Installation](/getting-started/installation)
- [Connecting AI clients](/getting-started/connecting-clients)
- [The workflow: discover, read, plan, apply](/guides/workflow)
- [Safety and modes](/guides/safety-and-modes)
- [Tools reference](/reference/tools)
