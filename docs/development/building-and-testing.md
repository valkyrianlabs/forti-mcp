---
title: Building and testing
description: The engine pin and subproject, the test suites, CI, and keeping package builds on the pinned engine.
order: 110
status: published
tags:
  - development
---

# Building and testing

```bash
meson setup build/dev -Dtests=enabled --buildtype=debugoptimized
meson compile -C build/dev -j 4
meson test -C build/dev --print-errorlogs
```

forti-mcp is C++23 built with Meson (1.3 or newer), tested with GCC 13 and newer and Apple Clang. It
needs libcurl, GoogleTest for the tests, and python3 and openssl for the integration suite. No test
contacts a FortiGate.

## The engine: a pinned subproject

forti-mcp does not implement FortiOS semantics. It compiles forti-cli's command engine (`engine/`,
Meson dependency `forti-engine`) from source, as the subproject `subprojects/forti-cli`, and links it
statically. The engine brings forti-api, the header-only SDK, with it.

`subprojects/forti-cli.wrap` is the single pin: a full forti-cli commit id in `revision`. Meson
clones that commit at configure time. forti-api comes from a system install (`libforti-api-dev`,
the `forti-api` formula) when one is found, else from `subprojects/forti-api.wrap`.

For co-development, replace the subprojects with symlinks to sibling checkouts; Meson then uses them
instead of downloading:

```bash
ln -sfn "$(cd ../forti-cli && pwd)" subprojects/forti-cli
ln -sfn "$(cd ../forti-api && pwd)" subprojects/forti-api
```

The engine's resource catalog is compiled in shards, and each shard's translation unit needs 1 to
2.5 GB of RAM depending on the compiler and optimization level, so keep the job count modest.
`ci/build-jobs` prints a memory-aware job count for the current machine.

`forti-mcp --version` reports the engine revision it was built from: the actual `HEAD` of a Git
checkout in `subprojects/forti-cli` (with `-dirty` for local changes), or the wrap's revision for a
source tree without Git metadata, as in package builds.

### Changing the pin

1. Edit `revision` in `subprojects/forti-cli.wrap`.
2. Run `packaging/forti-cli-pin.sh update`: it points the Homebrew formula's `forti-cli` resource
   at the new revision and computes its sha256.
3. Commit both files.

`packaging/forti-cli-pin.sh` keeps every package build on the wrap's revision:

| Command | Does |
|---|---|
| `vendor` | Clones forti-cli at the pinned revision into `subprojects/forti-cli` (through the wrap) and verifies it. A symlinked co-development checkout is refused. The Debian build runs it first |
| `check [--download]` | Checks that the formula's resource names the pinned revision; `--download` also verifies GitHub's archive against its sha256 |
| `update` | Points the formula's resource at the pinned revision |

Package builds then never fetch anything themselves: Debian configures with
`--wrap-mode=nodownload` against the system `libforti-api-dev`, and the Homebrew formula stages its
`forti-cli` resource into `subprojects/forti-cli` before `meson setup`.

## Test suites

| Suite | What it runs |
|---|---|
| `unit` | GoogleTest over the protocol core (`Server::handle`), the tools, the byte budget and the options, with the engine on a scripted transport (`tests/support/fake_transport.hpp`); includes a quick slice of the fuzz tests |
| `fuzz` | The same binary's fuzz tests at full size (`FORTI_FUZZ=full`); minutes under sanitizers |
| `e2e` | Spawns the built `forti-mcp` with temporary config, state and cache directories and talks to it over stdio |
| `integration` | Real `forti-mcp` and real `forti` against forti-cli's stateful mock FortiOS over loopback TLS: both protocol eras, `--read-only`, the byte budget, plan in MCP and approve in the CLI, drift, rollback, critical backups. Also the no-second-HTTP-stack scan of the source and binary |

```bash
meson test -C build/dev --suite unit --suite e2e
meson test -C build/dev --suite integration -v
```

The integration suite needs python3, openssl and a forti-cli subproject that ships its mock FortiOS
and builds `forti`. With tests enabled, the top-level `meson.build` turns that on for the subproject
(`subproject_cli`). Without them the suite is reported as not built, not failed. Tests never touch
your real configuration, secret store or AI-client configuration.

## CI

CI (`.github/workflows/ci.yml`) runs on every push to `main` and on pull requests. Each job builds
exactly once, unoptimized (`-O0`, warnings as errors), and runs every suite against that build:

- **Linux** (Ubuntu 24.04, GCC 13, in a disposable Podman container, `ci/run-ci`): first the quick
  checks, shellcheck, `packaging/forti-cli-pin.sh check --download` and
  `pmdocs validate --source forti-mcp`; then the build with AddressSanitizer and
  UndefinedBehaviorSanitizer, the unit, e2e and integration suites, and `ci/smoke-test.sh`. A
  skipped test fails the job. Pushes to `main` also run the full-size fuzz suite.
- **macOS** (Apple Clang): the same build without sanitizers, the same suites and the smoke test.

ccache keeps the engine's shards between runs, so a change that touches neither the forti-cli nor
the forti-api pin rebuilds only forti-mcp's own files.

The release build is a different configuration and is tested as built. `meson.build` defaults to
`buildtype=release` (`-O3`); the Debian package uses `-O3` in place of dpkg's `-O2` with NDEBUG and
without LTO or `-g`, and the Homebrew formula passes `-Doptimization=3`. The Debian build runs the
unit and e2e suites against that binary. `.github/workflows/package.yml` (run on demand) and
`.github/workflows/release.yml` (on a tag) build the package and the formula this way, install the
`.deb` into a stock Ubuntu 24.04 and run the smoke test there, and check that `--version` reports
the wrap's engine revision.

`ci/smoke-test.sh` needs only POSIX tools. It checks `--version`, plays an MCP client over stdio
(`initialize`, `tools/list` and an offline `forti_discover` resource search), checks that nothing
was written to the config or state directories and that a bad option exits 2.

## Releases

forti-mcp releases with vl-release (`vlr`). Keep `.release/RELEASE_NOTES_NEXT.md` and
`.release/CHANGELOG_NEXT.md` current with user-visible changes; the repository's
`.claude/context/release.md` describes the channels (APT, GitHub release, Homebrew) and the release
workflow. The docs package (`docs/`, docs set `forti-mcp`) is validated in CI and published by the
release workflow:

```bash
pmdocs validate --source forti-mcp
```
