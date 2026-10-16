<!--
vl-release staged changelog: package-facing changes for the NEXT release.
Maintained continuously while working; `vlr prepare` renders it into debian/changelog
and resets this file to this template.

Format: one "- " bullet per change (concise, technical). Indent continuation lines.
Optional "## Section" headings group bullets; if used, every bullet must be under one.
One level of nested "  - " detail bullets is allowed. Consolidate; don't paste commit logs.
-->
- Engine: forti-cli v0.4.2 (468f9e1) on forti-api 1.1.1 (builds on macOS 15 / Xcode 16; schema tables with hidden
  visibility).
- Homebrew bottles for arm64 and Intel, built on macOS 15.
- Recommend (not depend on) valkyrianlabs-archive-keyring; the clean-install smoke test is a standalone
  install again. Release tooling vl-release 0.5.1.

