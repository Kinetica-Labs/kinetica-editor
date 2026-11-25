"""The MCP server of the editor (tools/ke_editor_mcp.py), without an editor: each operation is a tool with MCP's
annotations from its kind, a plugin's own wiring is not offered, a profile (KE_MCP_FAMILIES) asks the editor for its
families only and says so in the instructions, a refused call is an MCP error; the instructions fit a client's cut and the guide's topics are a tool."""
import tempfile
import subprocess
import json
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
        self.assertEqual(sorted(tools), ["editor_call", "editor_launch", "guide", "scene_add", "scene_list"])
        said = mcp.handle({"method": "initialize", "params": {}})["instructions"]
        self.assertIn("Only the families scene are tools here", said)
        parts, failed = mcp.call_tool("editor_call", {"op": "sim_play"})  # the rest: one call away
        self.assertFalse(failed)

    def test_the_guide_is_whole_and_its_topics_are_asked_for(self):
        said = mcp.handle({"method": "initialize", "params": {}})["instructions"]
        self.assertLess(len(said), 3800)  # Claude Code cuts a server's instructions at about 4 000 characters
        for topic in ("car", "arm", "power"):  # each topic the instructions name is one the tool gives
            self.assertIn(f"`{topic}`", said)
            parts, failed = mcp.call_tool("guide", {"topic": topic})
            self.assertFalse(failed)
            self.assertTrue(parts[0]["text"].startswith("# "))
        parts, failed = mcp.call_tool("guide", {"topic": "boat"})
        self.assertTrue(failed)
        self.assertIn("arm", parts[0]["text"])  # the refusal names the topics there are

    def test_a_refusal_is_an_error(self):
        parts, failed = mcp.call_tool("scene_add", {})
        self.assertTrue(failed)
        self.assertIn("shape", parts[0]["text"])


class ClaudePlugin(unittest.TestCase):
    """The plugin's door, Claude's and Codex's (claude/kinetica/server/kinetica_mcp.py): with Kinetica found it runs the editor's own MCP
    server - the guide comes as the instructions; with none it says how to install it and offers no tools."""
    DOOR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "claude", "kinetica", "server", "kinetica_mcp.py")

    def ask(self, home):
        lines = [{"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2025-06-18"}},
                 {"jsonrpc": "2.0", "id": 2, "method": "tools/list"}]
        env = dict(os.environ, KINETICA_HOME=home, LOCALAPPDATA=home)
        done = subprocess.run([sys.executable, self.DOOR], input="".join(json.dumps(m) + "\n" for m in lines), env=env,
                              capture_output=True, text=True, timeout=30)
        return [json.loads(line) for line in done.stdout.splitlines() if line.strip()]

    def test_finds_the_editor_and_gives_its_guide(self):
        answers = self.ask(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
        self.assertIn("Kinetica для агента", answers[0]["result"]["instructions"])

    def test_without_the_editor_says_how_to_install(self):
        with tempfile.TemporaryDirectory() as nowhere:
            answers = self.ask(nowhere)
        self.assertIn("install.sh", answers[0]["result"]["instructions"])
        self.assertEqual(answers[1]["result"]["tools"], [])

    def test_codex_reads_the_same_plugin(self):
        """Codex and ChatGPT read plugin.json and mcp.json (the Agent Plugins schema) before .claude-plugin/plugin.json: the
        same name, version and words as Claude's; one stdio server, a bare command (Codex refuses a path), run in the
        plugin's folder on the door Claude runs; the logo and icon its face names are there."""
        root = os.path.dirname(os.path.dirname(self.DOOR))
        read = lambda *path: json.load(open(os.path.join(root, *path), encoding="utf-8"))
        claude, codex, mcp = read(".claude-plugin", "plugin.json"), read("plugin.json"), read("mcp.json")
        for key in ("name", "version", "author", "homepage", "repository", "license", "keywords"):
            self.assertEqual(codex[key], claude[key], key)
        server = mcp["mcpServers"]["ke-editor"]
        self.assertEqual((server["type"], server["command"], server["cwd"]), ("stdio", "python3", "${PLUGIN_ROOT}"))
        self.assertEqual(os.path.normpath(os.path.join(root, server["args"][0])), os.path.normpath(self.DOOR))
        face = codex["extensions"]["com.openai"]["interface"]  # what Codex and ChatGPT show: its logo, not their placeholder
        for key in ("logo", "composerIcon"):
            self.assertTrue(face[key].startswith("./"), key)
            with open(os.path.join(root, face[key]), "rb") as f:
                self.assertEqual(f.read(8), b"\x89PNG\r\n\x1a\n", key)


if __name__ == "__main__":
    unittest.main()
