"""Kinetica plugins in Python: one file, plain functions, no build and no manifest::

    from kinetica.plugin import command, field

    @command(label="Ряд кубиков")
    def cube_row(scene, count: int = field(5, range=(1, 50), label="Сколько"),
                 gap: float = field(0.12, unit="м", range=(0.05, 1.0), label="Шаг")):
        \"\"\"A row of cubes in front of the robot.\"\"\"
        for k in range(count):
            scene.add("box", position=(0.4 + k * gap, 0.05, 0.3), role="rigid")
        return {"made": count}

Put it as ``plugin.py`` in a folder of the editor's plugins (Плагины → Папка моих плагинов, or Плагины → Новый плагин…):
the editor starts it, and ``cube_row`` is then a command of the editor like any - a button in «Плагины» with its key,
a line in Ctrl+K, ``ed.cubes_cube_row(count=3)`` in the console, an operation a script calls, a tool an AI agent gets
over MCP. Its fields make the form the button opens. What it changes in the scene is one undo step (Ctrl+Z), and every
call it makes is shown in the console as a Python line. Save the file: the editor restarts the plugin within a second.
An error points at your line: the editor shows ``plugin.py:12: ...`` on a banner, and goes on.

``scene`` is the same object model as the editor's console (``kinetica.Scene``): ``scene.objects["Куб 1"]``,
``scene.add("box")``, ``scene.editor.selection`` - and ``scene.ed.<operation>(**args)`` for any operation it has no
word for: the console's ``ed``, so a line tried there is pasted here as it is (``ops_list`` names them all). These two
are the way to the scene; there is no other. ``print`` goes to the editor's API console. The editor starts the file
itself (``python -m kinetica.plugin plugin.py``). Only the Python standard library is used.
"""
import functools
import inspect
import json
import os
import runpy
import socket
import sys
import threading
import traceback

from .client import Client
from .scene import Scene

_commands = {}
_hooks = {"play": [], "tick": [], "stop": []}  # the run's hooks: (function, world)
_panel = None
_link = {}  # the editor's client, name and token once serving: show() speaks through it
_busy = threading.Lock()  # the author's code runs one piece at a time: a command, or a hook of the run


class field:
    """A command's argument or a component's value as the editor shows it: its default, unit, range, the words of its label,
    its choices - and, on a component, its handle in the view (a point drawn on the shape): "radius" (along the
    shape's x), "length" (along `axis`, "y" if none), "point" (a place in the shape's frame; a 3-number default), "dial"
    (an angle about `axis`: a point on a circle around the shape - in degrees when the unit is "°", else radians) or
    "box" (a size, 3 numbers: a point on each of the faces +x, +y, +z).

    `ref` makes it an object of the scene instead of a number typed by hand: ``joint=field(ref="joint")``. Its word is a
    kind ``scene_list`` knows - "joint", "body", "encoder" and the other sensors' own kinds, "sensor", "camera", "light", "force",
    "driver"... The form offers the scene's objects of that kind by name (the selected one first), a call gives its id -
    one of another kind is refused, naming those there are -, and the command gets the object itself, as
    ``scene.objects[id]`` gives it. A component keeps the id."""

    def __init__(self, default=None, unit=None, range=None, label=None, choices=None, help=None, handle=None, axis=None, ref=None):
        self.default, self.unit, self.range, self.label, self.choices, self.help = default, unit, range, label, choices, help
        self.handle, self.axis, self.ref = handle, axis, ref

    def schema(self, name, annotation=inspect.Parameter.empty):
        if self.ref:  # an id of the scene; no default: the call must say which
            out = {"type": "integer", "ref": self.ref, "label": self.label or name}
            if self.default is not None:
                out["default"] = int(self.default)
            if self.help:
                out["help"] = self.help
            return out
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


