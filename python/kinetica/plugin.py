"""Kinetica plugins in Python: one file, plain functions, no build and no manifest::

    from kinetica.plugin import command, field

    @command(label="Ряд кубиков")
    def cube_row(scene, count: int = field(5, range=(1, 50), label="Сколько"),
                 gap: float = field(0.12, unit="м", range=(0.05, 1.0), label="Шаг")):
        \"\"\"A row of cubes in front of the robot.\"\"\"
        for k in range(count):
            scene.add("box", position=(0.4 + k * gap, 0.05, 0.3))
        return {"made": count}

Put it as ``plugin.py`` in a folder of the editor's plugins (Плагины → Папка моих плагинов, or Плагины → Новый плагин…):
the editor starts it, and ``cube_row`` is then a command of the editor like any - a button in «Плагины» with its key,
a line in Ctrl+K, ``ed.cubes_cube_row(count=3)`` in the console, an operation a script calls, a tool an AI agent gets
over MCP. Its fields make the form the button opens. What it changes in the scene is one undo step (Ctrl+Z), and every
call it makes is shown in the console as a Python line. Save the file: the editor restarts the plugin within a second.
An error points at your line: the editor shows ``plugin.py:12: ...`` on a banner, and goes on.

``scene`` is the same object model as the editor's console (``kinetica.Scene``): ``scene.objects["Куб 1"]``,
``scene.add("box")``, ``scene.editor.selection``, and ``scene.ed.<operation>(**args)`` for any operation - the
console's ``ed``, so a line tried there is pasted here as it is (``ops_list`` names them all). ``print`` goes to the
editor's API console. The editor starts the file itself (``python -m kinetica.plugin plugin.py``): no ``run()`` at its
end is needed - an old file's ``run()`` does nothing. Only the Python standard library is used.
"""
import inspect
import os
import runpy
import sys
import traceback

from .client import Client
from .scene import Scene

_commands = {}
_hooks = {"play": [], "tick": [], "stop": []}  # the run's hooks: (function, world)
_panel = None
_link = {}  # the editor's client, name and token once serving: show() speaks through it


class field:
    """A command's argument or a component's value as the editor shows it: its default, unit, range, the words of its label,
    its choices - and, on a component, its handle in the view (Cinema 4D's orange point on the shape): "radius" (along the
    shape's x), "length" (along `axis`, "y" if none) or "point" (a place in the shape's frame; a 3-number default)."""

    def __init__(self, default, unit=None, range=None, label=None, choices=None, help=None, handle=None, axis=None):
        self.default, self.unit, self.range, self.label, self.choices, self.help = default, unit, range, label, choices, help
        self.handle, self.axis = handle, axis

    def schema(self, name, annotation=inspect.Parameter.empty):
        kind = annotation if annotation is not inspect.Parameter.empty else type(self.default)
        vector = isinstance(self.default, (list, tuple))
        out = {"type": "vector" if vector else {bool: "boolean", int: "integer", float: "number", str: "string"}.get(kind, "number"),
               "default": list(self.default) if vector else self.default, "label": self.label or name}
        if self.handle:
            out["handle"] = self.handle
        if self.axis:
            out["axis"] = self.axis
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


_components = {}  # name -> the class


def component(label=None, help=None):
    """A class made a component of shapes: its `field` attributes its values. The editor draws its card in the inspector,
    offers it in «+ Добавить компонент», writes it in the scene file, shows its handles as points on the shape::

        @component(label="Ветряк")
        class Turbine:
            radius = field(1.0, unit="м", range=(0.1, 5.0), label="Радиус", handle="radius")
            power = field(100.0, unit="Вт", label="Мощность")

    ``Turbine.instances(scene)`` - every shape that has it, with its values: ``[(id, {"radius": 1.0, ...}), ...]``."""

    def make(cls):
        name = "".join("_" + ch.lower() if ch.isupper() else ch for ch in cls.__name__).lstrip("_")
        cls._kinetica = {"name": name, "label": label or cls.__name__, "help": help or (inspect.getdoc(cls) or "").strip(),
                         "fields": {k: v for k, v in vars(cls).items() if isinstance(v, field)}}
        cls.instances = classmethod(_instances)
        _components[name] = cls
        return cls

    return make


def _instances(cls, scene):
    """The shapes that have this component, with its values (a field the scene does not say: its default)."""
    meta = cls._kinetica
    rows = scene.ed.component_list(type=f"{_link.get('name', '')}.{meta['name']}")["components"]
    defaults = {k: (list(f.default) if isinstance(f.default, (list, tuple)) else f.default) for k, f in meta["fields"].items()}
    return [(row["id"], {**defaults, **row["values"]}) for row in rows]


