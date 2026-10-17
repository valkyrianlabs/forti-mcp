---
title: Tools
description: The four tools, their input schemas, results and annotations.
order: 90
status: published
tags:
  - reference
---

# Tools

forti-mcp offers four tools. With `--read-only` only the first two are listed, and calls to the
others are refused with a protocol error ([Protocol](/reference/protocol)).

| Tool | Does | Touches the device |
|---|---|---|
| `forti_discover` | Profiles, a device's identity, version and mode, resource search, a resource's attributes | Only for `what: capabilities` and `live: true` (reads) |
| `forti_read` | Configuration or monitor data | Reads |
| `forti_plan` | Previews a change: validates it and stores it as a plan | Reads only |
| `forti_apply` | Makes a change: `operations` (plan and apply in one call) or a stored `plan_id` | Writes, if the profile's mode allows |

Every input schema is strict (`additionalProperties: false`): an unknown argument is an error, and
the message lists the allowed ones. The schemas mirror the engine's requests, which forti-cli shares.

:::toc[On this page]{depth="2" theme="compact"}
:::

:::cards{
  columns="2"
  cardTheme="muted"
}

:::card[forti_discover]{
  href="/reference/tools#forti-discover"
  eyebrow="Start here"
}
Profiles, device identity and mode, resource search and resource details.
:::

:::card[forti_read]{
  href="/reference/tools#forti-read"
  eyebrow="Read"
}
Configuration or monitor data, bounded and paginated.
:::

:::card[forti_plan]{
  href="/reference/tools#forti-plan"
  eyebrow="Plan"
}
Previews a change: validates it and stores it as a plan, without touching the device.
:::

:::card[forti_apply]{
  href="/reference/tools#forti-apply"
  eyebrow="Apply"
}
Makes a change, from operations or a stored plan, as the profile's mode allows.
:::

:::

## Results

Every tool returns the engine's envelope as `structuredContent`, and the same JSON as the first text
content block. `isError` is true exactly when `ok` is false.

```json
{"ok": true, "data": "...", "meta": {"profile": "home", "resource": "firewall/address", "start": 0, "returned": 50, "next_start": 50}}
{"ok": false, "error": {"code": "not_found", "message": "...", "details": {}}}
```

The `outputSchema` of every tool describes this envelope: `ok` (boolean, required), `data`, `meta`
(object) and `error` (object with `code` and `message`, and optional `details`).

Anything the model can correct is a tool result with `isError: true`, not a protocol error: invalid
arguments (code `usage`, with the failing JSON pointer, e.g. `/operations/0/op: must be one of
"create", "update", "delete", "action"`), and every engine and device error.

| `error.code` | Meaning |
|---|---|
| `usage` | Bad arguments or request shape |
| `config` | Missing or invalid profile, credentials or configuration file |
| `connection` | DNS, connect, TLS or timeout |
| `auth` | HTTP 401 or 403 |
| `not_found` | The object does not exist |
| `unknown_resource` | The path is not in the catalog |
| `validation` | Unknown or read-only attribute, or a value that does not decode |
| `api` | Any other FortiOS error |
| `stale_plan` | The device changed since the plan was made |
| `approval_required` | Guarded mode, and the plan has no valid approval (or the person did not approve it in the prompt) |
| `policy_denied` | The profile is read-only, or the plan cannot run (already applied, backup failed, tampered) |
| `partial_failure` | Apply stopped part-way; `details` has the record |
| `management_connectivity_lost` | Apply stopped after a critical change cut the management path |
| `outcome_unknown` | A change may have reached the device without a reply |
| `internal` | A bug or unexpected state |

Results are bounded by the byte budget ([Large results and paging](/guides/large-results-and-paging)).

## Annotations

| Tool | `readOnlyHint` | `destructiveHint` | `idempotentHint` | `openWorldHint` |
|---|---|---|---|---|
| `forti_discover` | true | not set | not set | false |
| `forti_read` | true | not set | not set | false |
| `forti_plan` | true | not set | not set | false |
| `forti_apply` | false | true | false | false |

`forti_plan` is read-only for the device: it reads the current objects and writes only a local plan
file. Every tool also has a `title`, repeated in its annotations.

:::callout[Clients may run forti_plan without asking]{variant="note"}
Some clients run read-only tools without asking. The [safety guide](/guides/safety-and-modes) covers
what that means for planning.
:::

## forti_discover

"Start here." Profiles, device identity and mode, resource search and resource details.