def command(label=None, key=None, tool=False, opens=None, saves=None, obsolete=None):
    """A function of this file made a command of the editor: ``label`` on its button, ``key`` its shortcut.

    ``tool=True`` makes it a tool of the shelf at the left of the view (an orange button): a click in the view calls it
    with ``point`` and ``normal`` (the surface's, in the world) and ``object`` (the id under the mouse, 0 - the floor);
    its other arguments take the values of the plugin's panel, else their defaults::

        @command(label="Дерево", tool=True)
        def plant(scene, point, normal, object, height=field(2.0, unit="м")):
            scene.add("cylinder", position=point, size=(0.2, height, 0.2))

    A tool that also takes ``to`` is dragged: pressed at ``point``, let go at ``to`` (and
    ``to_object`` under it), a line with its length drawn between them meanwhile; a click is a drag of no length::

        @command(label="Стена", tool=True)
        def wall(scene, point, to, height=field(1.0, unit="м")):
            ...

    ``opens=".urdf"`` (or a list) makes it how the editor opens such files: «Файл → Импорт…» lists the format, a file
    dropped on the view goes to it; it is called with ``path``, and all it builds is one undo step::

        @command(label="URDF — робот", opens=".urdf")
        def load(scene, path):
            ...

    ``saves=".urdf"`` makes it how the editor writes such files: «Файл → Экспорт…» lists the format and calls it with
    ``path`` (the file the person chose); it changes nothing in the scene.

    ``obsolete="io_formats_urdf_load"`` keeps an old name alive after it moved (names only grow:
    docs/contracts/names.md): the operation still answers - the function forwards to the new one -, but it is no
    button, no menu item and no format of «Импорт…»; its help names what to call instead.
    """

    def make(fn):
        given = [opens] if isinstance(opens, str) else list(opens or [])
        written = [saves] if isinstance(saves, str) else list(saves or [])
        _commands[fn.__name__] = {"fn": fn, "label": label or fn.__name__, "key": key or "", "tool": bool(tool),
                                  "opens": [] if obsolete else [e if e.startswith(".") else "." + e for e in given],
                                  "saves": [] if obsolete else [e if e.startswith(".") else "." + e for e in written],
                                  "obsolete": obsolete or ""}
        return fn

    return make


_CLICK = {"point": {"type": "vector", "label": "Точка щелчка", "unit": "м"}, "normal": {"type": "vector", "label": "Нормаль поверхности"},
          "object": {"type": "integer", "label": "Объект под мышью (0 - пол)"},
          "to": {"type": "vector", "label": "Точка, до которой дотянули", "unit": "м"},  # a tool with `to` is dragged: two points
          "to_object": {"type": "integer", "label": "Объект под мышью в конце (0 - пол)"}}


def _leader(leader):
    """view_draw's leader from draw's: {"point": (x, y, z), "end": "dot" | "arrow" | "none"} (none given: no line)."""
    if leader is None:
        return {}
    return {"leader": {"point": _xyz([leader.get("point", (0, 0, 0))])[0], "end": str(leader.get("end", "dot"))}}


