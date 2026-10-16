<!--
vl-release staged changelog: package-facing changes for the NEXT release.
Maintained continuously while working; `vlr prepare` renders it into debian/changelog
and resets this file to this template.

Format: one "- " bullet per change (concise, technical). Indent continuation lines.
Optional "## Section" headings group bullets; if used, every bullet must be under one.
One level of nested "  - " detail bullets is allowed. Consolidate; don't paste commit logs.
-->
- Depend on valkyrianlabs-archive-keyring so renewed APT signing-key metadata reaches existing installs
  through upgrades (it owns /usr/share/keyrings/valkyrianlabs.gpg and links an unmodified
  /etc/apt/keyrings/valkyrianlabs.gpg to it).
- Engine: forti-cli v0.4.1 (c9cf77c): approvals bound to the digest shown, audit events with actor,
  summary and approver, `forti approve` in approval details.
- forti_apply takes operations (plan and apply in one call) or plan_id; the result carries the plan
  (id, summary, risk, digest, diffs). forti_plan stays read-only.
- Guarded mode: in-chat approval through MCP form elicitation. Legacy era: elicitation/create during
  the tools/call (pings answered, requests queued, cancellation and EOF abandon). 2026-07-28:
  input_required with an HMAC-SHA256-sealed, single-use, 15-minute requestState. An approval is
  recorded (approved_by mcp:<client>:elicitation) only from an accept with approve=true, and only
  for the digest that was shown.
- Without elicitation, approval_required relays `forti approve <id>` and `forti approve --watch`.
  The engine actor is mcp:<client>.
- Build: hidden symbol visibility (startup ~25 ms to ~6 ms).
- Release: the clean-install smoke test adds the ValkyrianLabs repository (the keyring dependency).
