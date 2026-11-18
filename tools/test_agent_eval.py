"""Integrity tests for the editor-agent logger (agent_eval.py), without a desktop or a running editor.

The mock replaces only the TCP client; role enforcement, evidence validation,
persistent numbering and budgets exercise the actual experiment harness.
"""
import json
import io
from pathlib import Path
import tempfile
import unittest

import agent_eval as ae


class Client:
    def __init__(self, port):
        self.port = port

    def call(self, op, **args):
        if op == "view_screenshot":
            Path(args["file"]).write_bytes(b"screenshot fixture")
        return {"ok": True, "payload": "x" * 4000, "time": 2.0}

    def close(self):
        pass


class HarnessTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.run = Path(self.directory.name) / "run"
        ae.initialize(self.run, "ui", 47801, "T1", 20)

    def call(self, op, args=None, phase="build"):
        return ae.perform_call(self.run, op, args or {}, "expected feedback", "visible screen",
                               phase, client_factory=Client)

    def api_run(self, budget=20):
        self.run = Path(self.directory.name) / "api"
        ae.initialize(self.run, "api", 47802, "T1", budget)

    def report(self, evidence):
        report = dict(agent="ui-human-tester", task="T1", outcome="done", findings=[], top3=[],
                      mast={key: "ok" for key in ae.MAST_KEYS}, criteria=[evidence])
        ae.write_json(self.run / "report.json", report)
        return report

    def ui_evidence(self):
        self.call("view_screenshot", {"file": "view.png", "screen": True}, "verify")
        return dict(name="rotation", expected="disc rotates", seen="disc changed orientation",
                    **{"pass": True}, actions=[1], shot="view.png")

    def test_init_rejects_reuse(self):
        with self.assertRaisesRegex(ValueError, "empty"):
            ae.initialize(self.run, "ui", 47801, "T1", 20)

    def test_denied_semantic_call_is_logged_without_connecting(self):
        def no_connection(port):
            self.fail("forbidden operation attempted a connection")
        row = ae.perform_call(self.run, "scene_add", {}, "add shape", "view", client_factory=no_connection)
        self.assertFalse(row["ok"])
        self.assertTrue(row["denied"])
        self.assertFalse(row["sent"])
        self.assertEqual(ae.journal(self.run)[0]["op"], "scene_add")

    def test_ui_window_rules_and_shortcuts(self):
        invalid = [("ui_pointer", {"action": "click", "space": "view"}),
                   ("ui_pointer", {"action": "click", "space": "window", "cursor": True}),
                   ("ui_key", {"key": "Enter"}), ("ui_trigger", {"text": "Пуск"}),
                   ("view_screenshot", {"file": "v.png"})]
        for op, args in invalid:
            with self.subTest(op=op, args=args):
                self.assertTrue(self.call(op, args)["denied"])

    def test_screenshot_traversal_and_symlink_escape(self):
        self.assertTrue(self.call("view_screenshot", {"file": "../outside.png", "screen": True})["denied"])
        outside = Path(self.directory.name) / "outside"
        outside.mkdir()
        (self.run / "escape").symlink_to(outside, target_is_directory=True)
        self.assertTrue(self.call("view_screenshot", {"file": "escape/file.png", "screen": True})["denied"])

    def test_counters_persist_and_answers_are_not_truncated(self):
        self.call("ui_key", {"key": "Tab", "window": True})
        row = self.call("ui_type", {"text": "12,5"})
        self.assertEqual(row["n"], 2)
        self.assertEqual(len(ae.journal(self.run)[1]["answer"]["payload"]), 4000)

    def test_budget_blocks_further_calls_and_counters(self):
        self.api_run(budget=1)
        self.call("ops_list")
        with self.assertRaisesRegex(ValueError, "budget exhausted"):
            self.call("scene_list")
        self.assertEqual(len(ae.journal(self.run)), 1)

    def test_api_rejects_ui_and_hidden_batches(self):
        self.api_run()
        for op, args in [("ui_key", {"window": True}), ("batch", {}),
                         ("scene_batch", {}), ("scene_add", {"operations": ["scene_add"]})]:
            with self.subTest(op=op):
                self.assertTrue(self.call(op, args)["denied"])

    def test_api_accepts_discoverable_semantic_tools(self):
        self.api_run()
        for op in ("source_add", "material_set", "group_add", "instrument_add", "edit_undo", "view_frame"):
            with self.subTest(op=op):
                self.assertTrue(self.call(op)["ok"])

    def test_verify_ui_hand_is_separate_from_authoring(self):
        self.call("ui_key", {"key": "Tab", "window": True}, "verify")
        result = ae.metrics(self.run)
        self.assertEqual(result["categories"], {"verify_action": 1})
        self.assertEqual(result["hand_actions"], 1)

    def test_metrics_separate_physical_input_and_observation(self):
        self.call("view_screenshot", {"file": "view.png", "screen": True})
        self.call("ui_pointer", {"action": "move", "space": "window", "x": 0, "y": 0})
        self.call("ui_pointer", {"action": "click", "space": "window", "x": 0, "y": 0})
        self.call("ui_key", {"key": "Tab", "window": True})
        self.call("ui_type", {"text": "тест"})
        result = ae.metrics(self.run)
        self.assertEqual(result["categories"], {"observation": 1, "pointer_move": 1, "authoring": 3})
        self.assertEqual((result["clicks"], result["keys"], result["typed_characters"]), (1, 1, 4))
        self.assertGreaterEqual(result["elapsed_seconds"], 0)

    def test_empty_report_does_not_pass(self):
        ae.write_json(self.run / "report.json", {})
        result = ae.check(self.run)
        self.assertFalse(result["contract_ok"])
        self.assertFalse(result["physics_verified"])

    def test_complete_ui_contract_is_not_physics_verification(self):
        self.report(self.ui_evidence())
        result = ae.check(self.run)
        self.assertTrue(result["contract_ok"], result["problems"])
        self.assertFalse(result["physics_verified"])

    def test_missing_evidence_reference_fails(self):
        evidence = self.ui_evidence()
        evidence["actions"] = [999]
        self.report(evidence)
        result = ae.check(self.run)
        self.assertFalse(result["contract_ok"])

    def test_missing_screenshot_fails(self):
        evidence = self.ui_evidence()
        (self.run / "view.png").unlink()
        self.report(evidence)
        self.assertFalse(ae.check(self.run)["contract_ok"])

    def test_done_with_failed_criterion_fails(self):
        evidence = self.ui_evidence()
        evidence["pass"] = False
        self.report(evidence)
        self.assertFalse(ae.check(self.run)["contract_ok"])

    def test_api_criterion_requires_measurement_and_verify_phase(self):
        self.api_run()
        self.call("sim_state", phase="build")
        evidence = dict(name="speed", expected="0.5236", calls=[1], **{"pass": True})
        report = self.report(evidence)
        report["agent"] = "api-agent-tester"
        ae.write_json(self.run / "report.json", report)
        result = ae.check(self.run)
        self.assertTrue(any("measured" in p for p in result["problems"]))
        self.assertTrue(any("verification-phase" in p for p in result["problems"]))

    def test_honest_unavailable_api_measurement_can_report_blocked(self):
        self.api_run()
        self.call("sim_state", phase="verify")
        evidence = dict(name="speed", expected="0.5236", measured=None, calls=[1], **{"pass": False})
        report = self.report(evidence)
        report.update(agent="api-agent-tester", outcome="blocked")
        ae.write_json(self.run / "report.json", report)
        result = ae.check(self.run)
        self.assertTrue(result["contract_ok"], result["problems"])

    def test_finding_requires_existing_reference_and_screenshot(self):
        report = self.report(self.ui_evidence())
        report["findings"] = [dict(id="F1", did="click", expected="dialog", got="no change", actions=[999])]
        ae.write_json(self.run / "report.json", report)
        self.assertFalse(ae.check(self.run)["contract_ok"])

    def test_self_report_cannot_hide_role_violation(self):
        self.call("scene_add")
        evidence = self.ui_evidence()
        evidence["actions"] = [2]
        self.report(evidence)
        result = ae.check(self.run)
        self.assertTrue(any("role violation" in p for p in result["problems"]))


class ServingTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.run = Path(self.directory.name) / "run"
        ae.initialize(self.run, "ui", 47801, "T1", 20)
        self.connections, self.calls, self.closes = 0, [], 0

    def factory(self, port):
        owner = self
        owner.connections += 1
        class TrackedClient(Client):
            def call(self, op, **args):
                owner.calls.append(op)
                return super().call(op, **args)
            def close(self):
                owner.closes += 1
        return TrackedClient(port)

    def request(self, op="ui_key", args=None):
        return dict(op=op, args=args or {"key": "Tab", "window": True}, expect="field commits", see="visible field")

    def serve(self, requests):
        data = "".join((json.dumps(request) if not isinstance(request, str) else request) + "\n" for request in requests)
        output = io.StringIO()
        code = ae.serve(self.run, io.StringIO(data), output, self.factory)
        return code, [json.loads(line) for line in output.getvalue().splitlines()]

    def test_two_calls_share_one_client_and_journal_sequence(self):
        code, replies = self.serve([self.request(), self.request("ui_type", {"text": "12,5"})])
        self.assertEqual(code, 0)
        self.assertTrue(replies[0]["ready"])
        self.assertEqual([r["n"] for r in replies[1:]], [1, 2])
        self.assertEqual([r["n"] for r in ae.journal(self.run)], [1, 2])
        self.assertEqual(len(ae.journal(self.run)[0]["answer"]["payload"]), 4000)
        self.assertEqual((self.connections, self.closes), (1, 1))

    def test_forbidden_ui_semantic_call_is_denied_and_logged(self):
        code, replies = self.serve([self.request("scene_add", {"shape": "box"})])
        self.assertEqual(code, 0)
        self.assertTrue(replies[1]["denied"])
        self.assertFalse(replies[1]["sent"])
        self.assertEqual(ae.journal(self.run)[0]["n"], 1)
        self.assertEqual(self.connections, 0)

    def test_malformed_or_executable_requests_are_data_only(self):
        invalid = ['__import__("os").system("false")', {"code": "print(1)"},
                   {"op": "ui_key", "args": {}, "expect": "x", "port": 9},
                   'NaN', self.request()]
        code, replies = self.serve(invalid)
        self.assertEqual(code, 0)
        self.assertTrue(all(r["ok"] is False for r in replies[1:5]))
        self.assertEqual(replies[4]["local_errors"], 4)
        self.assertEqual(len(ae.journal(self.run)), 1)
        self.assertEqual(self.calls, ["ui_key"])

    def test_numeric_quit_is_not_boolean_true(self):
        code, replies = self.serve([{"quit": 1}, self.request()])
        self.assertEqual(code, 0)
        self.assertFalse(replies[1]["ok"])
        self.assertEqual(self.calls, ["ui_key"])

    def test_quit_closes_logger_connection_without_editor_operation(self):
        code, replies = self.serve([self.request(), {"quit": True}, self.request()])
        self.assertEqual(code, 0)
        self.assertEqual(replies[-1], {"ok": True, "closed": True})
        self.assertEqual(self.calls, ["ui_key"])
        self.assertEqual(self.closes, 1)

    def test_eof_without_requests_never_connects(self):
        code, replies = self.serve([])
        self.assertEqual(code, 0)
        self.assertEqual(len(replies), 1)
        self.assertEqual((self.connections, self.closes), (0, 0))

    def test_budget_is_same_for_persistent_and_single_calls(self):
        manifest = ae.read_json(self.run / "manifest.json")
        manifest["budget"] = 1
        ae.write_json(self.run / "manifest.json", manifest)
        code, replies = self.serve([self.request(), self.request(), self.request()])
        self.assertEqual(code, 2)
        self.assertEqual(len(ae.journal(self.run)), 1)
        self.assertIn("budget exhausted", replies[-1]["error"])
        self.assertEqual(self.calls, ["ui_key"])


if __name__ == "__main__":
    unittest.main()
