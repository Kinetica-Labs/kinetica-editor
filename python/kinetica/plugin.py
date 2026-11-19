"""Kinetica plugins in Python: one file, plain functions, no build and no manifest::

    from kinetica.plugin import command, field, run, ui, show

    @command(label="Ряд кубиков", key="Ctrl+Shift+R")
    def cube_row(scene, count: int = field(5, range=(1, 50), label="Сколько"),
                 gap: float = field(0.12, unit="м", range=(0.05, 1.0), label="Шаг")):
        \"\"\"A row of cubes in front of the robot.\"\"\"
        for k in range(count):
            scene.add("box", position=(0.4 + k * gap, 0.05, 0.3))
        return {"made": count}

    run()

Put it as ``plugin.py`` in a folder of the editor's plugins (Плагины → Папка плагинов, or Плагины → Новый плагин…):
the editor starts it, and ``cube_row`` is then a command of the editor like any - a button in «Плагины» with its key,
a line in Ctrl+K, ``ed.cubes_cube_row(count=3)`` in the console, an operation a script calls, a tool an AI agent gets
over MCP. Its fields make the form the button opens. What it changes in the scene is one undo step (Ctrl+Z), and every
call it makes is shown in the console as a Python line. Save the file: the editor restarts the plugin within a second.
An error points at your line: the editor shows ``plugin.py:12: ...`` on a banner, and goes on.

``scene`` is the same object model as the editor's console (``kinetica.Scene``): ``scene.objects["Куб 1"]``,
``scene.add("box")``, ``scene.editor.select(...)``, and ``scene.client.call(op, **args)`` for any operation
(``ops_list`` names them). Only the Python standard library is used.
"""
import inspect
import os
import sys
import traceback

from .client import Client
from .scene import Scene

_commands = {}
_panel = None
_link = {}  # the editor's client, name and token once run() has them: show() speaks through it


class field:
    """A command's argument as the editor shows it: its default, unit, range, the words of its label, its choices."""

    def __init__(self, default, unit=None, range=None, label=None, choices=None, help=None):
        self.default, self.unit, self.range, self.label, self.choices, self.help = default, unit, range, label, choices, help

    def schema(self, name, annotation):
        kind = annotation if annotation is not inspect.Parameter.empty else type(self.default)
        out = {"type": {bool: "boolean", int: "integer", float: "number", str: "string"}.get(kind, "number"), "default": self.default,
               "label": self.label or name}
        if self.unit:
            out["unit"] = self.unit
        if self.range:
            out["minimum"], out["maximum"] = self.range
        if self.choices:
            out["enum"] = list(self.choices)
        if self.help:
            out["help"] = self.help
        return out


def command(label=None, key=None):
    """A function of this file made a command of the editor: ``label`` on its button, ``key`` its shortcut."""

    def make(fn):
        _commands[fn.__name__] = {"fn": fn, "label": label or fn.__name__, "key": key or ""}
        return fn

    return make


