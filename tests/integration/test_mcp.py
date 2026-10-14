#!/usr/bin/env python3
"""End-to-end tests of the real `forti-mcp` binary over stdio, against forti-cli's mock FortiOS
(through libcurl and verified TLS), with the real `forti` binary sharing its state directory for
configuration and guarded-mode approvals.

    FORTI_MCP_BINARY=... FORTI_BINARY=... FORTI_MOCK_DIR=subprojects/forti-cli/tests/mock-fortios \\
        python3 tests/integration/test_mcp.py
"""

from __future__ import annotations

import json
import os
import sys
import unittest

MOCK_DIR = os.environ.get("FORTI_MOCK_DIR", "")
if not MOCK_DIR or not os.path.isfile(os.path.join(MOCK_DIR, "harness.py")):
    print("SKIP: FORTI_MOCK_DIR does not point at forti-cli's tests/mock-fortios", file=sys.stderr)
    sys.exit(77)
sys.path.insert(0, MOCK_DIR)
from harness import API_KEY, Sandbox, binary, require_tools  # noqa: E402
from mock_fortios import BACKUP_MARKER  # noqa: E402

FORTI = FORTI_MCP = ""
TOOLS = ["forti_discover", "forti_read", "forti_plan", "forti_apply"]
ADDRESS = {"op": "create", "resource": "firewall/address",
           "object": {"name": "web", "subnet": "192.0.2.10 255.255.255.255", "comment": "from mcp"}}


class McpTest(unittest.TestCase):
    mode = "guarded"

    def setUp(self) -> None:
        self.sb = Sandbox(forti=FORTI, forti_mcp=FORTI_MCP).__enter__()
        self.addCleanup(self.sb.__exit__, None, None, None)
        self.mock = self.sb.mock
        self.transcripts: list[str] = []
        configured = self.sb.configure(mode=self.mode)
        self.assertEqual(configured.code, 0, configured.err)

    def tearDown(self) -> None:
        for text in self.transcripts + [self.sb.all_files_text()]:
            self.assertNotIn(API_KEY, text)

    def session(self, *args: str):  # type: ignore[no-untyped-def]
        session = self.sb.mcp(*args)
        original = session.receive

        def receive(timeout: float = 60):  # type: ignore[no-untyped-def]
            message = original(timeout)
            self.transcripts.append(json.dumps(message))
            return message
        session.receive = receive  # type: ignore[method-assign]

        def close() -> None:
            code = session.close()
            self.transcripts.append(session.stderr)
            self.assertEqual(code, 0, session.stderr)
        self.addCleanup(close)
        return session

    def cli(self, *args: str, expect: int = 0):  # type: ignore[no-untyped-def]
        result = self.sb.forti(*args)
        self.transcripts += [result.out, result.err]
        self.assertEqual(result.code, expect, f"forti {' '.join(args)}: {result.out} {result.err}")
        return result

    def ok(self, result):  # type: ignore[no-untyped-def]
        self.assertFalse(result["isError"], json.dumps(result)[:2000])
        envelope = result["structuredContent"]
        self.assertTrue(envelope["ok"])
        # The text block carries the same JSON as structuredContent.
        self.assertEqual(json.loads(result["content"][0]["text"]), envelope)
        return envelope


class Protocol(McpTest):
    def test_modern_stateless_requests(self) -> None:
        mcp = self.session()
        discover = mcp.request("server/discover", modern=True)["result"]
        self.assertEqual(discover["resultType"], "complete")
        self.assertEqual(discover["_meta"]["io.modelcontextprotocol/serverInfo"]["name"], "forti-mcp")
        listed = mcp.request("tools/list", modern=True)["result"]
        self.assertEqual([t["name"] for t in listed["tools"]], TOOLS)
        result = mcp.call("forti_discover", {"what": "capabilities"}, modern=True)
        data = self.ok(result)["data"]
        self.assertEqual(data["device"]["serial"], "FGT60FTK20000000")
        self.assertTrue(data["schema_matches_device"])
        self.assertEqual(data["mode"], "guarded")
        # Without _meta and without initialize, a request is refused.
        refused = mcp.request("tools/list")
        self.assertEqual(refused["error"]["code"], -32602)

    def test_legacy_initialize_session(self) -> None:
        mcp = self.session()
        init = mcp.initialize("2025-06-18")["result"]
        self.assertEqual(init["protocolVersion"], "2025-06-18")
        self.assertIn("instructions", init)
        self.assertEqual(mcp.request("ping")["result"], {})
        self.assertEqual([t["name"] for t in mcp.request("tools/list")["result"]["tools"]], TOOLS)
        devices = self.ok(mcp.call("forti_discover", {"what": "devices"}))["data"]
        self.assertEqual(devices[0]["name"], "lab")
        self.assertNotIn(API_KEY, json.dumps(devices))

    def test_read_only_flag_hides_and_refuses_changes(self) -> None:
        mcp = self.session("--read-only")
        mcp.initialize()
        self.assertEqual([t["name"] for t in mcp.request("tools/list")["result"]["tools"]], TOOLS[:2])
        refused = mcp.request("tools/call", {"name": "forti_plan", "arguments": {"operations": [ADDRESS]}})
        self.assertIn("error", refused)
        self.assertEqual(self.mock.writes(), [])


