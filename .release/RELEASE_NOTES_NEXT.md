<!--
vl-release staged release notes: the user-facing story of the NEXT release.
Maintained continuously while working; `vlr prepare` promotes it into the release notes
history (and the GitHub release) and resets this file to this template.

Format: the first line is "# <release title>" WITHOUT a version number (vlr adds it).
Everything after the title is the Markdown release body. Describe the resulting behavior
for users and operators; keep it representative of what actually ships.
-->
# One DHCP reservation at a time: child tables

`forti_read` and the operations of `forti_plan` and `forti_apply` take child tables: one table
attribute of an object, such as a DHCP server's `reserved-address`, entry by entry, without
rewriting the rest of the list.

- `forti_read` takes `child` (the table attribute of the object named by `key`) and `child_key`
  (one entry): `{"resource": "system.dhcp/server", "key": 1, "child": "reserved-address"}`.
- Operations take `parent` (the object's key) and `child`; `key` is then the entry's. A numeric id
  left out of `create` is assigned by the device and reported in the result, and the rollback plan
  deletes exactly that entry. Only the one entry is re-checked before writing, so changes to the
  other entries never make the plan stale.
- The in-chat approval prompt names the object, the table attribute and the entry
  (`update system.dhcp/server "1" reserved-address "21"`).
- Webhook header secrets (`http-headers` entries such as `Authorization`) are redacted in results,
  as other secrets are.
- The tool schemas stay strict; every existing call works as before.

Built on forti-cli's engine with child tables (forti-api 1.2.0). The agent skill that comes with
forti-cli (`forti install mcp`) describes them.

