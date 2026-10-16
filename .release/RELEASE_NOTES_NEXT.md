<!--
vl-release staged release notes: the user-facing story of the NEXT release.
Maintained continuously while working; `vlr prepare` promotes it into the release notes
history (and the GitHub release) and resets this file to this template.

Format: the first line is "# <release title>" WITHOUT a version number (vlr adds it).
Everything after the title is the Markdown release body. Describe the resulting behavior
for users and operators; keep it representative of what actually ships.
-->
# Approve changes in the conversation

forti-mcp 0.4 carries forti-cli 0.4.1's engine and makes a change one tool call with, in guarded
mode, one approval where you already are.

## One call to make a change

`forti_apply` takes the operations themselves and plans and applies them in one call, as the
profile's mode allows; `forti_plan` remains for previewing. The result includes the plan's diff, so
the agent can report exactly what changed.

## Guarded mode asks you in the chat

- With a client that supports MCP elicitation (Claude Code and Codex do), you get one prompt in the
  conversation: the device, mode, risk and every attribute change, with an **Apply these changes**
  checkbox. Approve and the change is applied, verified and reported in the same call. Decline or
  dismiss it and nothing is applied, and the agent is told not to ask again.
- The approval is recorded as `mcp:<client>:elicitation` only from your answer, never from anything
  the model sends, and covers only the plan you were shown: a plan changed after it was shown is
  refused.
- No prompt support? The result names `forti approve <id>` for your terminal, or keep
  `forti approve --watch` running there.

## Also

- Audit records name the client that applied a change (`mcp:claude-code`), so `forti history` shows
  who did what.
- Startup is back to about 6 ms.
- Homebrew pours a bottle on Apple silicon Macs running macOS 26 or later.
- APT packages depend on `valkyrianlabs-archive-keyring`, which keeps the repository signing key
  current through normal upgrades.
