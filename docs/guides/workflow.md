---
title: "The workflow: discover, read, plan, apply"
navTitle: Workflow
description: How a model uses the four tools, learns a profile's mode and handles guarded approvals.
order: 30
status: published
tags:
  - guides
---

# The workflow: discover, read, plan, apply

The four tools are meant to be used in order. The server sends the same outline to the model as its
`instructions` (in the `initialize` result and in `server/discover`), so a model sees it before its
first call.

```text
forti_discover  ->  forti_read  ->  forti_plan  ->  forti_apply {plan_id}
  find paths         look at the      validate and     execute, as the
  and attributes     current state    store a change   profile's mode allows
```

The examples below show tool arguments. [Tools](/reference/tools) has every field.

:::toc[On this page]{depth="3" theme="compact"}
:::

## 1. Discover

```json
{"what": "devices"}
{"what": "capabilities", "profile": "home"}
{"what": "resources", "query": "addr", "kind": "table"}
{"what": "resource", "resource": "firewall/address", "fields": ["type", "subnet"]}
```

- `devices` lists the configured profiles with host, port and mode (never a secret), from the local
  configuration.
- `capabilities` connects to the device: hostname, model, serial, FortiOS version compared with the
  SDK's schema version, the profile's mode and the number of known resources.
- `resources` searches the compiled-in catalog of resource paths (configuration tables, singletons
  and typed monitor endpoints).
- `resource` describes one resource: its key, attributes, write operations and risk tier. `fields`
  gives full detail for selected attributes, and `live: true` adds the device's own help text,
  options and ranges.

:::callout[Discovery is mostly offline]{variant="note"}
Everything except `capabilities` and `live: true` is answered locally, without contacting the device.
:::

## 2. Read

```json
{"resource": "firewall/address", "filter": ["type==ipmask", "name=@lan"], "fields": ["name", "subnet"], "count": 100}
{"resource": "firewall/policy", "key": 12}
{"resource": "monitor/system/status"}
```

Narrow reads with `key` (one table object), `fields`, `filter` (FortiOS syntax; list items are
ANDed, a comma inside one item ORs) and `start`/`count`. Large results are cut to the byte budget and
say where to continue: [Large results and paging](/guides/large-results-and-paging). Device secrets
come back as `"<redacted>"` ([Security](/reference/security)).

## 3. Plan

```json
{"profile": "home", "operations": [
  {"op": "create", "resource": "firewall/address", "object": {"name": "printer", "subnet": "192.0.2.50 255.255.255.255"}},
  {"op": "update", "resource": "firewall/address", "key": "LAN", "set": {"comment": "managed"}, "clear": ["color"]}
]}
```