class _Draw:
    """Lines, points and words over the 3D view, in world metres: what the plugin wants seen - a sensor's ray, a planned
    path, a force, a reading. Each drawing has a key: the same key again replaces it. Drawn from on_tick, it goes with the
    frame's answer and is gone at Стоп; drawn from a command, it stays until cleared or the plugin stops. No undo step,
    no journal line::

        draw.line("путь", [(0, 0, 0), (1, 0.5, 0), (2, 0, 0)])        # a line through the points
        draw.segments("силы", [a0, a1, b0, b1])                        # pairs: the arrows of forces
        draw.points("метки", points, colour="#33ccff", size=4)
        draw.label("v", (0, 1.2, 0), "3.2 м/с")                           # size=20: larger letters
        draw.circle("зона", (0, 0, 0), 0.8)                            # about the world's y; axis="x", or a direction
        draw.box("габарит", (0, 0.5, 0), (1, 1, 2))                    # its twelve edges
        draw.fill("знак", ["M 0 0 L 1 0 L 0.5 1 Z"], object=cube, at=(0, 0.4, 0), space="label", scale=0.05)  # a filled picture
        draw.clear("путь")                                             # draw.clear(): all of this plugin's

    A color is "#rrggbb" or (r, g, b) from 0 to 1; orange if none. `color` and `center` are the words of every 3D
    standard; `colour` and `centre`, the first ones, are taken the same."""

    def line(self, key, points, colour=None, color=None):
        self._send(key, line=_xyz(points), colour=_either(color, colour))

    def segments(self, key, points, colour=None, color=None):
        self._send(key, segments=_xyz(points), colour=_either(color, colour))

    def points(self, key, points, colour=None, size=6, color=None):
        self._send(key, points=_xyz(points), colour=_either(color, colour), size=size)

    def label(self, key, at, text, colour=None, size=None, color=None, object=None, leader=None):
        """Words at a point of the world; `size`: their letters, pt, when they are to be larger than the view's own.
        `object`: they hang at `at` from its place and go where it goes; `leader`: a line from them to a point of it - see fill."""
        what = {"at": _xyz([at])[0], "text": str(text), **({"size": size} if size else {})}
        if object is not None:
            what["object"] = int(getattr(object, "id", object))
        self._send(key, colour=_either(color, colour), **what, **_leader(leader))

    def circle(self, key, centre=None, radius=None, axis="y", colour=None, color=None, center=None):
        """A circle around `center`, about a world axis ("x", "y", "z") or a direction (x, y, z) - a gizmo's ring."""
        middle = _either(center, centre)
        if middle is None or radius is None:
            raise TypeError("draw.circle(key, center, radius, axis='y')")
        about = {"x": (1, 0, 0), "y": (0, 1, 0), "z": (0, 0, 1)}.get(axis, axis)
        self._send(key, circle={"center": _xyz([middle])[0], "radius": float(radius), "axis": _xyz([about])[0]}, colour=_either(color, colour))

    def box(self, key, centre=None, size=None, colour=None, color=None, center=None):
        """The twelve edges of a box of `size` (x, y, z) around `center`, along the world's axes."""
        middle = _either(center, centre)
        if middle is None or size is None:
            raise TypeError("draw.box(key, center, size)")
        self._send(key, box={"center": _xyz([middle])[0], "size": _xyz([size])[0]}, colour=_either(color, colour))

    def fill(self, key, outlines, at=(0, 0, 0), object=None, space="world", scale=1.0, turn=None, place=None, colour=None, stroke=None,
             stroke_width=None, color=None, leader=None):
        """A flat picture, filled, painted over the scene: `outlines` are rows "M x y L x y C x1 y1 x2 y2 x y Z" in the
        picture's own units, x right and y up. space "world": on its own plane at `at`, turned by `turn` (Euler degrees),
        a unit `scale` metres; "label": on the screen at the world's point `at`, facing the viewer; "screen": in the frame
        at `place` = (x, y), fractions of the view - in both a unit is `scale` of the view's height. `object` (an object
        of the scene or its id): the picture is its own and goes where it goes; a click on it selects the object. `stroke`:
        an edge of that colour round every outline, `stroke_width` of the picture's units wide (0.05) - so that letters
        read on any background. `leader` = {"point": (x, y, z), "end": "dot" | "arrow" | "none"}: a leader line from the
        picture to that point of the object (its own frame) - a reference line under the picture, a dot on the part, an
        arrow on its outline (ISO 128-22, ГОСТ 2.316)."""
        what = {"fill": [str(row) for row in outlines], "at": _xyz([at])[0], "space": space, "scale": float(scale), **_leader(leader)}
        if object is not None:
            what["object"] = int(getattr(object, "id", object))
        if turn is not None:
            what["turn"] = _xyz([turn])[0]
        if place is not None:
            what["place"] = [float(place[0]), float(place[1])]
        if stroke is not None:
            what["stroke"] = [int(stroke[i:i + 2], 16) / 255 for i in (1, 3, 5)] if isinstance(stroke, str) else [float(c) for c in stroke]
            if stroke_width is not None:
                what["stroke_width"] = float(stroke_width)
        self._send(key, colour=_either(color, colour), **what)

    def clear(self, key=None):
        self._send(key, clear=True)

    def _send(self, key, colour=None, **what):
        name = _link.get("name")
        if not name:
            raise RuntimeError("draw: only in a plugin the editor started (the console has ed.view_draw)")
        what["key"] = f"{name}/" + ("" if key is None else str(key))
        if colour is not None:
            what["color"] = [int(colour[i:i + 2], 16) / 255 for i in (1, 3, 5)] if isinstance(colour, str) else [float(c) for c in colour]
        batch = getattr(_frame, "draws", None)
        if batch is not None:  # inside a hook: with the frame's answer, no call of its own
            batch.append(what)
        else:
            _link["client"].call("view_draw", **what)


