"""The Python side of plugins (python/kinetica/plugin.py), without an editor: a command's fields become the schema the
editor's form and MCP read (type, default, unit, range, choices, the label's words, in the function's order); a plain
default is a field too; an error points at the author's file and line, not at the SDK's; the hooks of a run answer on
their own wire (editor/RunHooks.h) - who the plugin is first, then play, every tick and stop in order, a tick's commands
merged, the bodies' truth only to a hook of the physics, a hook's mistake with its line."""
import json
import os
import socket
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "python"))
from kinetica import plugin  # noqa: E402
from kinetica.plugin import command, field, on_play, on_stop, on_tick  # noqa: E402

ran = []  # the run's hooks, as they were called


@on_play
def started(scene):
    ran.append("play")


@on_tick
def control(scene, dt, sensors):
    ran.append("tick")
    return {7: {"target": sensors[5]["reading"] * 2}}  # no sensor 5: the author's mistake, this line


@on_tick(world=True)
def wind(scene, dt, bodies):
    return {3: {"force": [bodies[3]["mass"], 0, 0]}}


@on_stop
def stopped(scene):
    ran.append("stop")


@command(label="Ряд", key="Ctrl+Shift+R")
def row(scene, count: int = field(5, range=(1, 30), label="Сколько"), gap: float = field(0.1, unit="м"),
        solid: bool = True, colour: str = field("red", choices=("red", "blue"))):
    """A row."""
    return {"made": count}


class PluginSdk(unittest.TestCase):
    def test_command_registered(self):
        self.assertIn("row", plugin._commands)
        self.assertEqual(plugin._commands["row"]["label"], "Ряд")
        self.assertEqual(plugin._commands["row"]["key"], "Ctrl+Shift+R")

    def test_fields_become_schema(self):
        args = plugin._args(row)
        self.assertEqual(list(args), ["count", "gap", "solid", "colour"])
        self.assertEqual(args["count"], {"type": "integer", "default": 5, "label": "Сколько", "minimum": 1, "maximum": 30})
        self.assertEqual(args["gap"]["unit"], "м")
        self.assertEqual(args["gap"]["type"], "number")
        self.assertEqual(args["solid"], {"type": "boolean", "default": True, "label": "solid"})
        self.assertEqual(args["colour"]["enum"], ["red", "blue"])

    def test_field_of_a_scene_object(self):
        said = field(ref="joint", label="Сустав", help="what to turn").schema("joint")
        self.assertEqual(said, {"type": "integer", "ref": "joint", "label": "Сустав", "help": "what to turn"})  # no default: it must be said
        self.assertEqual(field(7, ref="encoder").schema("encoder")["default"], 7)

    def test_error_points_at_author(self):
        try:
            row(None, count="x") + 1  # a dict plus an int: the author's own line
        except TypeError as error:
            where = plugin._where(error)
        self.assertTrue(where.startswith("test_plugin_sdk.py:"), where)
        self.assertIn("TypeError", where)

    def test_run_hooks_on_their_wire(self):
        editor = socket.socket()
        editor.bind(("127.0.0.1", 0))
        editor.listen(1)
        editor.settimeout(10)
        taken = []  # the plugin's side waits for {"ok": true}: the editor's side answers from a thread

        def take():
            wire, _ = editor.accept()
            wire.settimeout(10)
            lines = wire.makefile("rb")
            taken.extend([wire, lines, json.loads(lines.readline())])
            wire.sendall(b'{"ok": true}\n')

        import threading
        accepting = threading.Thread(target=take)
        accepting.start()
        thread = plugin._run_wire(None, editor.getsockname()[1], "ticker", "secret")
        accepting.join(10)
        wire, lines, hello = taken
        self.assertEqual(hello, {"name": "ticker", "token": "secret", "hooks": ["play", "tick", "stop", "tick:world"]})

        def event(call, name, **args):
            wire.sendall((json.dumps({"call": call, "event": name, "args": args}) + "\n").encode())
            return json.loads(lines.readline())

        self.assertEqual(event(1, "play"), {"call": 1, "commands": {}, "draw": []})
        answer = event(2, "tick", dt=0.016, sensors=[{"id": 5, "kind": "encoder", "reading": 0.25}], bodies={"3": {"mass": 2.0}})
        self.assertEqual(answer, {"call": 2, "commands": {"7": {"target": 0.5}, "3": {"force": [2.0, 0, 0]}}, "draw": []})
        wrong = event(3, "tick", dt=0.016, sensors=[], bodies={"3": {"mass": 2.0}})
        self.assertEqual(wrong["call"], 3)
        self.assertRegex(wrong["error"], r"^test_plugin_sdk\.py:\d+: KeyError: 5$")
        self.assertEqual(event(4, "stop"), {"call": 4, "commands": {}, "draw": []})
        self.assertEqual(ran, ["play", "tick", "tick", "stop"])
        lines.close()
        wire.close()
        editor.close()
        thread.join(10)
        self.assertFalse(thread.is_alive(), "the wire closed: its thread ends")


if __name__ == "__main__":
    unittest.main()
