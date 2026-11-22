"""The Python side of plugins (clients/python/kinetica/plugin.py), without an editor: a command's fields become the schema the
editor's form and MCP read (type, default, unit, range, choices, the label's words, in the function's order); a plain
default is a field too; an error points at the author's file and line, not at the SDK's; the hooks of a run answer on
their own wire (editor/RunHooks.h) - who the plugin is first, then play, every tick and stop in order, a tick's commands
merged, the bodies' truth only to a hook of the physics, a hook's mistake with its line; a chart of the panel is data, and
what a hook shows on the panel goes with its frame's answer."""
import json
import os
import socket
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "clients", "python"))
from kinetica import plugin  # noqa: E402
from kinetica.plugin import command, field, on_play, on_stop, on_tick, show, ui  # noqa: E402

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
    show(pushed=bodies[3]["mass"])
    return {3: {"force": [bodies[3]["mass"], 0, 0]}}


ui.panel("Ветер", ui.chart("Сила", {"pushed": "по x"}, unit="Н"), place="bottom")


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

    def test_gizmo_fields_and_drawings(self):
        self.assertEqual(field(30.0, unit="°", range=(0, 360), handle="dial").schema("angle")["handle"], "dial")
        self.assertEqual(field((0.4, 0.2, 0.6), handle="box").schema("size"), {"type": "vector", "default": [0.4, 0.2, 0.6], "label": "size", "handle": "box"})
        drawn = []  # a ring and a box are the editor's to work out (view_draw): the SDK only passes the words on
        send, plugin._Draw._send = plugin._Draw._send, lambda self, key, colour=None, **what: drawn.append(what)
        try:
            plugin.draw.circle("ring", (0, 1, 0), 2)
            plugin.draw.circle("tilted", center=(0, 0, 0), radius=1.0, axis=(1, 1, 0))
            plugin.draw.box("edges", centre=(0, 0.5, 0), size=(1, 1, 2))  # centre: the first name, taken as center
        finally:
            plugin._Draw._send = send
        self.assertEqual(drawn, [{"circle": {"center": [0.0, 1.0, 0.0], "radius": 2.0, "axis": [0.0, 1.0, 0.0]}},
                                 {"circle": {"center": [0.0, 0.0, 0.0], "radius": 1.0, "axis": [1.0, 1.0, 0.0]}},
                                 {"box": {"center": [0.0, 0.5, 0.0], "size": [1.0, 1.0, 2.0]}}])
        coloured = []  # color, the word of every 3D standard, and colour, the first name, are one
        send, plugin._Draw._send = plugin._Draw._send, lambda self, key, colour=None, **what: coloured.append(colour)
        try:
            plugin.draw.line("a", [(0, 0, 0), (1, 0, 0)], color=(1, 0, 0))
            plugin.draw.line("b", [(0, 0, 0), (1, 0, 0)], colour=(0, 1, 0))
        finally:
            plugin._Draw._send = send
        self.assertEqual(coloured, [(1, 0, 0), (0, 1, 0)])

    def test_a_filled_picture_and_the_change_hook(self):
        drawn = []  # the outlines, the space and the object are the editor's to draw (view_draw): the SDK passes the words on
        send, plugin._Draw._send = plugin._Draw._send, lambda self, key, colour=None, **what: drawn.append((key, colour, what))
        try:
            plugin.draw.fill("sign", ["M 0 0 L 1 0 L 0 1 Z"], object=7, at=(0, 0.4, 0), space="label", scale=0.05, colour="#ffcc00")
            plugin.draw.fill("board", ["M 0 0 L 1 0 L 0 1 Z"], turn=(0, 90, 0))
            plugin.draw.fill("title", ["M 0 0 L 1 0 L 0 1 Z"], space="screen", place=(0.5, 0.1), scale=0.06, stroke="#000000", stroke_width=0.08)
        finally:
            plugin._Draw._send = send
        row = ["M 0 0 L 1 0 L 0 1 Z"]
        self.assertEqual(drawn, [("sign", "#ffcc00", {"fill": row, "at": [0.0, 0.4, 0.0], "space": "label", "scale": 0.05, "object": 7}),
                                 ("board", None, {"fill": row, "at": [0.0, 0.0, 0.0], "space": "world", "scale": 1.0, "turn": [0.0, 90.0, 0.0]}),
                                 ("title", None, {"fill": row, "at": [0.0, 0.0, 0.0], "space": "screen", "scale": 0.06, "place": [0.5, 0.1],
                                                  "stroke": [0.0, 0.0, 0.0], "stroke_width": 0.08})])
        before = list(plugin._changes)
        try:
            hook = plugin.on_change(lambda scene: None)
            self.assertIs(plugin._changes[-1], hook)  # told when the plugin's components change, and once at its start
        finally:
            plugin._changes[:] = before

    def test_chart_is_data(self):
        self.assertEqual(plugin._panel["children"], [{"kind": "chart", "title": "Сила", "lines": [{"name": "pushed", "label": "по x"}], "unit": "Н"}])
        self.assertEqual(ui.chart("Углы", ["a", "b"])["lines"], [{"name": "a", "label": "a"}, {"name": "b", "label": "b"}])
        steps = ui.chart("Обучение", {"best": "лучший", "mean": "средний"}, unit="м", x="generation")  # a step, not the run's time
        self.assertEqual((steps["x"], steps["x_label"], steps["unit"]), ("generation", "generation", "м"))
        words = ui.chart("Обучение", ["best"], x={"generation": "поколение"})
        self.assertEqual((words["x"], words["x_label"], words["lines"]), ("generation", "поколение", [{"name": "best", "label": "best"}]))
        self.assertNotIn("x", ui.chart("Время", ["a"]))  # without x everything stays as it was
        show(pushed=1.0)  # no editor and no hook running: nowhere to say it, and no mistake

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
        self.assertEqual(answer, {"call": 2, "commands": {"7": {"target": 0.5}, "3": {"force": [2.0, 0, 0]}}, "draw": [],
                                  "show": {"pushed": 2.0}})  # what the hook showed on the panel: with the frame's answer
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