def _either(new, first):
    """The value under the canonical word, else under its first name (docs/contracts/names.md: names only grow)."""
    return new if new is not None else first


def _xyz(points):
    points = points.tolist() if hasattr(points, "tolist") else points
    return [[float(p[0]), float(p[1]), float(p[2])] for p in points]


_frame = threading.local()  # the hook running on this thread collects its drawings and its panel's values here
draw = _Draw()


_components = {}  # name -> the class


def component(label=None, help=None):
    """A class made a component: its `field` attributes its values. On a shape the editor draws its card in the inspector,
    offers it in «+ Добавить компонент» and shows its handles as points on the shape; on any other object - a joint, a
    light, a sensor - it is put by ``scene.ed.component_set(id=..., type=..., values={...})`` and kept in the scene file
    the same way::

        @component(label="Ветряк")
        class Turbine:
            radius = field(1.0, unit="м", range=(0.1, 5.0), label="Радиус", handle="radius")
            power = field(100.0, unit="Вт", label="Мощность")

    ``Turbine.instances(scene)`` - every object that has it, with its values: ``[(id, {"radius": 1.0, ...}), ...]``."""

    def make(cls):
        name = "".join("_" + ch.lower() if ch.isupper() else ch for ch in cls.__name__).lstrip("_")
        cls._kinetica = {"name": name, "label": label or cls.__name__, "help": help or (inspect.getdoc(cls) or "").strip(),
                         "fields": {k: v for k, v in vars(cls).items() if isinstance(v, field)}}
        cls.instances = classmethod(_instances)
        _components[name] = cls
        return cls

    return make


def _instances(cls, scene):
    """The objects that have this component, with its values (a field the scene does not say: its default)."""
    meta = cls._kinetica
    rows = scene.ed.component_list(type=f"{_link.get('name', '')}.{meta['name']}")["components"]
    defaults = {k: (list(f.default) if isinstance(f.default, (list, tuple)) else f.default) for k, f in meta["fields"].items()}
    return [(row["id"], {**defaults, **row["values"]}) for row in rows]


_changes = []  # the functions of @on_change


def on_change(fn):
    """A function called when a component of this plugin is put on an object, changed or taken off - by a hand in its
    card, a script, an undo, a scene opened - and once when the plugin starts: ``fn(scene)``. Where a plugin keeps what
    it draws for its components up to date::

        @on_change
        def redraw(scene):
            draw.clear()
            for id, values in Sign.instances(scene):
                draw.fill(f"sign/{id}", OUTLINE, object=id, at=(0, 0.4, 0), space="label", scale=values["size"])

    It is not an edit: a change it makes to the scene is no part of an undo step, and one to its own components calls
    it again. Not called while the scene plays: what a run changed is told after Стоп, if it stayed."""
    _changes.append(fn)
    return fn


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

    A plugin that extends the physics (wind, a magnet, a thruster) sees the bodies as the engine has them and pushes them::

        @on_tick(world=True)
        def wind(scene, dt, bodies):                # bodies: {id: {"position", "rotation", "velocity", "spin", "mass"}}
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
        """The plugin's panel: a tab of the inspector. `place` is kept ("bottom" too) and means the same: the window's
        first level - its docked panels - is the person's, not a plugin's (docs/contracts/ui.md)."""
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
    def chart(title, lines, unit=None, x=None):
        """Lines drawn by the editor in the «Графики» tab, in the plugin's section (the panel has a «Графики ▸» button to
        it); the person picks there which of them are seen. A line is one of the panel's values.
        `lines`: the values' names, or {name: the line's words}. By default the axis is the run's time: show(error=0.02)
        from on_tick adds a point at that frame's time::

            ui.chart("Ошибка привода", {"error": "ошибка", "trim": "поправка"}, unit="рад")

        With `x` (a value's name, or {name: words}) the axis is a step - a generation, an iteration - and a point comes
        whenever one show() carries the `x` and a line's value, from any thread, not only on_tick; the chart keeps all of
        them, and `x` going back starts a new history::

            ui.chart("Обучение", {"best": "лучший", "mean": "средний"}, unit="м", x={"generation": "поколение"})
            show(generation=g, best=849.0, mean=27.0)"""
        lines = lines if isinstance(lines, dict) else {name: name for name in lines}
        axis = {} if x is None else {"x": next(iter(x)), "x_label": next(iter(x.values()))} if isinstance(x, dict) else {"x": x, "x_label": x}
        return {"kind": "chart", "title": title, "lines": [{"name": n, "label": l} for n, l in lines.items()], **({"unit": unit} if unit else {}), **axis}

    @staticmethod
    def svg(name, height=None):
        """A picture in the panel: an SVG the plugin sends as the value `name`, drawn as wide as the panel (aspect kept),
        at most `height` px high. It changes with every show(), from a hook or any thread::

            ui.svg("net", height=260)
            show(net='<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 300 200">...</svg>')"""
        return {"kind": "svg", "name": name, **({"height": height} if height else {})}

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