| Field | Type | Description |
|---|---|---|
| `what` | `devices`, `resources`, `resource`, `capabilities` | Default: `resource` when `resource` is given, else `resources` |
| `query` | string, at most 256 characters | `resources`: case-insensitive substring of the path, e.g. `addr` |
| `kind` | `table`, `singleton`, `monitor` | `resources`: restrict to one kind |
| `start` | integer, at least 0 | First item (default 0); continue with `meta.next_start` |
| `count` | integer, 1 to 1000 | Items per page (default 50) |
| `resource` | string, 1 to 256 characters | `resource`: e.g. `firewall/address`, `firewall.service/custom`, `system/interface`, `monitor/system/status` |
| `fields` | array of up to 200 strings | `resource`: attributes to describe in full (nested children, and live help and options with `live`) |
| `live` | boolean | `resource`: merge the device's live schema |
| `profile` | string matching `^[A-Za-z0-9_-]{1,64}$` | Device profile; default: `--profile`, else the configured default |

| `what` | `data` |
|---|---|
| `devices` | The profiles: name, host, port, mode and where the key comes from (never the key); `meta.default_profile` |
| `capabilities` | `profile`, `device` (serial, version, build, hostname, model), `sdk_schema_version`, `schema_matches_device`, `mode`, `resources` (catalog size) |
| `resources` | Matching resources (`resource`, `kind`, `key`); `meta.total`, `start`, `returned`, `next_start` |
| `resource` | `resource`, `kind`, `key`, `fields` (every attribute, or full detail of the selected ones), `operations` (`read`, `write`) and `risk`; with `live`, the device's help text, options and ranges; `meta.schema_version`, `meta.live` |

Everything except `capabilities` and `live: true` is answered from the compiled-in catalog. Monitor
paths that are not in the catalog can still be read untyped with `forti_read`.

## forti_read

Read configuration (cmdb tables and singletons) or monitor data over the FortiOS REST API.

| Field | Type | Description |
|---|---|---|
| `resource` | string, 1 to 256 characters, **required** | Resource path from `forti_discover`: `firewall/address`, `firewall/policy`, `system/global`, `monitor/system/status`, ... |
| `key` | string or integer | Table object key (its name, or the number for policyid-style keys); with `child`, the parent object's key |
| `child` | string, 1 to 128 characters | A table attribute of the object `key` (a child table, e.g. `reserved-address` of `system.dhcp/server`): its entries instead of the object |
| `child_key` | string or integer | With `child`: only this entry (its id or name) |
| `filter` | array of up to 32 strings | FortiOS filter clauses, ANDed: `["type==ipmask", "name=@lan"]` |
| `fields` | array of up to 200 strings | Return only these attributes, e.g. `["name", "subnet"]` |
| `start` | integer, at least 0 | First item (default 0); continue with `meta.next_start` |
| `count` | integer, 1 to 1000 | Items per page (default 50) |
| `vdom` | string matching `^[A-Za-z0-9._-]{1,31}$` | VDOM (default: the profile's) |
| `profile` | string matching `^[A-Za-z0-9_-]{1,64}$` | Device profile |
| `params` | object of string, number or boolean values | Extra query parameters, mainly for monitor endpoints |

Each string in `filter` and `fields` is 1 to 256 characters. Filter operators are `==`, `!=`, `=@`,
`!@`, `<`, `<=`, `>` and `>=`; a comma inside one clause ORs. Attribute names in `filter` and
`fields` are checked against the catalog; with `child`, against the entry's attributes. `child` must
be a table attribute of the resource with keyed entries (`validation` otherwise, with the table
attributes it has).

```json
{"resource": "system.dhcp/server", "key": 1, "child": "reserved-address", "fields": ["id", "ip", "mac", "description"]}
```

`data` is the device's JSON as FortiOS sent it (an object for `key` and singletons, an array for
listings), with secrets replaced by `"<redacted>"`. `meta` has `resource`, `profile`, `device`
(serial, version, build), `vdom`, and for listings `start`, `returned`, `next_start`, `total` and
`truncated` as described in [Large results and paging](/guides/large-results-and-paging), plus
`redacted` (JSON pointers of redacted values, e.g. `["/*/psksecret"]`).

## forti_plan

Preview a configuration change: validate it and store it as a plan, without touching the device. To
make a change, `forti_apply` with the same `operations` plans and applies in one call.

