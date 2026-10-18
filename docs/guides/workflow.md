---
title: "The workflow: discover, read, plan, apply"
navTitle: Workflow
description: How a model uses the four tools, applies a change in one call, and gets a person's approval in guarded mode.
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
forti_discover  ->  forti_read  ->  forti_apply {operations}
  find paths         look at the      plan, validate and apply in one call,
  and attributes     current state    as the profile's mode allows

                     forti_plan {operations}  ->  forti_apply {plan_id}
                     (only to preview a change first)
```

A routine change is one `forti_apply` call with the operations. `forti_plan` is for when you want to
see a change before deciding: it stores the plan without applying it, and `forti_apply {plan_id}`
applies it later.

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
current objects, and stores the plan (owner-only, in `~/.forti/state/`). It never changes the
device. The result (`data`) carries:

| Field | Meaning |
|---|---|
| `id` | The plan id, which `forti_apply` takes as `plan_id`, e.g. `20261015-104512-a1b2c3` |
| `digest` | SHA-256 over the device identity, the operations and the before-state |
| `risk`, `summary` | The plan's risk tier (`low`, `elevated`, `critical`) and a one-line summary |
| `operations` | Per operation: the attribute diff and the exact request (`wire`) that apply will send, secrets redacted |
| `mode`, `notes` | The profile's mode and what apply will do in it |
| `approve` | Guarded mode only: `forti approve <id>`, for approving in a terminal |

Operations run in order at apply time:

| `op` | Arguments |
|---|---|
| `create` | `resource`, `object` |
| `update` | `resource`, `key`, `set`, `clear` |
| `delete` | `resource`, `key` |
| `action` | `resource` (a `monitor/...` path), `body` |

## 4. Apply

Either the operations themselves (planned and applied in one call):

```json
{"profile": "home", "operations": [
  {"op": "create", "resource": "firewall/address", "object": {"name": "printer", "subnet": "192.0.2.50 255.255.255.255"}}
]}
```

or a plan `forti_plan` stored:

```json
{"plan_id": "20261015-104512-a1b2c3"}
```

Pass exactly one of `operations` and `plan_id`. With `plan_id` the operations come from the stored
plan, so a model cannot apply something other than what was planned; `profile` and `vdom` go only
with `operations`. What happens depends on the profile's mode.

| Mode | Result of `forti_apply` |
|---|---|
| `read-only` | `policy_denied`, with the command an operator would run to allow changes. Nothing is sent |
| `guarded` | Applied once a person approves this exact plan: in the conversation when the client supports it, otherwise with `forti approve <id>` in a terminal |
| `autonomous` | Applied now |

The result has the apply record and, for an `operations` call, the plan (`data.plan`: id, summary,
risk, digest and each operation's diff), so the model can report exactly what changed.

### How a model learns the mode

A model never has to guess. The mode is in the `forti_discover` `devices` and `capabilities`
results, in every `forti_plan` result (`mode` and `notes`), and the server's instructions and the
`forti_apply` description explain all three outcomes. Nothing a model sends can change the mode: it
is set by the operator with `forti configure`.

### Guarded mode: approval in the conversation

When the client supports MCP form elicitation (Claude Code and Codex do), `forti_apply` asks you
directly, during the call. You see one prompt with the proposed change:

```text
Apply these changes to FortiGate home?

Device: home (192.0.2.1, serial FGT60FTK20000000, vdom root)
Mode: guarded · Risk: low · 1 operation

1. create firewall/address "printer"
   name = printer
   subnet = 192.0.2.50 255.255.255.255

