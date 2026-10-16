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
- forti_apply takes operations (plan and apply in one call) or plan_id;
  the result carries the plan (id, summary, risk, digest, diffs).
- Guarded mode: in-chat approval through MCP form elicitation. Legacy era:
  elicitation/create during the tools/call (pings answered, requests queued,
  cancellation and EOF abandon). 2026-07-28: input_required with an
  HMAC-SHA256-sealed, single-use, 15-minute requestState. An approval is
  recorded (approve_plan, approved_by mcp:<client>:elicitation) only from an
  accept with approve=true, and only for the digest that was shown.
- Without elicitation, approval_required relays `forti approve <id>` and
  `forti approve --watch`. The engine actor is mcp:<client>.
