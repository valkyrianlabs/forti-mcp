#!/usr/bin/env bash
# Smoke test of a built or installed `forti-mcp`, offline and without touching the caller's config:
#
#   ci/smoke-test.sh [PATH-TO-FORTI-MCP]     (default: forti-mcp on PATH)
#
# Checks `forti-mcp --version` (including the engine revision it was built from), then plays an MCP
# client over stdio: the `initialize` handshake, `tools/list` and an offline `forti_discover` resource
# search (the catalog is compiled in; no profile, device or network is needed). Checks that nothing
# was written to the forti home (or anywhere in HOME) and that a bad option exits 2. POSIX tools only: it
# also runs in a stock ubuntu:24.04 with nothing but the .deb installed.
set -euo pipefail

mcp="${1:-forti-mcp}"
scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT
export HOME="$scratch/home" FORTI_HOME="$scratch/forti"
mkdir -p "$HOME"

fail() {
  echo "smoke-test: $*" >&2
  exit 1
}

version="$("$mcp" --version)"
echo "$version"
case "$version" in
  "forti-mcp "[0-9]*" (engine forti-cli "[0-9a-f]*", forti-api "[0-9]*")") ;;
  *) fail "unexpected --version output" ;;
esac

cat > "$scratch/requests.jsonl" <<'JSON'
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"smoke-test","version":"1"}}}
{"jsonrpc":"2.0","method":"notifications/initialized"}
{"jsonrpc":"2.0","id":2,"method":"tools/list"}
{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"forti_discover","arguments":{"what":"resources","query":"firewall/address"}}}
JSON
"$mcp" < "$scratch/requests.jsonl" > "$scratch/responses.jsonl" 2> "$scratch/stderr" || { cat "$scratch/stderr" >&2; fail "forti-mcp exited non-zero"; }
responses="$(wc -l < "$scratch/responses.jsonl" | tr -d ' ')"
[ "$responses" -eq 3 ] || { cat "$scratch/responses.jsonl" >&2; fail "expected 3 responses, got $responses"; }
grep -q '"protocolVersion":"2025-11-25"' "$scratch/responses.jsonl" || fail "initialize did not negotiate 2025-11-25"
for tool in forti_discover forti_read forti_plan forti_apply; do
  grep -q "\"name\":\"$tool\"" "$scratch/responses.jsonl" || fail "tools/list does not offer $tool"
done
sed -n 3p "$scratch/responses.jsonl" | grep -q '"isError":false' || fail "the offline forti_discover failed"
sed -n 3p "$scratch/responses.jsonl" | grep -q '"resource":"firewall/address"' || fail "forti_discover did not find firewall/address"
[ ! -s "$scratch/stderr" ] || { cat "$scratch/stderr" >&2; fail "forti-mcp wrote to stderr"; }

# Usage errors exit with 2, not a crash.
status=0
"$mcp" --no-such-option > /dev/null 2>&1 || status=$?
[ "$status" -eq 2 ] || fail "an unknown option exited $status, expected 2"

if [ -n "$(find "$FORTI_HOME" "$HOME" -type f 2>/dev/null)" ]; then
  fail "an offline session wrote files"
fi
echo "smoke-test: ok"
