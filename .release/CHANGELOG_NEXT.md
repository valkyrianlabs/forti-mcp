<!--
vl-release staged changelog: package-facing changes for the NEXT release.
Maintained continuously while working; `vlr prepare` renders it into debian/changelog
and resets this file to this template.

Format: one "- " bullet per change (concise, technical). Indent continuation lines.
Optional "## Section" headings group bullets; if used, every bullet must be under one.
One level of nested "  - " detail bullets is allowed. Consolidate; don't paste commit logs.
-->

## Server
- Initial release of forti-mcp: /usr/bin/forti-mcp, a stdio MCP server
  (JSON-RPC 2.0, MCP 2026-07-28 stateless plus the 2025-11-25 and 2025-06-18
  initialize lifecycle) with the tools forti_discover, forti_read, forti_plan
  and forti_apply over the FortiOS REST API.
- forti-mcp --version reports what it was built from: the forti-cli engine
  revision and the forti-api version.

## Packaging
- The command engine is compiled in statically from forti-cli's source at the
  revision subprojects/forti-cli.wrap pins; no dependency on the forti-cli
  package or the forti executable.
- Built against the system libforti-api-dev (>= 1.0.1) with
  --wrap-mode=nodownload: the package build fetches nothing.
- Release build profile: -O3 in place of dpkg's -O2, NDEBUG, and section
  garbage collection (-ffunction-sections -fdata-sections, --gc-sections); the
  network-free unit and e2e suites run against that build unless nocheck.
- amd64 only (vl-release supports one architecture per package contract).
