<!--
vl-release staged changelog: package-facing changes for the NEXT release.
Maintained continuously while working; `vlr prepare` renders it into debian/changelog
and resets this file to this template.

Format: one "- " bullet per change (concise, technical). Indent continuation lines.
Optional "## Section" headings group bullets; if used, every bullet must be under one.
One level of nested "  - " detail bullets is allowed. Consolidate; don't paste commit logs.
-->
- Child tables: forti_read takes child and child_key; operations take parent and child (strict
  schemas, additionalProperties false); the approval prompt names parent, child and entry, and says a
  child delete leaves the other entries alone. Unit, e2e and integration tests (mock FortiOS child paths).
- Engine: forti-cli v0.5.0 (child tables, webhook header redaction); requires forti-api >= 1.2.0.
- Homebrew: the bottle block aligns tags and digests and lists ARM bottles first (brew style).

