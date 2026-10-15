---
title: Installation
description: Install forti-mcp from the APT repository or Homebrew, or build it from source.
order: 10
status: published
tags:
  - getting-started
---

# Installation

forti-mcp is a single executable, `forti-mcp`. libcurl is its only runtime dependency. Install it
from the ValkyrianLabs APT repository, from Homebrew, or build it from source, then check the
version it reports.

:::callout[Install forti as well]{variant="info"}
forti-mcp does not depend on the forti-cli package, but you will want `forti` as well to create
device profiles and approve plans (see [How it relates to forti-cli](/#how-it-relates-to-forti-cli)).
Each install path below includes it.
:::

## Install

:::tabs{
  default="apt"
  theme="glass"
  tabTheme="muted"
}

:::tab[Debian and Ubuntu (APT)]{value="apt"}
`forti-mcp` is published to the ValkyrianLabs APT repository (amd64):

```bash
sudo install -d -m 0755 /etc/apt/keyrings
sudo curl -fsSL https://apt.valkyrianlabs.com/pubkey.gpg \
  -o /etc/apt/keyrings/valkyrianlabs.gpg
sudo chmod 0644 /etc/apt/keyrings/valkyrianlabs.gpg

echo "deb [arch=amd64 signed-by=/etc/apt/keyrings/valkyrianlabs.gpg] https://apt.valkyrianlabs.com stable main" | \
  sudo tee /etc/apt/sources.list.d/valkyrianlabs.list > /dev/null

sudo apt update
sudo apt install -y forti-mcp
sudo apt install -y forti-cli      # the forti CLI: profiles, approvals, client registration
```

The package installs `/usr/bin/forti-mcp`.
:::

:::tab[Homebrew (macOS)]{value="homebrew"}
```bash
brew install valkyrianlabs/tap/forti-mcp
brew install valkyrianlabs/tap/forti-cli      # the forti CLI
```

Recent Homebrew versions ask you to trust a third-party tap first (`brew trust valkyrianlabs/tap`).

The formula builds from source, which takes a few minutes: the command engine's translation units
need about 2 GB of RAM each, so the formula keeps the number of parallel compile jobs modest.
:::

:::tab[From source]{value="source"}
```bash
# Ubuntu: sudo apt install meson ninja-build g++ pkgconf libcurl4-openssl-dev
# macOS:  brew install meson ninja       (libcurl comes with the SDK)
meson setup build
meson compile -C build -j 4
meson install -C build --skip-subprojects
```

A C++23 compiler (GCC 13 or newer, or a recent Clang) and Meson 1.3 or newer are required.

Meson fetches the engine at configure time: forti-cli's source at the commit
`subprojects/forti-cli.wrap` pins. forti-api comes from a system install (`libforti-api-dev`, or
the `forti-api` formula) when one is present, else from its own wrap; `--skip-subprojects` keeps
`meson install` from installing the SDK's headers in that case.
[Building and testing](/development/building-and-testing) has the details.
:::

:::

## Check the installation

```bash
forti-mcp --version
```

```text
forti-mcp 0.1.0 (engine forti-cli ecacc67, forti-api 1.0.1)
```

The version line names what the binary was built from: the forti-cli engine revision and the
forti-api version. A build from a modified engine checkout shows the revision with `-dirty`.

## Next

:::buttons{align="left" gap="md"}
::button[Connect an AI client]{href="/getting-started/connecting-clients" variant="primary"}
::button[Safety and modes]{href="/guides/safety-and-modes" variant="secondary"}
:::
