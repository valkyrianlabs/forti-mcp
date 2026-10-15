---
title: Large results and paging
description: The per-result byte budget, how results are trimmed, and how a model continues a listing.
order: 50
status: published
tags:
  - guides
---

# Large results and paging

A FortiGate can hold thousands of addresses or policies, and a single object can be large. Every
tool result is therefore bounded twice: by an item count, and by a byte budget on its JSON.

## Item counts

`forti_read` and `forti_discover` (`what: resources`) take `start` (default 0) and `count` (default
50, at most 1000).

- Configuration tables page on the device. When a full page comes back, `meta.next_start` is
  `start + count`; the next page may turn out to be empty.
- Monitor endpoints do not page consistently on FortiOS, so the engine windows their results itself
  and reports `meta.total`, plus `meta.next_start` and `meta.truncated: true` when more remain.
- Resource searches report `meta.total`, and `meta.next_start` when more remain.

Every listing reports `meta.start` and `meta.returned`.

## The byte budget

Each tool result's envelope (`{"ok", "data", "meta"}` or `{"ok", "error"}`), serialized as compact
JSON, must fit the budget: **49152 bytes (48 KiB) by default**, set with `--max-bytes` between 4096
and 16777216 ([Command line](/reference/command-line)). The result is always complete, valid JSON:
the server drops whole items, never bytes.

| The result | What is cut | What `meta` says |
|---|---|---|
| A listing (`data` is an array) | The longest prefix of items that fits is kept | `truncated: true`, `returned`, `next_start` = `start` + kept, `budget_bytes` |
| One object (`data` is an object) | Its largest nested arrays are shortened, largest first | `truncated: true`, `trimmed` (JSON pointer to `{kept, total}`), `hint`, `budget_bytes` |
| Still too large | `data` becomes `null` | `truncated: true`, `hint` with the result's size and the budget |
| An error | `details` is replaced by `{"truncated": true}`, then the message is shortened (ending in `[truncated]`) | |

When a single item at `start` is larger than the budget on its own, nothing can be returned and
`meta.hint` says to select attributes with `fields` or read the item by `key`.

For example, a listing of 200 addresses of which 137 fit the budget (abridged):

```json
{"ok": true, "data": ["... 137 items ..."],
 "meta": {"resource": "firewall/address", "profile": "home", "start": 0, "returned": 137,
          "next_start": 137, "truncated": true, "budget_bytes": 49152}}
```

The text content block and `structuredContent` carry the same JSON, so one `tools/call` response
line is about twice the budget plus a fixed overhead.

## What the model does

The tool descriptions and the server's instructions give the model one rule: **when
`meta.truncated` is true or `meta.next_start` is present, call again with `start` set to
`meta.next_start`, or narrow the request.** Narrowing usually works better than paging:

- `fields`: only the attributes needed (`["name", "subnet"]`);
- `filter`: only matching objects (`["type==ipmask", "name=@lan"]`; FortiOS operators `==`, `!=`,
  `=@` (contains), `!@`, `<`, `<=`, `>`, `>=`; list items are ANDed, a comma inside one item ORs);
- `key`: one object;
- a smaller `count`.

An object whose nested arrays were trimmed (`meta.trimmed`) has no `next_start`, because there is no
paging inside one object: narrow it with `fields` instead.

## Choosing a budget

The default keeps a result well inside a model's context while fitting a page of typical objects. A
larger budget (`--max-bytes 131072`) means fewer round-trips on large tables at the cost of context;
a smaller one suits clients with tight tool-output limits. The budget is per result, not per
session.
