#!/usr/bin/env python3
"""End-to-end tests of the real `forti-mcp` binary over stdio, against forti-cli's mock FortiOS
(through libcurl and verified TLS), with the real `forti` binary sharing its state directory for
configuration and terminal approvals. In-chat approval (MCP elicitation) is driven over the same pipes.

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
        # The harness points FORTI_HOME (and every FORTI_*_DIR) at its temporary directory, so neither
        # binary ever resolves the real forti home (~/.forti).
        self.assertTrue(self.sb.env.get("FORTI_HOME", "").startswith(str(self.sb.root)), self.sb.env)
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


MODERN_META = {"io.modelcontextprotocol/protocolVersion": "2026-07-28",
               "io.modelcontextprotocol/clientCapabilities": {"elicitation": {"form": {}}},
               "io.modelcontextprotocol/clientInfo": {"name": "forti-it", "version": "1"}}


class Guarded(McpTest):
    mode = "guarded"

    def approve_in_cli(self, plan_id: str) -> None:
        """The human approves in a terminal. `forti approve <id>` is the command forti-mcp relays; a forti
        built before it existed only has `forti plan approve <id>`."""
        result = self.sb.forti("approve", plan_id, "--yes")
        self.transcripts += [result.out, result.err]
        if result.code == 2:
            self.cli("plan", "approve", plan_id, "--yes")
        else:
            self.assertEqual(result.code, 0, result.err)

    def test_without_elicitation_the_terminal_command_is_relayed(self) -> None:
        mcp = self.session()
        mcp.initialize()   # no elicitation capability
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
        self.assertEqual(command, f"forti approve {plan_id}")
        self.assertIn(command, refused["content"][-1]["text"])
        self.assertIn("forti approve --watch", refused["content"][-1]["text"])
        self.assertEqual(self.mock.writes(), [])

        # The human approves in a terminal (the real CLI, same state dir) while the server keeps running.
        self.approve_in_cli(plan_id)
        applied = self.ok(mcp.call("forti_apply", {"plan_id": plan_id}))["data"]
        self.assertEqual(applied["verified"], 1)
        self.assertEqual(self.mock.table("firewall/address")["web"]["comment"], "from mcp")
        self.assertEqual([(w.method, w.api) for w in self.mock.writes()], [("POST", "cmdb/firewall/address")])
        self.assertEqual(self.mock.writes()[0].json(), planned["operations"][0]["wire"]["body"])

        events = [(e["event"], e.get("outcome")) for e in self.sb.audit()]
        self.assertIn(("apply", "approval_required"), events)
        self.assertEqual(events[-1], ("apply", "ok"))
        self.assertEqual(self.sb.audit()[-1]["actor"], "mcp:forti-it")

    def test_legacy_in_chat_approval(self) -> None:
        mcp = self.session()
        init = mcp.request("initialize", {"protocolVersion": "2025-11-25", "capabilities": {"elicitation": {}},
                                          "clientInfo": {"name": "forti-it", "version": "1"}})
        self.assertIn("result", init)
        mcp.send({"jsonrpc": "2.0", "method": "notifications/initialized"})
        mcp.send({"jsonrpc": "2.0", "id": 50, "method": "tools/call",
                  "params": {"name": "forti_apply", "arguments": {"operations": [ADDRESS]}}})
        ask = mcp.receive()
        self.transcripts.append(json.dumps(ask))
        self.assertEqual(ask["method"], "elicitation/create")
        message = ask["params"]["message"]
        self.assertIn('create firewall/address "web"', message)
        self.assertIn("comment = from mcp", message)
        self.assertEqual(self.mock.writes(), [])
        # A ping while the person decides is answered at once.
        mcp.send({"jsonrpc": "2.0", "id": 51, "method": "ping"})
        self.assertEqual(mcp.receive()["id"], 51)
        mcp.send({"jsonrpc": "2.0", "id": ask["id"], "result": {"action": "accept", "content": {"approve": True}}})
        response = mcp.receive()
        self.assertEqual(response["id"], 50)
        data = self.ok(response["result"])["data"]
        self.assertEqual(data["verified"], 1)
        self.assertEqual(data["approval"]["approved_by"], "mcp:forti-it:elicitation")
        self.assertEqual(self.mock.table("firewall/address")["web"]["comment"], "from mcp")
        applied = [e for e in self.sb.audit() if e["event"] == "apply"][-1]
        self.assertEqual((applied["outcome"], applied["approved_by"]), ("ok", "mcp:forti-it:elicitation"))

    def test_legacy_decline_applies_nothing(self) -> None:
        mcp = self.session()
        mcp.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {"elicitation": {}},
                                   "clientInfo": {"name": "forti-it", "version": "1"}})
        mcp.send({"jsonrpc": "2.0", "id": 60, "method": "tools/call",
                  "params": {"name": "forti_apply", "arguments": {"operations": [ADDRESS]}}})
        ask = mcp.receive()
        self.assertNotIn("mode", ask["params"])   # 2025-06-18 predates it
        mcp.send({"jsonrpc": "2.0", "id": ask["id"], "result": {"action": "decline"}})
        result = mcp.receive()["result"]
        self.assertTrue(result["isError"])
        self.assertEqual(result["structuredContent"]["error"]["details"]["reason"], "declined")
        self.assertEqual(self.mock.writes(), [])

    def test_modern_in_chat_approval(self) -> None:
        mcp = self.session()
        arguments = {"operations": [ADDRESS]}
        params = {"_meta": MODERN_META, "name": "forti_apply", "arguments": arguments}
        mcp.send({"jsonrpc": "2.0", "id": 70, "method": "tools/call", "params": params})
        first = mcp.receive()["result"]
        self.assertEqual(first["resultType"], "input_required")
        state = first["requestState"]
        self.assertNotIn("web", state)   # opaque
        self.assertEqual(self.mock.writes(), [])
        retry = dict(params, inputResponses={"approve": {"action": "accept", "content": {"approve": True}}},
                     requestState=state)
        mcp.send({"jsonrpc": "2.0", "id": 71, "method": "tools/call", "params": retry})
        response = mcp.receive()
        self.assertEqual(response["id"], 71)
        data = self.ok(response["result"])["data"]
        self.assertEqual(data["approval"]["approved_by"], "mcp:forti-it:elicitation")
        self.assertEqual(data["plan"]["id"], data["plan_id"])
        self.assertEqual([(w.method, w.api) for w in self.mock.writes()], [("POST", "cmdb/firewall/address")])
        # Replaying the answer does nothing.
        mcp.send({"jsonrpc": "2.0", "id": 72, "method": "tools/call", "params": retry})
        replay = mcp.receive()["result"]
        self.assertEqual(replay["structuredContent"]["error"]["details"]["reason"], "approval_replayed")
        self.assertEqual(len(self.mock.writes()), 1)

    def test_mcp_cannot_approve(self) -> None:
        mcp = self.session()
        mcp.initialize()
        tools = mcp.request("tools/list")["result"]["tools"]
        self.assertFalse(any("approve" in t["name"] for t in tools))
        apply_schema = next(t for t in tools if t["name"] == "forti_apply")["inputSchema"]
        self.assertEqual(sorted(apply_schema["properties"]), ["operations", "plan_id", "profile", "vdom"])
        self.assertFalse(apply_schema["additionalProperties"])


class Autonomous(McpTest):
    mode = "autonomous"

    def test_apply_operations_in_one_call(self) -> None:
        mcp = self.session()
        mcp.initialize()
        data = self.ok(mcp.call("forti_apply", {"operations": [ADDRESS]}))["data"]
        self.assertEqual(data["verified"], 1)
        self.assertEqual(data["plan"]["operations"][0]["resource"], "firewall/address")
        self.assertNotIn("approval", data)
        self.assertEqual(self.mock.table("firewall/address")["web"]["comment"], "from mcp")

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