class ui:
    """A plugin's panel as data the editor draws (docs/contracts/editor-plugin.md, «Словарь элементов панели»)::

        ui.panel("Кубики", ui.section("Ряд", ui.slider("count", 5, range=(1, 30), label="Сколько"),
                                      ui.button(row, "Поставить")), ui.value("made", "Поставлено"))

    A button calls its command with the panel's fields the command has (by name); show(made=3) sets a value."""

    @staticmethod
    def panel(title, *children, place="right"):
        """The plugin's panel: a tab of the inspector (right) or of the bottom panel (bottom)."""
        global _panel
        _panel = {"kind": "panel", "title": title, "place": place, "children": list(children)}

    @staticmethod
    def section(title, *children):
        return {"kind": "section", "title": title, "children": list(children)}

    @staticmethod
    def row(*children):
        return {"kind": "row", "children": list(children)}

    @staticmethod
    def label(text):
        return {"kind": "label", "text": text}

    @staticmethod
    def value(name, label=None, default=""):
        return {"kind": "value", "name": name, "default": default, **({"label": label} if label else {})}

    @staticmethod
    def button(command, label=None, help=None):
        name = command if isinstance(command, str) else command.__name__
        return {"kind": "button", "command": name, "label": label or _commands.get(name, {}).get("label", name), **({"help": help} if help else {})}

    @staticmethod
    def _field(kind, name, default, label, unit=None, range=None, **more):
        out = {"kind": kind, "name": name, "default": default, "label": label or name}
        if unit:
            out["unit"] = unit
        if range:
            out["minimum"], out["maximum"] = range
        out.update({k: v for k, v in more.items() if v is not None})
        return out

    @staticmethod
    def number(name, default=0.0, label=None, unit=None, range=None, step=None):
        whole = isinstance(default, int) and not isinstance(default, bool)
        return ui._field("number", name, default, label, unit, range, step=step, type="integer" if whole else "number")

    @staticmethod
    def slider(name, default=0, range=(0, 100), label=None, unit=None):
        return ui._field("slider", name, int(default), label, unit, range)

    @staticmethod
    def toggle(name, default=False, label=None):
        return ui._field("toggle", name, bool(default), label)

    @staticmethod
    def choice(name, choices, default=None, label=None):
        return ui._field("choice", name, default if default is not None else choices[0], label, enum=list(choices))

    @staticmethod
    def vector(name, default=(0.0, 0.0, 0.0), label=None, unit=None, range=None):
        return ui._field("vector", name, list(default), label, unit, range)

    @staticmethod
    def text(name, default="", label=None):
        return ui._field("text", name, default, label)


def show(**values):
    """The panel's words and fields set from the plugin: a measured value, a state, a count."""
    if _link:
        _link["client"].call("plugin_panel_set", name=_link["name"], token=_link["token"], values=values)


def _args(fn):
    """The function's arguments after `scene`, as the editor's form and an operation's schema read them."""
    out = {}
    for name, p in list(inspect.signature(fn).parameters.items())[1:]:
        default = p.default
        f = default if isinstance(default, field) else field(None if default is inspect.Parameter.empty else default)
        out[name] = f.schema(name, p.annotation)
    return out


def _where(error):
    """The author's line the error came from: ``plugin.py:12: what went wrong``."""
    frames = [f for f in traceback.extract_tb(error.__traceback__) if not f.filename.startswith(os.path.dirname(__file__))]
    at = frames[-1] if frames else None
    place = f"{os.path.basename(at.filename)}:{at.lineno}: " if at else ""
    return f"{place}{type(error).__name__}: {error}"


def run():
    """Register this file's commands with the editor that started it, then answer its calls until it stops us."""
    name, token = os.environ.get("KE_PLUGIN_NAME"), os.environ.get("KE_PLUGIN_TOKEN")
    port = int(os.environ.get("KE_EDITOR_PORT", "47801"))
    if not name:
        sys.exit("kinetica.plugin.run(): start this file from the editor (Плагины), it gives the plugin its name and port")
    who = f"plugin:{name}"
    control, calls = Client(port=port, timeout=None, src=who), Client(port=port, src=who)
    scene = Scene(calls)
    commands = [{"name": n, "label": c["label"], "key": c["key"], "help": (inspect.getdoc(c["fn"]) or "").strip(), "args": _args(c["fn"]),
                 "order": list(_args(c["fn"]))} for n, c in _commands.items()]
    _link.update(client=calls, name=name, token=token)
    control.call("plugin_register", name=name, token=token, file=os.path.abspath(sys.argv[0]), commands=commands,
                 **({"panel": _panel} if _panel else {}))
    while True:
        call = control.call("plugin_wait", name=name, token=token)
        fn = _commands.get(call["command"], {}).get("fn")
        try:
            given = call.get("args", {})
            values = {}  # what the call gave, else the field's default - never a `field` itself
            for arg, p in list(inspect.signature(fn).parameters.items())[1:] if fn else []:
                values[arg] = given[arg] if arg in given else p.default.default if isinstance(p.default, field) else p.default
            result = fn(scene, **values) if fn else None
            control.call("plugin_reply", name=name, token=token, call=call["call"], result=result if isinstance(result, dict) else {})
        except Exception as error:  # the author's mistake goes back to the editor with its line, the plugin lives on
            control.call("plugin_reply", name=name, token=token, call=call["call"], error=_where(error))
