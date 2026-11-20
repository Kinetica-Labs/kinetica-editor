"""The MCP server of the editor (tools/ke_editor_mcp.py), without an editor: each operation is a tool with MCP's
annotations from its kind, a plugin's own wiring is not offered, a profile (KE_MCP_FAMILIES) asks the editor for its
families only and says so in the instructions, a refused call is an MCP error."""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ke_editor  # noqa: E402
import ke_editor_mcp as mcp  # noqa: E402

OPS = [{"name": "scene_list", "help": "The objects", "kind": "query", "schema": {"type": "object"}},
       {"name": "scene_add", "help": "A shape", "kind": "edit", "schema": {"type": "object"}},
       {"name": "sim_play", "help": "Run", "kind": "window", "schema": {"type": "object"}},
       {"name": "app_quit", "help": "Quit", "kind": "action", "schema": {"type": "object"}},
       {"name": "plugin_wait", "help": "A plugin's own", "kind": "query", "schema": {"type": "object"}}]


class FakeEditor:
    def __init__(self):
        self.asked = []

    def ops_list(self, **args):
        self.asked.append(args)
        families = args.get("families")
        return {"ok": True, "ops": [op for op in OPS if not families or op["name"].split("_")[0] in families]}

    def call(self, op, **args):
        if op == "scene_add" and "shape" not in args:
            raise ke_editor.EditorError("scene_add: нет обязательного поля «shape»")
        return {"ok": True, "op": op}


class EditorMcp(unittest.TestCase):
    def setUp(self):
        self.editor = FakeEditor()
        self.saved = (mcp._editor, mcp.FAMILIES, ke_editor.reachable)
        mcp._editor, mcp.FAMILIES, ke_editor.reachable = self.editor, [], lambda *a, **k: True

    def tearDown(self):
        mcp._editor, mcp.FAMILIES, ke_editor.reachable = self.saved

    def tools(self):
        return {t["name"]: t for t in mcp.handle({"method": "tools/list"})["tools"]}

    def test_a_tool_says_what_it_is(self):
        tools = self.tools()
        self.assertEqual(tools["scene_list"]["annotations"], {"readOnlyHint": True})
        self.assertEqual(tools["scene_add"]["annotations"], {"readOnlyHint": False, "destructiveHint": False})  # one undo step
        self.assertEqual(tools["sim_play"]["annotations"], {"readOnlyHint": False, "destructiveHint": False})
        self.assertEqual(tools["app_quit"]["annotations"], {"readOnlyHint": False})  # as it may be: no promise
        self.assertNotIn("plugin_wait", tools)  # a plugin's own wiring
        self.assertIn("editor_call", tools)
        self.assertIn("editor_launch", tools)

    def test_a_profile_offers_its_families(self):
        mcp.FAMILIES = ["scene"]
        tools = self.tools()
        self.assertEqual(self.editor.asked[-1], {"families": ["scene"]})  # the editor filters, not this server
        self.assertEqual(sorted(tools), ["editor_call", "editor_launch", "scene_add", "scene_list"])
        said = mcp.handle({"method": "initialize", "params": {}})["instructions"]
        self.assertIn("Only the families scene are tools here", said)
        parts, failed = mcp.call_tool("editor_call", {"op": "sim_play"})  # the rest: one call away
        self.assertFalse(failed)

    def test_a_refusal_is_an_error(self):
        parts, failed = mcp.call_tool("scene_add", {})
        self.assertTrue(failed)
        self.assertIn("shape", parts[0]["text"])


if __name__ == "__main__":
    unittest.main()
