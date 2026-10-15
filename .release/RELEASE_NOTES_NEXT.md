<!--
vl-release staged release notes: the user-facing story of the NEXT release.
Maintained continuously while working; `vlr prepare` promotes it into the release notes
history (and the GitHub release) and resets this file to this template.

Format: the first line is "# <release title>" WITHOUT a version number (vlr adds it).
Everything after the title is the Markdown release body. Describe the resulting behavior
for users and operators; keep it representative of what actually ships.
-->
# The ~/.forti workspace

forti-mcp now builds on the forti-cli 0.2.0 engine. Everything forti keeps lives in one directory,
the **forti home** (`~/.forti`, or `$FORTI_HOME`), and that directory is also the recommended place
to run your AI agent.

## One home for profiles, secrets, certificates and plans

forti-mcp reads its device profiles, file-stored credentials, client certificates, plans,
approvals and audit log from the forti home, the same one the `forti` CLI uses. `forti home` shows
every location.

**Upgrading from 0.1:** nothing breaks. An existing setup in `~/.config/forti` (with state in
`~/.local/state/forti`) keeps working as long as `~/.forti/config.json` does not exist. Run
`forti home migrate` (forti-cli 0.2.0) when you are ready to move it; Keychain items need no change.

## The workspace: firewall tools only where you want them

```bash
forti install mcp --scope workspace
cd ~/.forti && claude      # or: cd ~/.forti && codex
```

`forti install mcp --scope workspace` registers forti-mcp in the forti home only, so an agent started
there gets the four FortiGate tools and an agent started in any other project does not. The
workspace also keeps secrets out of the agent's context: Claude Code deny rules for `credentials/`,
certificate passwords and keys and edits to `config.json`, a Codex permission profile that denies the
same paths to sandboxed commands, and an `AGENTS.md` that tells the agent the rules. Codex reads the
workspace's configuration once it trusts `~/.forti`; `--trust-codex-workspace` adds that trust with
your consent. Registering the server by hand still works as before.

## Client certificates and several FortiGates

- Profiles made with `forti cert create` authenticate with a client certificate kept in
  `~/.forti/certs/<profile>/`, its password read from an owner-only file; forti-mcp uses them like
  any other profile.
- One forti-mcp serves every FortiGate in the forti home. The model names a device with the
  `profile` field of `forti_discover`, `forti_read` and `forti_plan`; `--profile` sets the default
  for a registration. Each profile keeps its own mode.

## Documentation

The docs are rewritten around the forti home: Connecting AI clients leads with the workspace,
Installation covers the home and migration, Security says where secrets and certificates live and
what the deny rules do, and a new guide, Several FortiGates, covers profiles per device.
