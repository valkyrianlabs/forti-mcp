<!--
vl-release staged changelog: package-facing changes for the NEXT release.
Maintained continuously while working; `vlr prepare` renders it into debian/changelog
and resets this file to this template.

Format: one "- " bullet per change (concise, technical). Indent continuation lines.
Optional "## Section" headings group bullets; if used, every bullet must be under one.
One level of nested "  - " detail bullets is allowed. Consolidate; don't paste commit logs.
-->
- Engine: forti-cli v0.3.1 (d0e83a8), on forti-api 1.1.0's runtime schema: no
  typed shards; resource lookup by CLI words, unique last word and keyed path;
  attribute suggestions; profile lists and mode commands in error details;
  missing certificate files caught before any request.
- Tool descriptions of forti_read / forti_discover name the new spellings.
- Packaging: Homebrew bottle built and published with each release; build
  jobs budgeted at 1.2 GB; libforti-api-dev (>= 1.1.0).

