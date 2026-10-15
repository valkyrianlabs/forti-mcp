---
title: Command line
description: forti-mcp's options, exit statuses, environment and the files it reads.
order: 100
status: published
tags:
  - reference
---

# Command line

An MCP client starts `forti-mcp`, normally with no arguments, and talks JSON-RPC over stdio. The
options only adjust policy for that process. Pass them in the client entry's arguments, e.g.
`claude mcp add forti -- forti-mcp --read-only` ([Connecting AI clients](/getting-started/connecting-clients)).

:::toc[On this page]{depth="2" theme="compact"}
:::

## Options

```text
forti-mcp [--profile NAME] [--read-only] [--max-bytes N]
forti-mcp --version
forti-mcp --help
```

| Option | Meaning |
|---|---|
| `--profile NAME` | The device profile used when a request names none. Without it, the configuration's default profile (see `forti devices`). `NAME` is 1 to 64 letters, digits, `-` and `_` |
| `--read-only` | Offer only `forti_discover` and `forti_read`. `forti_plan` and `forti_apply` are not listed, and calling them is a protocol error |
| `--max-bytes N` | The JSON budget per tool result, in bytes: default 49152 (48 KiB), from 4096 to 16777216 ([Large results and paging](/guides/large-results-and-paging)) |
| `--version` | Print the version and what it was built from, then exit |
| `--help`, `-h` | Print the usage, then exit |

Values can follow the option or be attached with `=` (`--max-bytes=65536`). Each option may be given
once. `--profile` does not apply to `forti_apply`: a stored plan already names its profile.

:::callout[--read-only limits the registration, not the profile]{variant="info"}
`--read-only` does not change any profile's mode; it limits this registration. A client entry with
`--read-only` cannot plan or apply even on an autonomous profile. Its effect is visible to the model:
the server's instructions say that plan and apply are not available.
:::

```text
$ forti-mcp --version
forti-mcp 0.2.0 (engine forti-cli 6a19035, forti-api 1.0.1)
```

## Exit status

| Status | When |
|---|---|
| 0 | stdin closed (the client finished), or `--version` / `--help` |
| 1 | stdin could not be read, stdout could not be written, or startup failed |
| 2 | Invalid options: an unknown option, a missing or invalid value, an option given twice. The message goes to stderr |

Errors during a session never end the process: they are JSON-RPC errors or tool errors
([Protocol](/reference/protocol)).

## Environment and files

forti-mcp needs no environment variables and no client `env` entry. It reads the same forti home as
the `forti` CLI: `$FORTI_HOME`, else `~/.forti` (`forti home` prints every location).

| What | Where | Override |
|---|---|---|
| Profiles (`config.json`) and file-stored secrets (`credentials/`) | `~/.forti` | `FORTI_CONFIG_DIR` |
| Plans, approvals, audit log (`audit.jsonl`), pre-change backups | `~/.forti/state` | `FORTI_STATE_DIR` |
| Cache (reserved; live schemas are not cached yet) | `~/.forti/cache` | `FORTI_CACHE_DIR` |
| Client certificates from `forti cert create` | `~/.forti/certs/<profile>` | |

`FORTI_HOME` moves the whole home; the `FORTI_*_DIR` variables move one location inside it. On
macOS, API keys stored with `forti configure` live in the login Keychain (service `forti`), and
`FORTI_SECRET_STORE=file` chooses the file store instead. The locations never depend on the working
directory, so the client may start forti-mcp anywhere.

:::callout[Pre-0.2 locations]{variant="note"}
A setup from before 0.2 (`~/.config/forti`, `~/.local/state/forti`, `~/.cache/forti`, or the
`XDG_*` variables) stays in use while `~/.forti/config.json` does not exist and neither `FORTI_HOME`
nor `FORTI_CONFIG_DIR` is set. `forti home migrate` moves it into the home
([Upgrading from 0.1](/getting-started/installation#upgrading-from-0-1)).
:::

:::callout[Path and ownership rules]{variant="warning"}
`FORTI_HOME` and the `FORTI_*_DIR` overrides must be absolute paths. forti refuses a `config.json`
owned by another user or writable by group or others.
:::
