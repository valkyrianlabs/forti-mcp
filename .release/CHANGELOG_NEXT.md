<!--
vl-release staged changelog: package-facing changes for the NEXT release.
Maintained continuously while working; `vlr prepare` renders it into debian/changelog
and resets this file to this template.

Format: one "- " bullet per change (concise, technical). Indent continuation lines.
Optional "## Section" headings group bullets; if used, every bullet must be under one.
One level of nested "  - " detail bullets is allowed. Consolidate; don't paste commit logs.
-->
- Engine: forti-cli v0.7.0 (plan retention: applied plans deleted, marker
  refuses a repeat as already_applied; plans, markers and approvals purged
  after plan_retention_days, default 30). Also brings forti-cli 0.6.0's
  content-addressed action bodies and action replies.
- Approval prompt: an action whose wire carries body_ref {sha256, bytes} shows
  its size and SHA-256 instead of "(none)"; inline bodies unchanged.
- forti_apply (modern-era retry with operations): the plan brief is taken
  before the apply, since the engine now deletes applied plans.
- Docs: plan retention, stored bodies in the approval prompt.