`forti_plan` validates every operation against the catalog and the SDK's attribute types, reads the
current objects, and stores the plan (owner-only, in forti's state directory). It never changes the
device. The result (`data`) carries:

| Field | Meaning |
|---|---|
| `id` | The plan id, which `forti_apply` takes as `plan_id`, e.g. `20261015-104512-a1b2c3` |
| `digest` | SHA-256 over the device identity, the operations and the before-state |
| `risk`, `summary` | The plan's risk tier (`low`, `elevated`, `critical`) and a one-line summary |
| `operations` | Per operation: the attribute diff and the exact request (`wire`) that apply will send, secrets redacted |
| `mode`, `notes` | The profile's mode and what apply will do in it |
| `approve` | Guarded mode only: `forti plan approve <id>` |

Operations run in order at apply time:

| `op` | Arguments |
|---|---|
| `create` | `resource`, `object` |
| `update` | `resource`, `key`, `set`, `clear` |
| `delete` | `resource`, `key` |
| `action` | `resource` (a `monitor/...` path), `body` |

## 4. Apply

```json
{"plan_id": "20261015-104512-a1b2c3"}
```

`forti_apply` takes only the plan id; the operations come from the stored plan, so a model cannot
apply something other than what was planned. What happens depends on the profile's mode.

| Mode | Result of `forti_apply` |
|---|---|
| `read-only` | `policy_denied`: nothing is sent |
| `guarded` | `approval_required` until a person approves the plan, then it executes |
| `autonomous` | Executes now |

### How a model learns the mode

A model never has to guess. The mode is in the `forti_discover` `devices` and `capabilities`
results, in every `forti_plan` result (`mode` and `notes`), and the server's instructions and the
`forti_apply` description explain all three outcomes. Nothing a model sends can change the mode: it
is set by the operator with `forti configure`.

### Guarded mode: `approval_required`

In a guarded profile, the first `forti_apply` returns a tool error (`isError: true`) whose
`structuredContent` is:

```json
{"ok": false, "error": {"code": "approval_required",
  "message": "plan 20261015-104512-a1b2c3 needs a human approval (guarded mode): forti plan approve 20261015-104512-a1b2c3",
  "details": {"plan_id": "20261015-104512-a1b2c3", "reason": "missing",
              "approve_command": "forti plan approve 20261015-104512-a1b2c3"}}}
```

The result also carries a second text block written for the model:

```text
Approval required (guarded mode). Ask the user to run this in their own terminal, then call forti_apply again with the same plan_id:
  forti plan approve 20261015-104512-a1b2c3
```

:::steps

#### The model relays the command

The model shows you the exact `forti plan approve <id>` command.

#### You approve in your own terminal

`forti plan approve` shows the plan (diff, request bodies, risk) and asks for confirmation.

#### The model applies again

The model calls `forti_apply` again with the same `plan_id`, and it executes.

:::

An approval:

- belongs to that plan's digest: a changed or tampered plan is not approved;
- expires after 24 hours (`reason: expired`);
- is used once (`reason: used`): applying again needs a new plan and a new approval.

:::callout[The approval is a person's act]{variant="warning"}
forti-mcp has no approve tool and never writes approvals. In guarded mode the approval is always a
person's act, outside MCP. Read what that does and does not guarantee in
[Safety and modes](/guides/safety-and-modes#what-guarded-mode-does-not-stop).
:::

### Autonomous mode and the safeguards of every apply

In an autonomous profile, `forti_apply` executes immediately. In every mode that writes, apply:

1. checks the plan's digest (tamper check);
2. re-reads every object the plan touches and checks the device's serial number, refusing with
   `stale_plan` if anything changed since planning (plan again);
3. checks that the audit log can be written, and refuses with `config` (nothing sent) if not;
4. for a critical-risk plan, first saves a configuration backup (owner-only, in forti's state
   directory) and refuses with `policy_denied` if it cannot, unless the profile sets
   `"backup_before_critical": false`;
5. runs the operations in order, never retrying a request that may have reached the device, and
   verifies each one by reading it back (monitor actions cannot be read back and are reported
   `applied_unverified`);
6. after each critical-risk operation, probes management connectivity with a fresh connection;
7. stops at the first failure;
8. writes a rollback plan and an audit record (`audit.jsonl`).

A successful result (`data.status: "applied"`) lists every operation's status (`verified`,
`applied_unverified`, ...), the number of verified and unverified operations, the rollback plan
(`data.rollback.plan_id`) and, for critical plans, the backup.

### When apply fails

| Code | What to do |
|---|---|
| `stale_plan` | The device, an object or the profile's host changed since planning: read again and make a new plan |
| `partial_failure` | Execution stopped at a failed operation. `details` has the per-operation record of what was applied |
| `outcome_unknown` | A request may have reached the device without a reply. Read the device before planning again |
| `management_connectivity_lost` | The probe after a critical change failed. The remaining operations were not attempted; `details` has the rollback plan, the backup and the before-state |
| `policy_denied` | The profile is read-only, the plan was already applied, or a critical plan's backup failed |

:::callout[Rolling back]{variant="tip"}
The rollback plan is an ordinary stored plan. Apply it like any other (`forti_apply` or
`forti apply <id>`), under the same mode rules. It needs a working management path; see
[Recovery aids are not guaranteed recovery](/guides/safety-and-modes#recovery-aids-are-not-guaranteed-recovery).
:::
