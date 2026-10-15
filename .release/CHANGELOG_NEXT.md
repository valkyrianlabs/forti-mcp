<!--
vl-release staged changelog: package-facing changes for the NEXT release.
Maintained continuously while working; `vlr prepare` renders it into debian/changelog
and resets this file to this template.

Format: one "- " bullet per change (concise, technical). Indent continuation lines.
Optional "## Section" headings group bullets; if used, every bullet must be under one.
One level of nested "  - " detail bullets is allowed. Consolidate; don't paste commit logs.
-->

## Engine
- The command engine is re-pinned to forti-cli 6a19035 (subprojects/forti-cli.wrap and the Homebrew
  formula's forti-cli resource), the engine of forti-cli 0.2.0.
- One forti home: `$FORTI_HOME`, else `~/.forti`, holds config.json, credentials/, state/, cache/
  and certs/<profile>/. `FORTI_CONFIG_DIR`, `FORTI_STATE_DIR` and `FORTI_CACHE_DIR` still override
  single locations. A pre-0.2 setup (`~/.config/forti`, `~/.local/state/forti`, `~/.cache/forti` or
  the XDG variables) stays in use while the home has no config.json and neither `FORTI_HOME` nor
  `FORTI_CONFIG_DIR` is set; `forti home migrate` moves it.
- Profiles can read a client certificate password from an owner-only file (secret source `file`),
  as written by `forti cert create`.

## Tests and packaging
- Tests build the engine's `Paths` with its new `home` member (no missing-field-initializers
  warning under GCC -Werror); the scripted transport gains `reply_raw`, in sync with forti-cli.
- The e2e tests, the smoke test and the Homebrew formula test set a temporary `FORTI_HOME`, so they
  never resolve the real forti home.

## Documentation
- The docs are built around the forti home and the `~/.forti` workspace:
  - Connecting AI clients leads with `forti install mcp --scope workspace` (`cd ~/.forti && claude`
    or `codex`), why the tools stay out of other projects, Codex trust
    (`--trust-codex-workspace`), and the Claude Code deny rules and Codex `forti-workspace`
    permission profile; manual registration remains the alternative.
  - Installation documents the forti home, `forti home` and upgrading with `forti home migrate`;
    the command-line reference lists the home's locations and overrides.
  - Security covers where secrets and certificates live and the workspace deny rules.
  - New guide, Several FortiGates: a profile per device, `--profile` and the per-request
    `profile` field.
