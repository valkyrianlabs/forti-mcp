<!--
vl-release staged release notes: the user-facing story of the NEXT release.
Maintained continuously while working; `vlr prepare` promotes it into the release notes
history (and the GitHub release) and resets this file to this template.

Format: the first line is "# <release title>" WITHOUT a version number (vlr adds it).
Everything after the title is the Markdown release body. Describe the resulting behavior
for users and operators; keep it representative of what actually ships.
-->
# Documentation in Payload Markdown

The forti-mcp documentation now uses Payload Markdown directives, so the published pages are easier
to scan and follow. The content is the same; only its presentation changed:

- **Installation** puts APT, Homebrew and the source build side by side in tabs.
- **Connecting AI clients** walks through setup as numbered steps, with Claude Code, Codex and
  manual registration in tabs.
- **Safety and modes** calls out what guarded mode does not stop and the limits of recovery after a
  management lockout.
- The overview page maps features and next pages as cards, and the reference pages have a table of
  contents and cleaner tables.

The server itself is unchanged.