Plan 20261015-104512-a1b2c3
```

and one checkbox, **Apply these changes**, unchecked by default.

:::steps

#### You ask for a change

"Add an address object for the printer." The model calls `forti_apply` with the operations.

#### You approve once

The prompt shows the device, the mode, the risk and every attribute change (secrets stay
`<redacted>`). Check the box and accept to apply; decline or cancel to apply nothing.

#### The model reports

`forti_apply` returns the outcome in the same call: each operation verified on the device, the
rollback plan and your approval (`data.approval.approved_by`, e.g. `mcp:claude-code:elicitation`).

:::

Only an accepted prompt with the box checked approves. Declining, cancelling, accepting with the box
unchecked, closing the client or leaving the prompt unanswered for 15 minutes applies nothing; the
result is `approval_required` with `details.reason` `declined`, `cancelled` or `timeout`, and tells
the model not to ask again unless you do.

What forti-mcp records, and how it is bound:

- The approval is written only after you accept, as `approved_by: mcp:<client>:elicitation`, through
  the same engine call as `forti approve`, and appears in `audit.jsonl`.
- It is bound to the plan you were shown: forti-mcp keeps the digest of the plan it displayed, and
  refuses to apply (`reason: plan_changed`) if the approval would cover a different one. The plan
  itself binds the device (profile, host, serial, VDOM) and the operations.
- The model cannot approve: no tool argument is read as an approval. Only the client's answer to
  the prompt is.

:::details[How the two protocol eras ask]
With the `initialize` handshake (MCP 2025-11-25 and 2025-06-18), forti-mcp sends the client an
`elicitation/create` request during the `tools/call` and waits for the answer. Pings are answered
meanwhile; other requests wait their turn. If the client cancels the call, nothing is applied and no
result is sent for it.

With MCP 2026-07-28, servers no longer send requests. The `tools/call` result is
`resultType: "input_required"`, carrying the prompt and an opaque `requestState`; the client asks you
and repeats the identical call with your answer. The `requestState` is sealed with HMAC-SHA256 under
a key that exists only in this forti-mcp process, names the tool, the plan, its digest, the profile,
the client and the call's arguments, expires after 15 minutes, and is accepted once. A repeated call
with `operations` does not plan again: it applies the plan you were shown.
:::

### Guarded mode without in-chat approval

When the client cannot show a prompt, `forti_apply` returns a tool error (`isError: true`) whose
`structuredContent` is:

```json
{"ok": false, "error": {"code": "approval_required",
  "message": "plan 20261015-104512-a1b2c3 needs your approval (guarded mode): forti approve 20261015-104512-a1b2c3",
  "details": {"plan_id": "20261015-104512-a1b2c3", "reason": "missing",
              "approve_command": "forti approve 20261015-104512-a1b2c3",
              "watch_command": "forti approve --watch"}}}
```

and a text block for the model, asking it to relay the command:

```text
Approval required (guarded mode): plan 20261015-104512-a1b2c3 was not applied, and this client cannot show an approval prompt. Ask the user to approve it in their own terminal:
  forti approve 20261015-104512-a1b2c3
(or to keep `forti approve --watch` running there), then call forti_apply again with {"plan_id": "20261015-104512-a1b2c3"}.
```

Run `forti approve <id>` (it shows the plan and asks), or keep `forti approve --watch` open in a
terminal to be asked about each new plan as it arrives. Then the model calls `forti_apply` with the
`plan_id`, and it executes.

An approval, whichever way it was given:

- belongs to that plan's digest: a changed or tampered plan is not approved;
- expires after 24 hours (`reason: expired`);
- is used once (`reason: used`): applying again needs a new plan and a new approval.

:::callout[The approval is a person's act]{variant="warning"}
forti-mcp has no approve tool. It records an approval only from your answer to its prompt, never
from anything the model sends. Read what guarded mode does and does not guarantee in
[Safety and modes](/guides/safety-and-modes#what-guarded-mode-does-not-stop).
:::

### Autonomous mode and the safeguards of every apply

In an autonomous profile, `forti_apply` executes immediately. In every mode that writes, apply:

1. checks the plan's digest (tamper check);
2. re-reads every object the plan touches and checks the device's serial number, refusing with
   `stale_plan` if anything changed since planning (plan again);
3. checks that the audit log can be written, and refuses with `config` (nothing sent) if not;
4. for a critical-risk plan, first saves a configuration backup (owner-only, in
   `~/.forti/state/`) and refuses with `policy_denied` if it cannot, unless the profile sets
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
| `policy_denied` | The profile is read-only, the plan was already applied (`details.reason` `already_applied`), or a critical plan's backup failed |

:::callout[Plans do not linger]{variant="note"}
Once an apply has started executing a plan, whatever its outcome, the engine deletes the plan file;
a small marker keeps the id, so applying it again is refused as already applied. A plan refused
before anything was sent (stale, unapproved, read-only) is kept for a retry. Plans never applied,
rollback plans and the markers are deleted after the plan retention period, 30 days unless a person
changes it with `forti home retention DAYS`; no tool can change it.
:::

:::callout[Rolling back]{variant="tip"}
The rollback plan is an ordinary stored plan, kept until it is applied or the retention period ends.
Apply it like any other (`forti_apply` or `forti apply <id>`), under the same mode rules. It needs a working management path; see
[Recovery aids are not guaranteed recovery](/guides/safety-and-modes#recovery-aids-are-not-guaranteed-recovery).
:::