class Tools(McpTest):
    def test_discover_live_schema(self) -> None:
        mcp = self.session()
        mcp.initialize()
        self.mock.clear_requests()
        found = self.ok(mcp.call("forti_discover", {"what": "resources", "query": "firewall/address"}))
        self.assertIn("firewall/address", [r["resource"] for r in found["data"]])
        self.assertEqual(self.mock.requests, [], "catalog search is offline")
        live = self.ok(mcp.call("forti_discover", {"what": "resource", "resource": "firewall/address",
                                                   "fields": ["type"], "live": True}))
        self.assertEqual(live["data"]["fields"][0]["options"], ["ipmask", "iprange", "fqdn", "geography", "wildcard"])
        self.assertEqual([r.api for r in self.mock.requests], ["cmdb/firewall/address"])

    def test_read_is_bounded_by_the_byte_budget(self) -> None:
        self.mock.seed_addresses(1000)
        mcp = self.session()
        mcp.initialize()
        first = self.ok(mcp.call("forti_read", {"resource": "firewall/address", "count": 1000}))
        text = json.dumps(first, separators=(",", ":"))
        self.assertLessEqual(len(text.encode()), 48 * 1024)
        meta = first["meta"]
        self.assertTrue(meta["truncated"])
        kept = meta["returned"]
        self.assertGreater(kept, 10)
        self.assertLess(kept, 1000)
        self.assertEqual(meta["next_start"], kept)
        self.assertEqual(len(first["data"]), kept)
        # The device was asked for 1000; continuation pages from next_start.
        self.assertIn(("count", "1000"), self.mock.requests[-1].query)
        second = self.ok(mcp.call("forti_read", {"resource": "firewall/address", "start": kept, "count": 5}))
        self.assertEqual(second["data"][0]["name"], f"host-{kept:05d}")

        small = self.session("--max-bytes", "4096")
        small.initialize()
        bounded = self.ok(small.call("forti_read", {"resource": "firewall/address", "count": 200}))
        self.assertLessEqual(len(json.dumps(bounded, separators=(",", ":")).encode()), 4096)

    def test_read_errors_are_tool_errors(self) -> None:
        mcp = self.session()
        mcp.initialize()
        missing = mcp.call("forti_read", {"resource": "firewall/address", "key": "nope"})
        self.assertTrue(missing["isError"])
        self.assertEqual(missing["structuredContent"]["error"]["code"], "not_found")
        bad = mcp.call("forti_read", {"resource": "firewall/address", "nosuch": 1})
        self.assertTrue(bad["isError"])
        self.assertEqual(bad["structuredContent"]["error"]["code"], "usage")