def tr(text):
    """A word of the plugin in the window's language: its translation in the plugin's catalog - i18n/<language>.json
    beside plugin.py, {"поколение {g}": "generation {g}"} - else the word itself. What the plugin describes (labels,
    titles, charts, help) the editor translates by that catalog itself; tr is for the words it makes up as it runs::

        show(status=tr("поколение {g}: лучший {best:.2f}").format(g=g, best=best))"""
    return _words().get(text, text)


@functools.lru_cache(maxsize=None)
def _words():
    """The catalog the editor named for this window's language (KE_PLUGIN_WORDS), read once; none: no words."""
    try:
        with open(os.environ["KE_PLUGIN_WORDS"], encoding="utf-8") as f:
            return json.load(f)
    except (KeyError, OSError, ValueError):
        return {}


def show(**values):
    """The panel's words and fields set from the plugin: a measured value, a state, a count. From a hook of the run
    (on_tick) they go with the frame's answer, no call of their own, and a chart's lines take a point at that frame. From
    any other thread they go in a call of their own - a step chart (ui.chart(..., x=...)) takes its points from them."""
    batch = getattr(_frame, "shown", None)
    if batch is not None:
        if _panel is None:
            raise RuntimeError("show: the plugin has no panel (ui.panel(...) in its file)")
        batch.update(values)
    elif _link:
        _link["client"].call("plugin_panel_set", name=_link["name"], token=_link["token"], values=values)


def process_pool(max_workers=None, initializer=None, initargs=()):
    """Worker processes for work a plugin's one core is too slow for (a training, a sweep): a
    ``concurrent.futures.ProcessPoolExecutor`` whose processes end with the plugin's - stopped, restarted, the editor
    closed - instead of computing on for no one (the editor ends the plugin's process, not what it started). Spawned,
    not forked: the plugin has threads of its own, a fork would copy them half-way. Its jobs and their functions go to
    the workers by pickle, so a job's function is a module's top-level one::

        with process_pool(8) as pool:
            scores = list(pool.map(score, genomes))
    """
    import multiprocessing
    from concurrent.futures import ProcessPoolExecutor
    return ProcessPoolExecutor(max_workers, mp_context=multiprocessing.get_context("spawn"), initializer=_pool_worker,
                               initargs=(initializer, tuple(initargs)))


def _pool_worker(initializer, initargs):
    """A process_pool worker's start: a thread that ends the process when its parent's ends - the parent's sentinel (a
    pipe or a process handle) is signalled then, on every system -, then the pool's own initializer."""
    import multiprocessing
    parent = multiprocessing.parent_process()

    def orphaned():
        parent.join()
        os._exit(0)  # not exit(): the pool's own thread would wait for a parent that is gone

    threading.Thread(target=orphaned, name="kinetica-orphan", daemon=True).start()
    if initializer:
        initializer(*initargs)


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


