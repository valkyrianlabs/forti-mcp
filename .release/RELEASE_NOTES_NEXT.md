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