| Field | Type | Description |
|---|---|---|
| `operations` | array, at least 1 item, **required** | The operations, run in order at apply time |
| `profile` | string matching `^[A-Za-z0-9_-]{1,64}$` | Device profile |
| `vdom` | string matching `^[A-Za-z0-9._-]{1,31}$` | VDOM (default: the profile's) |

Each operation:

| Field | Type | Description |
|---|---|---|
| `op` | `create`, `update`, `delete`, `action`, **required** | |
| `resource` | string, 1 to 256 characters, **required** | A cmdb resource; for `action` a monitor path (`monitor/...`) |
| `key` | string or integer | `update`, `delete`: the object's key; with `parent`/`child`, the entry's key |
| `parent` | string or integer | With `child`: the key of the object whose table attribute holds the entry |
| `child` | string, 1 to 128 characters | With `parent`: the table attribute (`reserved-address`); the operation changes one entry |
| `object` | object | `create`: the new object's attributes |
| `set` | object | `update`: attributes to change |
| `clear` | array of up to 200 strings | `update`: attributes to reset to their defaults |
| `body` | object | `action`: the request body |

With `parent` and `child`, `create`, `update` and `delete` change one entry of a table attribute on
FortiOS's child paths (`cmdb/system.dhcp/server/1/reserved-address/21`) and leave the object's other
entries alone. `object`, `set` and `clear` then name the entry's attributes, a numeric id left out of
`create` (or 0) is assigned by the device and reported as the operation's `key`, and the plan's
before-state, drift check, verification and rollback are that entry's. Prefer it to setting a whole
table attribute on the object, which replaces the list:

```json
{"op": "update", "resource": "system.dhcp/server", "parent": "1", "child": "reserved-address", "key": "21",
 "set": {"description": "printer"}}
```

`data` has the plan `id` (pass it to `forti_apply` as `plan_id`), `digest`, `kind`, `created_at`,
`device` (profile, host, port, vdom, serial, version), `risk`, `summary`, `operations` (each with its
`diff` and the exact request, `wire`, secrets redacted), `mode`, `notes`, `apply` (the equivalent
`forti apply <id>` command) and, in guarded mode, `approve` (`forti approve <id>`).

:::callout[Redacted values cannot be written]{variant="info"}
A value of `"<redacted>"` is refused (`validation`): a model cannot write back a secret it was never
shown.
:::

## forti_apply

Change the live firewall: plan and apply operations in one call, or apply a stored plan.

:::callout[Writes to the device]{variant="warning"}
`forti_apply` is annotated destructive and not idempotent. Whether it runs depends on the profile's
mode, not on the client ([Safety and modes](/guides/safety-and-modes)).
:::

| Field | Type | Description |
|---|---|---|
| `operations` | array, at least 1 item | The change, exactly as for `forti_plan`: planned, then applied |
| `profile` | string matching `^[A-Za-z0-9_-]{1,64}$` | With `operations`: device profile |
| `vdom` | string matching `^[A-Za-z0-9._-]{1,31}$` | With `operations`: VDOM (default: the profile's) |
| `plan_id` | string matching `^[a-z0-9][a-z0-9-]{11,31}$` | Instead of `operations`: a plan `forti_plan` returned |

Pass exactly one of `operations` and `plan_id` (anything else is `usage`). A stored plan already names
its device, so `profile` and `vdom` go only with `operations`. The outcome follows the profile's mode
([Workflow](/guides/workflow)):

| Mode | Outcome |
|---|---|
| `read-only` | `policy_denied`, with `details.mode_command` |
| `guarded` | Applied once a person approves this exact plan. With a client that supports form elicitation, forti-mcp asks in the conversation during the call; an accepted prompt with **Apply these changes** checked applies, anything else is `approval_required` with `details.reason` `declined`, `cancelled` or `timeout`. Without elicitation, `approval_required` with `details.approve_command` (`forti approve <plan_id>`), `details.watch_command` and a second text block asking the model to relay it |
| `autonomous` | Applied now |

In the 2026-07-28 protocol the in-chat prompt arrives as a `tools/call` result with
`resultType: "input_required"`; the client repeats the call with `inputResponses` and `requestState`
([Protocol](/reference/protocol#in-chat-approval)).

On success, `data` has `status: "applied"`, `plan_id`, `digest`, `operations` (per-operation
status: `verified`, `applied_unverified`, `failed`, `outcome_unknown` or `not_attempted`), the
numbers `verified` and `applied_unverified`, `rollback` (the generated rollback plan, with its
`plan_id`), `backup` for critical plans, and `audit_error` if the audit record could not be written.
`partial_failure`, `outcome_unknown` and `management_connectivity_lost` carry the same record in
`error.details`. A call with `operations` adds `data.plan` (or `error.details.plan`): the plan's `id`,
`summary`, `risk`, `digest` and each operation's `diff`. A plan approved in the prompt adds
`data.approval` (`approved_by`, e.g. `mcp:claude-code:elicitation`, `approved_at`, `via`).
