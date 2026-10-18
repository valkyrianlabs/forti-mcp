<!--
vl-release staged release notes: the user-facing story of the NEXT release.
Maintained continuously while working; `vlr prepare` promotes it into the release notes
history (and the GitHub release) and resets this file to this template.

Format: the first line is "# <release title>" WITHOUT a version number (vlr adds it).
Everything after the title is the Markdown release body. Describe the resulting behavior
for users and operators; keep it representative of what actually ships.
-->
# Engine forti-cli 0.7.0: plan retention; the approval prompt shows stored bodies

#### Plans no longer pile up

forti-mcp now builds on forti-cli 0.7.0's engine:

- Once an apply has started executing a plan (whatever the outcome), the plan file and its approval
  records are deleted. A small marker stays, so `forti_apply` with that `plan_id` again is refused as
  **already applied** (`policy_denied`, reason `already_applied`), not "not found". A plan refused
  before anything was sent (stale, approval required, read-only, tampered) is kept for a retry.
- Plans never applied, rollback plans, the markers and approval records are deleted after the plan
  retention period: 30 days, or what a person sets with `forti home retention DAYS` (1 to 3650). No
  tool can change it. The purge runs on every plan and apply and never during an apply.
- Through the same engine: monitor action bodies over 64 KiB are stored once by their SHA-256 instead
  of inside the plan, and an action's apply record keeps the device's reply; a command the device
  reports as failed inside a successful reply fails the operation (forti-cli 0.6.0).

#### Approval prompt

- A monitor action whose body the engine stored by digest showed `request body: (none)` in the in-chat
  approval prompt. It now shows the body's size and SHA-256, for example
  `request body: 20.0 MiB (20971520 bytes), stored by digest, sha256 9b1f…`, and never the content.
  Inline bodies are shown as before.
- A `forti_apply` with `operations` approved through the modern-era retry kept its `plan` summary in
  the result: it is now taken before the apply, which deletes the plan.
