# CI container

forti-mcp's Linux CI and release jobs follow forti-cli's and forti-api's conventions: every step runs
inside a disposable, rootless Podman container on the self-hosted VPS runner (GitHub-hosted
`ubuntu-latest` for pull requests from forks). The runner needs no host sudo, host packages or
Docker. The shared conventions are described in [payload-markdown's `ci/README.md`](https://github.com/valkyrianlabs/payload-markdown/blob/main/ci/README.md).

- `ci/run-ci` is copied **verbatim** from forti-cli / forti-api / payload-markdown (it is
  repository-agnostic); update it there and copy it back.
- `ci/Containerfile`: Ubuntu 24.04 (pinned by digest, GCC 13) with meson, ninja, g++, libcurl,
  GoogleTest, python3 and openssl (integration suite), dpkg-dev, debhelper, gh and vl-release.
  libforti-api-dev is installed at run time by the package jobs only (development builds use the
  pinned wraps).
- `ci/smoke-test.sh`: an offline check of a built or installed `forti-mcp` (`--version`, an MCP
  handshake, `tools/list`, an offline `forti_discover`). It needs only POSIX tools, so it also runs in a
  stock `ubuntu:24.04` with just the `.deb` installed.
- Workflows set `defaults.run.shell: bash ./ci/run-ci bash -euo pipefail {0}` on their Linux jobs.
  Clean-install checks of the `.deb` use a stock image instead:
  `./ci/run-ci --image "$UBUNTU_IMAGE" --root bash -c 'apt-get install -y ./release/…'`.
- ccache: `ci/run-ci` keeps `$cache/ccache` on the runner and mounts it at `/cache/ccache`
  (`CCACHE_DIR`, `CCACHE_MAXSIZE=8G`); Meson picks ccache up from the image. The engine's shard TUs
  are ~70% of a build, so a PR that changes neither the forti-cli nor the forti-api pin gets them from
  the cache. macOS uses `actions/cache` on `~/Library/Caches/ccache`, keyed by the two wraps.
- Memory and parallelism: `ci/run-ci` gives the container three quarters of the host's RAM
  (`CI_MEMORY` overrides it), and `ci/build-jobs` runs every core that memory allows, budgeting 2.2 GB
  per job (an engine shard TU with GCC 13: ~1.9 GB at -O0, ~2.3 GB at -O3, smaller with 10 shards).
  Each CI job builds exactly once: pull requests build -O0 (Linux with ASan/UBSan) and run every suite
  against it; release builds are -O3 and are tested as built (package.yml, release.yml).

Build locally the way CI does (needs Podman):

```sh
./ci/run-ci bash -c 'meson setup build/ci -Dtests=enabled -Dwerror=true && meson compile -C build/ci -j 4 && meson test -C build/ci'
```