class Guarded(McpTest):
    mode = "guarded"

    def test_plan_in_mcp_approve_in_cli_apply_in_mcp(self) -> None:
        mcp = self.session()
        mcp.initialize()
        planned = self.ok(mcp.call("forti_plan", {"operations": [ADDRESS]}))["data"]
        plan_id = planned["id"]
        self.assertEqual(self.mock.writes(), [])

        # The CLI sees the plan MCP stored (one state directory).
        listed = json.loads(self.cli("plan", "list", "--json").out)["data"]
        self.assertIn(plan_id, [p["id"] for p in listed])
        shown = json.loads(self.cli("plan", "show", plan_id, "--json").out)["data"]
        self.assertEqual(shown["digest"], planned["digest"])

        refused = mcp.call("forti_apply", {"plan_id": plan_id})
        self.assertTrue(refused["isError"])
        error = refused["structuredContent"]["error"]
        self.assertEqual(error["code"], "approval_required")
        command = error["details"]["approve_command"]
        self.assertEqual(command, f"forti plan approve {plan_id}")
        self.assertIn(command, refused["content"][-1]["text"])
        self.assertEqual(self.mock.writes(), [])

        # The human runs the command (here: the real CLI, same state dir) while the server keeps running.
        self.cli(*command.split()[1:], "--yes")
        applied = self.ok(mcp.call("forti_apply", {"plan_id": plan_id}))["data"]
        self.assertEqual(applied["verified"], 1)
        self.assertEqual(self.mock.table("firewall/address")["web"]["comment"], "from mcp")
        self.assertEqual([(w.method, w.api) for w in self.mock.writes()], [("POST", "cmdb/firewall/address")])
        self.assertEqual(self.mock.writes()[0].json(), planned["operations"][0]["wire"]["body"])

        # The audit log is shared too, and says the approval came from the CLI.
        events = [(e["event"], e.get("outcome")) for e in self.sb.audit()]
        self.assertIn(("apply", "approval_required"), events)
        self.assertEqual(events[-1], ("apply", "ok"))
        self.assertEqual(self.cli("plan", "show", plan_id, "--json").out.count('"applied"') >= 1, True)

    def test_mcp_cannot_approve(self) -> None:
        mcp = self.session()
        mcp.initialize()
        tools = mcp.request("tools/list")["result"]["tools"]
        self.assertFalse(any("approve" in t["name"] for t in tools))
        apply_schema = next(t for t in tools if t["name"] == "forti_apply")["inputSchema"]
        self.assertEqual(sorted(apply_schema["properties"]), ["plan_id"])


class Autonomous(McpTest):
    mode = "autonomous"

    def test_apply_directly_and_cli_rolls_back(self) -> None:
        self.mock.add_address("old", comment="before")
        mcp = self.session()
        mcp.initialize()
        planned = self.ok(mcp.call("forti_plan", {"operations": [
            ADDRESS, {"op": "update", "resource": "firewall/address", "key": "old", "set": {"comment": "after"}}]}))["data"]
        applied = self.ok(mcp.call("forti_apply", {"plan_id": planned["id"]}))["data"]
        self.assertEqual([op["status"] for op in applied["operations"]], ["verified", "verified"])
        self.assertEqual(self.mock.table("firewall/address")["old"]["comment"], "after")
        # The CLI applies the rollback plan MCP's apply wrote.
        self.cli("apply", applied["rollback"]["plan_id"])
        self.assertEqual(sorted(self.mock.table("firewall/address")), ["old"])
        self.assertEqual(self.mock.table("firewall/address")["old"]["comment"], "before")

    def test_critical_apply_backs_up_privately(self) -> None:
        mcp = self.session()
        mcp.initialize()
        planned = self.ok(mcp.call("forti_plan", {"operations": [
            {"op": "update", "resource": "system/global", "set": {"hostname": "fw-mcp"}}]}))["data"]
        self.assertEqual(planned["risk"], "critical")
        applied = self.ok(mcp.call("forti_apply", {"plan_id": planned["id"]}))["data"]
        self.assertEqual(applied["backup"]["status"], "saved")
        self.assertEqual(self.mock.singleton("system/global")["hostname"], "fw-mcp")
        backup = self.sb.state / "backups" / f"{planned['id']}.conf"
        self.assertIn(BACKUP_MARKER, backup.read_text())
        self.assertNotIn(BACKUP_MARKER, "\n".join(self.transcripts))

    def test_cli_plan_mcp_apply_and_drift(self) -> None:
        self.mock.add_address("old", comment="before")
        planned = json.loads(self.cli("plan", "set", "firewall/address", "old", "comment=cli", "--json").out)["data"]
        self.mock.set_attr("firewall/address", "old", "comment", "drifted")
        mcp = self.session()
        mcp.initialize()
        stale = mcp.call("forti_apply", {"plan_id": planned["id"]})
        self.assertTrue(stale["isError"])
        self.assertEqual(stale["structuredContent"]["error"]["code"], "stale_plan")
        self.assertEqual(self.mock.writes(), [])


def main() -> int:
    global FORTI, FORTI_MCP
    require_tools()
    FORTI = binary("FORTI_BINARY")
    FORTI_MCP = binary("FORTI_MCP_BINARY")
    program = unittest.main(exit=False, verbosity=2)
    return 0 if program.result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
