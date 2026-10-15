# Releasing forti-mcp

forti-mcp releases with vl-release (`vlr`, ValkyrianLabs convention, same as forti-cli and forti-api).
The repository contract is `release.toml`; release CI is `.github/workflows/release.yml` and only
calls `vlr` commands (plus `packaging/forti-cli-pin.sh`, below). The generic workflow is in
`.claude/skills/vl-release/SKILL.md`, this repository's specifics in `PROJECT.md` next to it
(`vlr install-local-skill` regenerates it).

## What ships

| Channel | Artifact | Built by |
|---|---|---|
| APT (`apt.valkyrianlabs.com`, suite `stable`, `main`) | `forti-mcp_<v>-1_amd64.deb`: `/usr/bin/forti-mcp`, copyright | `vlr build-deb` (`debian/`, against `libforti-api-dev >= 1.0.1`) |
| GitHub release | the .deb, `forti-mcp-<v>.tar.gz` (prepared tracked tree), `SHA256SUMS` | `vlr source-archive`, `vlr checksums`, `vlr github-release` |
| Homebrew (`valkyrianlabs/tap`) | `Formula/forti-mcp.rb`, builds from the source archive | `vlr homebrew formula` / `publish` (`packaging/homebrew/forti-mcp.rb`) |

forti-mcp installs independently (architecture decision 3): neither channel depends on the forti-cli
package or the `forti` executable, and no engine library or headers ship. Each binary carries its own
copy of the engine.

## Engine source

forti-mcp compiles forti-cli's engine (`engine/`, Meson dependency `forti-engine`) from source, as the
subproject `subprojects/forti-cli`. **`subprojects/forti-cli.wrap` is the single pin**: a full commit
id (`revision`) until forti-cli has release tags. Every build path uses exactly that revision:

| Build | Where the engine source comes from | Verified by |
|---|---|---|
| Development, CI | the wrap: Meson clones the pinned commit (or a co-development symlink, see AGENTS.md) | Git (commit id) |
| Debian (`vlr build-deb`) | `release.toml` `debian.pre_build` runs `packaging/forti-cli-pin.sh vendor` in the work tree first: `meson subprojects download forti-cli`, then HEAD = the pin and no local changes. A symlink there is refused. build-deb then copies the tree (without `.git`), and `dpkg-buildpackage` configures with `--wrap-mode=nodownload`: the package build itself fetches nothing | the script (commit id) |
| Homebrew | the formula's `resource "forti-cli"`: GitHub's archive of the pinned commit, staged into `subprojects/forti-cli` before `meson setup` (Meson uses an existing subproject directory as is, so `--wrap-mode=nofallback` never fetches) | Homebrew (sha256) |
| From the release source archive by hand | the wrap, at configure time | Git (commit id) |

`packaging/forti-cli-pin.sh check --download` (CI `lint`, release `build`) proves the formula's
resource is the wrap's revision and that GitHub serves the declared sha256. **Bumping the engine:**
change `revision` in the wrap, run `packaging/forti-cli-pin.sh update` (rewrites the resource url and
sha256), commit both. Once forti-cli tags releases, pin a tag's commit the same way.

The built binary says what it was built from: `forti-mcp --version` prints
`forti-mcp 0.1.0 (engine forti-cli a0456f1, forti-api 1.0.1)`. The revision is the wrap's for package
builds (an unpacked archive has no Git metadata), or the actual HEAD (`-dirty` with local changes) when
`subprojects/forti-cli` is a Git checkout. CI's clean-install and formula checks compare it with the
wrap.

### Why not a source archive that vendors the engine

The first choice was a self-contained release tarball: the prepared tree plus `subprojects/forti-cli`,
used by both Debian and Homebrew. vl-release 0.5 cannot produce it: `vlr source-archive` takes only
Git-tracked files (`git ls-files --cached`) plus the prepared documents, so an ignored subproject
directory is left out, and a submodule appears only as a gitlink, which the archiver skips. There is no
include key and no pre-step for the archive. `meson dist --include-subprojects` archives the committed
`HEAD`, so it lacks the prepared `debian/changelog` and release notes, and the formula must use vl-release's
archive (`source = "release-asset"`). Committing a copy of the engine would duplicate forti-cli's
source in this repository. The pinned-input approach above gives the same guarantees (exact revision,
verified, no download inside the package build) with no new artifact. If vl-release gains extra
archive paths, the formula's resource can go and Debian can build from that archive with
`--wrap-mode=nodownload` unchanged.

## Cutting a release

1. Keep `.release/RELEASE_NOTES_NEXT.md` and `.release/CHANGELOG_NEXT.md` current with every
   user-visible change (`vlr help staging`). `vlr status` must say `pending`.