def _serve(file):
    """Register the file's commands with the editor that started it, then answer its calls until it stops us - over one
    connection of the API (the client hands each answer to the call that asked, from any thread). The hooks of a run
    answer on their own wire (_run_wire)."""
    name, token = os.environ.get("KE_PLUGIN_NAME"), os.environ.get("KE_PLUGIN_TOKEN")
    port = int(os.environ.get("KE_EDITOR_PORT", "47801"))
    if not name:
        sys.exit("kinetica.plugin: the editor starts a plugin (Плагины) - it gives it its name and port")
    control = Client(port=port, src=f"plugin:{name}")
    scene = Scene(control)
    commands = []
    for n, c in _commands.items():
        args = _args(c["fn"])
        if c["tool"]:  # the click's three: given by the view, not a field of a form
            args.update({k: v for k, v in _CLICK.items() if k in args})
        if c["opens"] and "path" in args:  # the file to open: given by «Импорт…» or a drop
            args["path"] = {"type": "string", "label": "Файл"}
        commands.append({"name": n, "label": c["label"], "key": c["key"], "tool": c["tool"], "opens": c["opens"], "saves": c["saves"], "obsolete": c["obsolete"],
                         "help": (inspect.getdoc(c["fn"]) or "").strip(),
                         "args": args, "order": [k for k in args if not (c["tool"] and k in _CLICK)]})
    _link.update(client=control, name=name, token=token)
    components = [{"name": m["name"], "label": m["label"], "help": m["help"], "order": list(m["fields"]),
                   "fields": {k: f.schema(k) for k, f in m["fields"].items()}} for m in (c._kinetica for c in _components.values())]
    if os.environ.get("KE_RUN_PORT"):  # the hooks answer on their own wire, taken before the editor is told of the plugin
        _run_wire(scene, int(os.environ["KE_RUN_PORT"]), name, token)
    control.call("plugin_register", name=name, token=token, file=os.path.abspath(file), commands=commands,
                 components=components, **({"panel": _panel} if _panel else {}), **({"events": ["change"]} if _changes else {}))
    while True:
        call = control.call("plugin_wait", name=name, token=token)
        fn = _commands.get(call["command"], {}).get("fn")
        if call["command"] == "@change":  # the plugin's components in the scene changed: its @on_change functions
            fn = lambda scene: [change(scene) for change in _changes] and None  # noqa: E731
        try:
            given = call.get("args", {})
            values = {}  # what the call gave, else the field's default - never a `field` itself
            for arg, p in list(inspect.signature(fn).parameters.items())[1:] if fn else []:
                values[arg] = given[arg] if arg in given else p.default.default if isinstance(p.default, field) else p.default
                if isinstance(p.default, field) and p.default.ref and values[arg] is not None:
                    values[arg] = scene.objects[values[arg]]  # the object, not its number: joint.drive(...), encoder.angle
            with _busy:
                result = fn(scene, **values) if fn else None
            control.call("plugin_reply", name=name, token=token, call=call["call"], result=result if isinstance(result, dict) else {})
        except Exception as error:  # the author's mistake goes back to the editor with its line, the plugin lives on
            control.call("plugin_reply", name=name, token=token, call=call["call"], error=_where(error))


def _run_wire(scene, port, name, token):
    """The run's hooks - on_play, on_tick, on_stop - on a wire and a thread of their own (editor/RunHooks.h): the
    simulation waits for a tick's answer, and this wire takes it there without the editor's window in between. One JSON
    line each way an event, in order: play, every tick, stop. No hooks: no wire. Returns the thread, or None."""
    hooks = [event for event, fns in _hooks.items() if fns] + (["tick:world"] if any(w for _, w in _hooks["tick"]) else [])
    if not hooks:
        return None
    wire = socket.create_connection(("127.0.0.1", port))
    wire.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)  # an answer goes at once: the simulation waits for it
    lines = wire.makefile("rb")

    def say(what):
        wire.sendall((json.dumps(what, ensure_ascii=False) + "\n").encode("utf-8"))

    say({"name": name, "token": token, "hooks": hooks})
    taken = json.loads(lines.readline() or b"{}")
    if not taken.get("ok"):
        sys.exit(f"kinetica.plugin: the run's wire refused the plugin: {taken.get('error', 'closed')}")

    def answer():
        try:
            for line in lines:
                call = json.loads(line)
                _frame.draws, _frame.shown = [], {}
                try:
                    with _busy:
                        commands = _run_hooks(scene, call["event"], call.get("args", {}))
                    say({"call": call["call"], "commands": commands, "draw": _frame.draws, **({"show": _frame.shown} if _frame.shown else {})})
                except Exception as error:  # the author's mistake, with its line; the run goes on
                    say({"call": call["call"], "error": _where(error)})
                _frame.draws = _frame.shown = None
        except OSError:  # the editor closed the wire: the plugin is being stopped
            pass

    thread = threading.Thread(target=answer, name="kinetica-run", daemon=True)
    thread.start()
    return thread


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
