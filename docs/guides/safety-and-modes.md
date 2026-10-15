---
title: Safety and modes
description: What each mode allows, the safeguards of every change, what guarded mode does not stop, and the limits of recovery.
order: 40
status: published
tags:
  - guides
---

# Safety and modes

A profile's mode decides what `forti_apply` may do on that device. This page covers what each mode
allows, the safeguards every change goes through, what a mode cannot protect against, and what
recovery forti-mcp can and cannot offer.

:::toc[On this page]{depth="2" theme="compact"}
:::

## The FortiOS account is the boundary

The FortiGate API account that a profile uses is the primary authorization boundary. An API user can
do exactly what its admin profile, VDOM assignment and trusted hosts allow, whatever forti-mcp is
told. A profile's mode is the operator's policy on top of that boundary, not a replacement for it.

:::callout[Scope the API account first]{variant="tip"}
- Give the API user an admin profile that grants only what the agent should manage (for example
  read-write on firewall objects and policy, read-only on system), and avoid `super_admin`.
- Restrict its trusted hosts to the machine the agent runs on.
- Use a VDOM-scoped account when the agent should manage only one VDOM.
:::

## Modes

Each device profile has a mode, chosen with `forti configure` (or `forti profile mode`) and stored in
forti's `config.json`. forti-mcp reads it and cannot change it.

| Mode | `forti_plan` | `forti_apply` | Suits |
|---|---|---|---|
| `read-only` (default) | Yes: planning only reads, so it works for preview and validation | Refused: `policy_denied` | Monitoring, auditing and planning |
| `guarded` | Yes | Only after a person approves that exact plan with `forti plan approve <id>` | An agent that prepares and validates changes while a person approves each plan |
| `autonomous` | Yes | Immediately, with no interactive approval | A capable agent doing real administration |

:::callout[Autonomous mode has no interactive gate]{variant="warning"}
With `autonomous`, **the agent can do anything the FortiOS API account may do**, including changes
that cut off your own management access.
:::

:::callout[A second limit per registration]{variant="info"}
`forti-mcp --read-only` limits one client registration: the client is offered only
`forti_discover` and `forti_read`, and calls to the other two are refused, whatever the profile's
mode ([Command line](/reference/command-line)).
:::

## Safeguards in every mode that writes

Autonomous mode included, every `forti_apply`:

- runs only a stored plan, validated against the catalog and the SDK's types before anything is sent;
- refuses a plan whose objects, device or profile changed since it was made (`stale_plan`);
- runs operations in order without retries, stops at the first failure and verifies each change by
  reading it back;
- writes a rollback plan and an audit record, and refuses to start when the audit log cannot be
  written;
- for a critical-risk plan, takes a full configuration backup first and probes management
  connectivity after each critical operation.

### Risk tiers

Risk tiers are reported by `forti_discover` (`what: resource`) and `forti_plan`.

| Tier | Covers |
|---|---|
| `low` | Objects that do nothing unless referenced (addresses, services, schedules) |
| `elevated` | Policies, VIPs, users, VPN and most of `system/*` |
| `critical` | Management access, routing, interfaces, admins, HA, VDOMs, global settings, and the monitor actions that restart or reconfigure the device or cut a management path (reboot, configuration restore, firmware, factory reset, and similar) |

Tiers drive the backup and the connectivity probe; they do not add interactive gates.

:::callout[forti_plan is annotated read-only]{variant="note"}
`forti_plan` is annotated read-only (`readOnlyHint: true`) because it never changes the device: it
reads the device and writes a local plan file. Some clients run read-only tools without asking.
`forti_apply` is annotated destructive and not idempotent ([Tools](/reference/tools)).
:::

## What guarded mode does not stop

A mode governs what the **forti-mcp tools** can do. Nothing an MCP client sends can change a mode,
approve a plan or apply a plan in a read-only profile: there is no approve tool, and the server never
writes approvals.

:::callout[A mode is not a barrier against your own user account]{variant="warning"}
An agent that can also run shell commands as you (Claude Code's Bash tool, Codex's shell) could run
`forti plan approve` or `forti configure`, or edit `config.json` and the approval files, exactly as
you can. forti uses no passphrase or terminal-only approval, by design.
:::

For guarded mode to mean "a person approved this":

1. Keep the client's permission prompts on for `forti` commands, or deny them outright. In Claude
   Code's settings:

   ```json
   {"permissions": {"deny": ["Bash(forti plan approve:*)", "Bash(forti configure:*)", "Bash(forti profile:*)"]}}
   ```

2. Read the plan before you approve it. `forti plan approve` shows it first (`forti plan show <id>`
   shows it on its own).

:::callout[Guidance, not enforcement]{variant="note"}
The agent skill that `forti install mcp` can install tells agents never to approve plans or change
modes, and the server's instructions say the same. That is guidance, not enforcement. The FortiOS
account's privileges remain the boundary that holds in every case.
:::

## Recovery aids are not guaranteed recovery

Every apply writes a rollback plan (the inverse operations, from the recorded before-state), and
critical plans take a configuration backup first. Both need a working management path to the device.

:::callout[forti-mcp cannot undo a management lockout]{variant="danger"}
**If a change cuts off management access, forti-mcp cannot undo it.** When the connectivity probe
after a critical operation fails, apply stops with `management_connectivity_lost` and returns the
rollback plan id, the backup reference and the before-state, but applying the rollback needs the
same REST API path that was just lost.
:::

Recovering from a management lockout needs an independent path that the operator provides: the
console port, an out-of-band management interface, a separate admin account on another interface, or
a device-side automatic revert. Arrange one before letting an agent change interfaces, routing or
administrative access.

:::callout[Treat backups like credentials]{variant="warning"}
A configuration backup holds the device's complete configuration, including FortiOS-encrypted
secrets, most of them reversible. forti keeps the 20 most recent backups in its state directory
(owner-only) and never logs or prints them.
:::
