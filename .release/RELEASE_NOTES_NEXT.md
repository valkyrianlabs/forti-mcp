<!--
vl-release staged release notes: the user-facing story of the NEXT release.
Maintained continuously while working; `vlr prepare` promotes it into the release notes
history (and the GitHub release) and resets this file to this template.

Format: the first line is "# <release title>" WITHOUT a version number (vlr adds it).
Everything after the title is the Markdown release body. Describe the resulting behavior
for users and operators; keep it representative of what actually ships.
-->
# Resources as admins name them, clearer errors, a bottle and a lighter build

forti-mcp 0.3 carries forti-cli 0.3.1's engine.

## Approve changes in the conversation

- **One call to make a change.** `forti_apply` takes the operations themselves and plans and applies
  them in one call; `forti_plan` remains for previewing. The result includes the plan's diff, so the
  agent can report exactly what changed.
- **Guarded mode asks you where you already are.** With a client that supports MCP elicitation
  (Claude Code and Codex do), you get one prompt in the conversation: the device, mode, risk and
  every attribute change, with an **Apply these changes** checkbox. Approve and the change is
  applied, verified and reported in the same call. Decline or cancel and nothing is applied, and the
  agent is told not to ask again.
- **The approval is bound to what you saw.** It is recorded as `mcp:<client>:elicitation` only from
  your answer (never from anything the model sends), covers only the plan digest you were shown, and
  appears in the audit log.
- **No prompt support? One short command.** The result names `forti approve <id>` for your terminal,
  or keep `forti approve --watch` running there.

## For agents

- `forti_read` and `forti_discover` take resources the way FortiOS admins name them: the path
  (`firewall/address`), FortiOS CLI words (`firewall address`), a last word only one resource has
  (`address`), and a table object by its path (`firewall/address/web`). An ambiguous name returns
  `unknown_resource` with the candidates in `error.details.candidates`.
- A misspelt attribute (in `fields`, `filter` or a plan) comes back with `details.suggestions`
  ("did you mean color?").
- A read-only profile's refusal names the command a person runs to allow changes
  (`details.mode_command`, `forti profile mode NAME guarded`).
- An unknown profile, or several and no default, lists the profiles in `details.profiles`.
- A profile whose client certificate or CA bundle file is missing is reported before any request,
  with what is missing (`details.missing`, `details.path`), instead of a TLS failure.
- Profiles that use forti-cli 0.3's named client certificates (`~/.forti/certs/<name>/`, issued by
  the forti CA) work as before; a certificate can now be shared by several profiles.

## Installing

- Homebrew installs pour a bottle on Apple silicon instead of compiling, and `forti-cli` now installs
  forti-mcp with it (Homebrew dependency, Debian Recommends). forti-mcp alone still works.
- Building from source is fast: the engine's catalog comes from forti-api 1.1's runtime schema, so
  no translation unit needs more than about 1.2 GB, and a full build with the tests takes about
  10 seconds on an Apple silicon Mac.