2. On a clean, up-to-date `main`: `vlr check`, then `vlr cut patch|minor|major --push` (or
   `vlr cut 0.1.0 --push` for the first release: the version already in `VERSION`).
3. The tag push runs `release.yml`:
   `build` (gate, prepare, engine pin check, `.deb` against `libforti-api-dev`, source archive,
   formula, checksums, contract validation, clean-install smoke test in stock `ubuntu:24.04`, APT
   dry-run plan) → `homebrew-check` (macOS: formula from a local tap, built from the archive, `brew
   test`, smoke test) → `publish-apt` (Production; upload, index verification, install from the
   repository) → `github-release` → `homebrew-tap` (Production) → `finalize` (commits the promoted
   `debian/changelog` / `RELEASE_NOTES.md` to `main`).
4. A rehearsal without publishing: Actions → Release → Run workflow, `ref: vX.Y.Z`,
   `publish: dry-run` (runs `build` and `homebrew-check` only).

`debian/changelog` and `RELEASE_NOTES.md` do not exist until the first `vlr prepare`; never create
or edit them by hand (vl-release would treat a hand-written entry as an inconsistent release).

## Secrets, variables and settings

| Name | Kind | Used by |
|---|---|---|
| `NEXUS_USER`, `NEXUS_PASSWORD` | org secrets | `publish-apt` (`vlr publish-deb`) |
| `NEXUS_APT_REPO` | org variable | `publish-apt`: Nexus APT upload URL |
| `HOMEBREW_TAP_TOKEN` | secret (org or repo) | `homebrew-tap`: push to `valkyrianlabs/homebrew-tap` |
| `RELEASE_PUBLISH_MODE` | repo variable, optional | `nexus` (the workflow default) or `disabled` |
| `GITHUB_TOKEN` | automatic | `github-release`, `finalize` (`contents: write`) |

Org secrets and variables must be granted to this repository (organization settings → secrets →
repository access).

## Before the first release (checklist)

- [ ] Environment `Production` on `valkyrianlabs/forti-mcp` (`publish-apt`, `homebrew-tap` use it; add
      reviewers if publication should need a click).
- [ ] The org secrets/variables above are available to this repository; `HOMEBREW_TAP_TOKEN` can push
      to `valkyrianlabs/homebrew-tap` (fine-grained token: contents read/write on that repo).
- [ ] The self-hosted VPS runner (labels `self-hosted, Linux, X64, ubuntu-latest-lts`) serves this
      repository (runner group access), with rootless Podman as for forti-cli.
- [ ] If `main` is protected, `github-actions[bot]` may push the `finalize` commit.
- [ ] `libforti-api-dev >= 1.0.1` is in the APT index and `forti-api` 1.0.1 is in the tap (both are).
- [ ] The wrap pins the forti-cli commit (ideally a forti-cli release tag) that should ship, and the
      formula's resource matches (`packaging/forti-cli-pin.sh check --download`).
- [ ] Staged release docs describe the first release; `vlr status` says `pending`.
- [ ] A dry-run of `release.yml` (workflow_dispatch, `publish: dry-run`) is green.

## Build profile

The same as forti-cli's (its `.claude/context/release.md` has the measurements): `buildtype=release`
+ `optimization=s` (meson emits `-Oz` on clang, `-Os` on GCC), the engine's default 8 shards, no LTO;
on ELF, non-debug builds add `-ffunction-sections -fdata-sections -Wl,--gc-sections` as *global*
arguments, so the engine compiled in the subproject gets them too. Packaging: Debian swaps dpkg's
`-O2` for `-Os` and sets `b_ndebug=true` (`debian/rules`; debhelper strips into `-dbgsym`);
Homebrew passes `-Doptimization=s -Dstrip=true`.

Package builds pass `-Dforti-cli:subproject_cli=false` (Debian) or `-Dtests=disabled` (Homebrew), so
the subproject builds only the engine, never `forti`. The Debian build runs the unit and e2e suites;
the integration suite (forti-mcp and forti against forti-cli's mock FortiOS over loopback TLS: python3,
openssl and the `forti` executable) runs in CI on every change instead.

## Multi-architecture (arm64): vl-release gap

The same limits as forti-cli (its release.md, "Multi-architecture"): vl-release 0.5 accepts one
architecture per package contract and one `.deb` per package in a release directory, so the package is
amd64 only. `debian/control` says `Architecture: any`, and `libforti-api-dev` is `Architecture: all`,
so a native arm64 runner is all the build itself needs once vl-release accepts several architectures.
Homebrew builds from source on Apple silicon and Intel.
