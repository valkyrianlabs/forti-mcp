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

:::badges{align="left" gap="sm"}
::badge[Release]{
  type="github"
  target="release"
  repo="valkyrianlabs/forti-mcp"
  href="https://github.com/valkyrianlabs/forti-mcp/releases"
}
::badge[CI]{
  type="github"
  target="workflow"
  repo="valkyrianlabs/forti-mcp"
  workflow="ci.yml"
  href="https://github.com/valkyrianlabs/forti-mcp/actions/workflows/ci.yml"
}
::badge[License]{
  type="github"
  target="license"
  repo="valkyrianlabs/forti-mcp"
}
:::

```bash
forti configure                          # a device profile in ~/.forti
forti mcp install --scope workspace      # the forti workspace: ~/.forti
cd ~/.forti && claude                    # or: cd ~/.forti && codex
```

Everything lives in one directory, the **forti home** (`~/.forti`, or `$FORTI_HOME`): device
profiles, file-stored credentials, client certificates, plans and the audit log. It is also the workspace where AI
agents get the firewall tools. An agent started in any other project does not see them.

:::buttons{align="left" stack="mobile" gap="md"}
::button[Install]{href="/getting-started/installation" variant="primary"}
::button[Set up the workspace]{href="/getting-started/connecting-clients" variant="secondary"}
::button[GitHub]{href="https://github.com/valkyrianlabs/forti-mcp" variant="outline" newTab=true}
:::

## What you get

:::cards{
  columns="3"
  cardTheme="muted"
}

:::card[Native and on demand]{eyebrow="Runtime"}
One executable that the AI client starts over stdio when it needs it and that exits when the client
closes it. No daemon, no network listener, no background service.
:::

:::card[Four tools]{eyebrow="Tools"}
`forti_discover` (profiles, device identity and mode, resource search, attributes), `forti_read`
(configuration and monitor data), `forti_plan` (previews a change as a stored plan) and
`forti_apply` (makes a change in one call, or applies a stored plan).
:::

:::card[The operator decides what may change]{eyebrow="Modes"}
Each device profile has its own mode, `read-only` (the default), `guarded` or `autonomous`, set in
`forti configure`. One server reaches every FortiGate in the forti home. In guarded mode you approve
each change in the conversation, or with `forti approve` in a terminal; nothing the model sends can
change a mode or approve a plan.
:::

:::card[Bounded results]{eyebrow="Paging"}
Every tool result fits a byte budget (48 KiB of JSON by default) and is always complete JSON; when a
listing is cut, the result says where to continue.
:::

:::card[Secrets stay out of the conversation]{eyebrow="Security"}
Device secrets in read results come back as `"<redacted>"`, API tokens never appear in the client's
configuration, and the workspace's deny rules keep the agent out of `~/.forti/credentials/`.
:::

:::card[Both protocol eras]{eyebrow="Protocol"}
MCP `2026-07-28` (stateless) and the `initialize`-based `2025-11-25` and `2025-06-18` revisions.
:::

:::

## How it relates to forti-cli

forti-mcp compiles in the command engine of [forti-cli](https://github.com/valkyrianlabs/forti-cli),
the `forti` command-line tool, which is built on the
[forti-api](https://github.com/valkyrianlabs/forti-api) SDK. The engine owns the semantics (resource
catalog, validation, plans, modes, approvals, audit); forti-mcp maps MCP tool calls to engine
requests and back. It never runs the `forti` executable, so the two packages install independently.

:::callout[Install forti as well]{variant="info"}
You still want `forti` next to forti-mcp. Device profiles and credentials are created with
`forti configure`, terminal approvals are given with `forti approve`, and
`forti mcp install` sets up the workspace for Claude Code and Codex. Both programs use the same forti
home (`~/.forti`), so a plan made in one can be inspected, approved or applied in the other.
:::

## Where to go next

:::cards{
  columns="2"
  cardTheme="glass"
}

:::card[Installation]{
  href="/getting-started/installation"
  eyebrow="Getting started"
}
Install forti-mcp from the APT repository or Homebrew, or build it from source.
:::

:::card[Connecting AI clients]{
  href="/getting-started/connecting-clients"
  eyebrow="Getting started"
}
Set up the `~/.forti` workspace with `forti mcp install`, or register forti-mcp by hand.
:::

:::card[The workflow: discover, read, plan, apply]{
  href="/guides/workflow"
  eyebrow="Guide"
}
How a model uses the four tools, learns a profile's mode and handles guarded approvals.
:::

:::card[Safety and modes]{
  href="/guides/safety-and-modes"
  eyebrow="Guide"
}
What each mode allows, what guarded mode does not stop, and the limits of recovery.
:::

:::card[Several FortiGates]{
  href="/guides/multiple-devices"
  eyebrow="Guide"
}
A profile per device, `--profile` and the per-request `profile` field.
:::

:::card[Security]{
  href="/reference/security"
  eyebrow="Reference"
}
Where secrets and certificates live, redaction, and what the server can never do.
:::

:::card[Tools reference]{
  href="/reference/tools"
  eyebrow="Reference"
}
The four tools, their input schemas, results and annotations.
:::

:::card[Protocol]{
  href="/reference/protocol"
  eyebrow="Reference"
}
Supported MCP versions, methods, error codes and the stdio transport rules.
:::

:::
