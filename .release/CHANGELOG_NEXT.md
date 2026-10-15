<!--
vl-release staged changelog: package-facing changes for the NEXT release.
Maintained continuously while working; `vlr prepare` renders it into debian/changelog
and resets this file to this template.

Format: one "- " bullet per change (concise, technical). Indent continuation lines.
Optional "## Section" headings group bullets; if used, every bullet must be under one.
One level of nested "  - " detail bullets is allowed. Consolidate; don't paste commit logs.
-->
- Documentation: the docs package (docs/, docs set forti-mcp) is authored in
  Payload Markdown, with directives for tabs (install methods, client
  registration), steps, callouts (safety limits, recovery), cards, badges,
  buttons and per-page tables of contents; documented behavior unchanged.