def on_play(fn):
    """Called once when the scene starts running (▶): ``def start(scene)``."""
    _hooks["play"].append((fn, False))
    return fn


def on_stop(fn):
    """Called once when the scene stops (Стоп - the scene goes back as it was): ``def stop(scene)``."""
    _hooks["stop"].append((fn, False))
    return fn


def on_tick(fn=None, *, world=False):
    """Called every frame while the scene runs, before the next frame is stepped - the simulation waits for it.

    A robot's controller sees what its sensors read, never the truth::

        @on_tick
        def control(scene, dt, sensors):            # sensors: {id: {"kind": "encoder", "reading": 0.41, ...}}
            return {7: {"target": 0.8}}             # a joint's target / velocity, a driver's duty: no undo step, no journal

    A plugin that extends the physics (a gas, water, wind) sees the bodies as the engine has them and pushes them::

        @on_tick(world=True)
        def wind(scene, dt, bodies):                # bodies: {id: {"position", "velocity", "spin", "mass"}}
            return {i: {"force": [2.0, 0, 0]} for i in bodies}   # N over this frame, at the centre of mass
    """
    def take(f):
        _hooks["tick"].append((f, world))
        return f

    return take(fn) if fn is not None else take


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
    """Kept for files written before the runner: the editor serves the file itself now, so this does nothing."""


def _serve(file):
    """Register the file's commands with the editor that started it, then answer its calls until it stops us - all of it
    over one connection (the client hands each answer to the call that asked, from any thread)."""
    name, token = os.environ.get("KE_PLUGIN_NAME"), os.environ.get("KE_PLUGIN_TOKEN")
    port = int(os.environ.get("KE_EDITOR_PORT", "47801"))
    if not name:
        sys.exit("kinetica.plugin: the editor starts a plugin (Плагины) - it gives it its name and port")
    control = Client(port=port, src=f"plugin:{name}")
    scene = Scene(control)
    commands = [{"name": n, "label": c["label"], "key": c["key"], "help": (inspect.getdoc(c["fn"]) or "").strip(), "args": _args(c["fn"]),
                 "order": list(_args(c["fn"]))} for n, c in _commands.items()]
    _link.update(client=control, name=name, token=token)
    hooks = [event for event, fns in _hooks.items() if fns] + (["tick:world"] if any(w for _, w in _hooks["tick"]) else [])
    components = [{"name": m["name"], "label": m["label"], "help": m["help"], "order": list(m["fields"]),
                   "fields": {k: f.schema(k) for k, f in m["fields"].items()}} for m in (c._kinetica for c in _components.values())]
    control.call("plugin_register", name=name, token=token, file=os.path.abspath(file), commands=commands, hooks=hooks,
                 components=components, **({"panel": _panel} if _panel else {}))
    while True:
        call = control.call("plugin_wait", name=name, token=token)
        if "event" in call:  # ▶, a frame, Стоп: the run's hooks; a tick's commands merged into one answer
            try:
                control.call("plugin_reply", name=name, token=token, call=call["call"], event=True,
                             result={"commands": _run_hooks(scene, call["event"], call.get("args", {}))})
            except Exception as error:  # the author's mistake, with its line; the run goes on
                control.call("plugin_reply", name=name, token=token, call=call["call"], event=True, error=_where(error))
            continue
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


def _run_hooks(scene, event, args):
    """The hooks of one event; a tick's commands, merged and keyed as JSON wants them."""
    commands = {}
    for fn, world in _hooks.get(event, []):
        if event != "tick":
            fn(scene)
            continue
        if world:
            seen = {int(k): v for k, v in args.get("bodies", {}).items()}
        else:
            seen = {s["id"]: s for s in args.get("sensors", [])}
        for target, what in (fn(scene, args.get("dt", 0.0), seen) or {}).items():
            commands.setdefault(str(target), {}).update(what)
    return commands


def main():
    """``python -m kinetica.plugin plugin.py``: the file run as the plugin's module (its @command and ui.panel lines
    declare it), then served."""
    if len(sys.argv) != 2:
        sys.exit("usage: python -m kinetica.plugin plugin.py (the editor starts plugins itself)")
    file = os.path.abspath(sys.argv[1])
    sys.path.insert(0, os.path.dirname(file))
    runpy.run_path(file, run_name="__plugin__")
    _serve(file)


if __name__ == "__main__":  # run as -m, this file is "__main__"; the plugin's file imports kinetica.plugin: serve from that one
    from kinetica import plugin as _module

    _module.main()
