"""The scene as an object: what is in it, its world, time, and undo.

    scene = kinetica.connect()
    car = scene.objects["Шасси"]          # by name, or by id: scene.objects[166]
    scene.run(1.0)                        # a second of simulation, then paused
    scene.stop()                          # back to the scene as it was before it ran
"""
import json

from .client import Client
from .core import Core
from .objects import (Battery, Body, Camera, Controller, Driver, Effort, Encoder, Group, Imu, Joint, PointWheel, SceneObject, Sensor, Sonar, Touch, Wrench, _axis,
                      _q, _t)

_KINDS = {"entity": Body, "group": Group, "camera": Camera, "joint": Joint, "battery": Battery, "driver": Driver, "wheel": PointWheel, "controller": Controller}
_SENSORS = {"encoder": Encoder, "imu": Imu, "sonar": Sonar, "touch": Touch, "wrench": Wrench, "effort": Effort, "stereo": Sensor}


def connect(port=None, host="127.0.0.1"):
    """The scene of the Kinetica editor serving its API on `port` (KE_EDITOR_PORT, else 47801)."""
    return Scene(Client(host, port))


class Objects:
    """The scene's objects: iterate them, or take one by id or by name."""

    def __init__(self, scene):
        self._scene = scene

    def _list(self):
        return self._scene._call("scene_list")["objects"]

    def _wrap(self, row):
        kind, id = row["kind"], row["id"]
        if kind == "sensor":
            have = {words[0] for words in self._scene._call("scene_get", id=id)["lines"]}
            word = next((w for w in _SENSORS if w in have), "encoder")
            return _SENSORS[word](self._scene, id)
        return _KINDS.get(kind, SceneObject)(self._scene, id)

    def __getitem__(self, key):
        for row in self._list():
            if row["id"] == key or row["name"] == key:
                return self._wrap(row)
        raise KeyError(key)

    def __iter__(self):
        return iter([self._wrap(row) for row in self._list()])

    def tagged(self, word):
        """The objects marked with the tag: ``for o in scene.objects.tagged("target"): ...``"""
        return [self._wrap(row) for row in self._scene._call("scene_list", tag=word)["objects"]]

    def __len__(self):
        return len(self._list())

    def __contains__(self, key):
        return any(row["id"] == key or row["name"] == key for row in self._list())


class World:
    """Gravity, the box and the physics engine's settings: the scene file's "world" and "physics" lines."""

    def __init__(self, scene):
        self._scene = scene

    def _get(self, line, key, count=1):
        values = self._scene._call("world_get", line=line, key=key, count=count)["values"]
        return values[0] if count == 1 else tuple(values)

    def _put(self, line, key, values, py):
        self._scene._call("world_set", py, line=line, key=key, values=values)

    @property
    def gravity(self):
        return self._get("world", "gravity", 3)

    @gravity.setter
    def gravity(self, g):
        self._put("world", "gravity", list(g), f"scene.world.gravity = {tuple(g)}")

    @property
    def size(self):
        """The box everything lives in, m (the floor at y = 0, the walls at +-x/2, +-z/2)."""
        return self._get("world", "size", 3)

    @size.setter
    def size(self, s):
        self._put("world", "size", list(s), f"scene.world.size = {tuple(s)}")

    @property
    def substeps(self):
        """The physics engine's substeps per frame of 1/60 s."""
        return int(self._get("physics", "substeps"))

    @substeps.setter
    def substeps(self, n):
        self._put("physics", "substeps", [int(n)], f"scene.world.substeps = {int(n)}")


class Editor:
    """The editor's window as objects - what exists because there is a screen and a hand: the selection
    (what an action without objects works on), the gizmo's tool and axes, the view's camera. Changing them
    changes no scene and is no undo step."""

    def __init__(self, scene):
        self.scene = scene
        self.gizmo = Gizmo(scene)
        self.view = View(scene)

    @property
    def selection(self):
        """The selected objects, the active one last."""
        return self.scene._objects(self.scene._call("selection_get")["ids"])

    @selection.setter
    def selection(self, objects):
        self.select(*objects)

    def select(self, *objects):
        """These objects selected as clicks would, the last one active; none: nothing selected."""
        self.scene._call("selection_set", f"editor.select({Scene._refs(objects)})", ids=[o.id for o in objects])


class Gizmo:
    """The gizmo's tool (select, move, rotate, scale) and its axes (the object's own, or the world's)."""

    def __init__(self, scene):
        self.scene = scene

    def _get(self, key):
        return self.scene._call("gizmo_get")[key]

    tool = property(lambda self: self._get("tool"),
                    lambda self, v: self.scene._call("gizmo_set", f"editor.gizmo.tool = {_q(v)}", tool=v))
    local = property(lambda self: self._get("local"),
                     lambda self, v: self.scene._call("gizmo_set", f"editor.gizmo.local = {bool(v)}", local=bool(v)))


class View:
    """The view's camera: where the eye is and the point it orbits."""

    def __init__(self, scene):
        self.scene = scene

    eye = property(lambda self: tuple(self.scene._call("view_get")["eye"]))
    target = property(lambda self: tuple(self.scene._call("view_get")["target"]))

    def look(self, eye, target):
        """The eye at `eye`, looking at `target`."""
        self.scene._call("view_look", f"editor.view.look({_t(eye)}, {_t(target)})", eye=list(eye), target=list(target))

    def frame(self, obj=None):
        """The object in view, selected - or the whole scene."""
        self.scene._call("view_frame", f"editor.view.frame({obj.ref if obj else ''})", **({"id": obj.id} if obj else {}))


class Operations:
    """ed.<operation>(**args): any operation of the editor by its name - the console's ``ed`` and a plugin's ``scene.ed``
    are the same, so a line tried in the console is pasted into a plugin as it is."""

    def __init__(self, scene):
        self._scene = scene

    def __getattr__(self, name):
        if name.startswith("_"):
            raise AttributeError(name)
        return lambda **args: self._scene._call(name, **args)


class Scene:
    """The scene as objects. ``Scene()`` starts kinetica-core, a world with no window (the clock, the selection and the
    view are the editor's: their operations answer that kinetica-core has none); ``connect()`` is the scene of a window.
    ``Scene().open(path)`` begins with a file; ``close()`` or a ``with`` block ends the program."""

    def __init__(self, client=None, *, executable=None):
        if isinstance(client, str):
            raise TypeError("Scene() takes a connection or nothing; to open a file: Scene().open(path)")
        self._client = client if client is not None else Core(executable)  # the wire (client.py, core.py): a call goes through an operation's own method or `ed`, never past them
        self.objects = Objects(self)
        self.world = World(self)
        self.editor = Editor(self)
        self.ed = Operations(self)

    def close(self):
        self._client.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def _call(self, op, py=None, **args):
        return self._client.call(op, py, **args)

    def _live(self, part, id):
        state = self._call("sim_state")
        return next((x for x in state.get(part, []) if x.get("id") == id), None)

    def _of(self, cls):
        return [o for o in self.objects if isinstance(o, cls)]

    bodies = property(lambda self: self._of(Body))
    joints = property(lambda self: self._of(Joint))
    sensors = property(lambda self: self._of(Sensor))
    batteries = property(lambda self: self._of(Battery))
    drivers = property(lambda self: self._of(Driver))
    controllers = property(lambda self: self._of(Controller))

    def new(self):
        """An empty scene: the floor and nothing else."""
        self._call("scene_new", "scene.new()")

    def open(self, file):
        """A scene file opened (one undo step back to the scene before)."""
        self._call("scene_open", f"scene.open({json.dumps(file, ensure_ascii=False)})", file=file)

    def save(self, file):
        self._call("scene_save", f"scene.save({json.dumps(file, ensure_ascii=False)})", file=file)

    def sessions(self):
        """The editor's journal: every session - id, start, end, lines, edits, crashed (no end, no window holds it)."""
        return self._call("journal_sessions")["sessions"]

    def restore(self, session, line=None):
        """The scene of a journal's session after its line `line` (None: its last) - after a crash, or as it was an hour
        ago; one undo step."""
        args = {"session": session} if line is None else {"session": session, "line": int(line)}
        self._call("journal_restore", f"scene.restore({_q(session)}{'' if line is None else f', {int(line)}'})", **args)

    def put(self, block):
        """An object from its block of the scene file (new, or the one of its id replaced): the object."""
        return self.objects[self._call("scene_put", block=block)["id"]]

    def _objects(self, ids):
        return [self.objects[id] for id in ids]

    @staticmethod
    def _refs(objects):
        return ", ".join(o.ref for o in objects)

    def add(self, shape="box", name=None, position=None, size=None, role=None, fixed=None, kinematic=None):
        """A shape as the toolbar's button adds it - in the air over a free spot, named "Куб 2" - or with what is given:
        its body. No role: geometry that stands where it is put; role="rigid": a body that falls (fixed=True: held still
        in the world; kinematic=True: it moves as its velocity says and pushes what it meets); role="collider": an obstacle."""
        given = {k: v for k, v in (("name", name), ("position", position), ("size", size), ("role", role), ("fixed", fixed),
                                   ("kinematic", kinematic)) if v is not None}
        show = {"name": _q, "role": _q, "fixed": repr, "kinematic": repr}  # the rest are three numbers
        shown = "".join(f", {k}={show.get(k, tuple)(v)}" for k, v in given.items())
        return self.objects[self._call("scene_add", f"scene.add({_q(shape)}{shown})", shape=shape,
                                       **{k: v if k in show else list(v) for k, v in given.items()})["id"]]

    def add_light(self, kind="sun", at=None, position=None, target=None):
        """The sun high at a slant, a lamp ("point") or a spot ("spot") over `at` - else over the selected
        object or the scene's centre; with `position` it stands there, shining at `target`."""
        args = {k: list(v) for k, v in (("at", at), ("position", position), ("target", target)) if v is not None}
        shown = "".join(f", {k}={_t(v)}" for k, v in args.items())
        return self.objects[self._call("scene_add_light", f"scene.add_light({_q(kind)}{shown})", kind=kind, **args)["id"]]

    def add_camera(self, eye=None, target=None, kind=None, at=None):
        """A camera at `eye` (else the editor's eye), looking at `target` (else the scene's centre). kind="target": a
        target camera that always looks there - or at object `at`, which it follows in play."""
        args = {k: list(v) for k, v in (("eye", eye), ("target", target)) if v is not None}
        shown = ", ".join(f"{k}={_t(v)}" for k, v in args.items())
        if kind is not None:
            args["kind"] = kind
            shown += (", " if shown else "") + f'kind="{kind}"'
        if at is not None:
            args["at"] = at.id
            shown += (", " if shown else "") + f"at={at.ref}"
        return self.objects[self._call("scene_add_camera", f"scene.add_camera({shown})", **args)["id"]]

    def _act(self, op, method, objects):
        objects = list(objects)
        line = f"{objects[0].ref}.{method}()" if len(objects) == 1 else f"scene.{method}({self._refs(objects)})"
        return self._call(op, line, ids=[o.id for o in objects])

    def delete(self, *objects):
        """Out of the scene as Del does it: with everything under them, the joints and sensors they hold."""
        self._act("scene_delete", "delete", objects)

    def duplicate(self, *objects):
        """Copies a little above the originals (Ctrl+D): the copies."""
        return self._objects(self._act("scene_duplicate", "duplicate", objects)["ids"])

    def group(self, *objects):
        """A group at their centre, their poses kept in the world (Ctrl+G): the group."""
        return self.objects[self._call("scene_group", f"scene.group({self._refs(objects)})", ids=[o.id for o in objects])["id"]]

    def ungroup(self, *groups):
        """The groups dissolved, their members kept where they stand: the members."""
        return self._objects(self._act("scene_ungroup", "ungroup", groups)["ids"])

    def make_array(self, *shapes, **row):
        """Each shape becomes the hidden template of a row of copies - ``count`` of them (5), ``step`` apart (its width
        and a gap, along x): the arrays. ``scene.make_array(brick, count=10, step=(0, 0.11, 0))``."""
        shown = "".join(f", {key}={value!r}" for key, value in row.items())
        return self._objects(self._call("scene_array", f"scene.make_array({self._refs(shapes)}{shown})", ids=[o.id for o in shapes],
                                        **{key: list(value) if key == "step" else value for key, value in row.items()})["ids"])

    def show_all(self):
        """Every hidden object shown again (Alt+H)."""
        self._call("scene_show_all", "scene.show_all()")

    def move(self, *objects, by):
        """Objects moved together by a step in the world, m (a gizmo drag of several)."""
        self._call("scene_move", f"scene.move({self._refs(objects)}, by={_t(by)})", ids=[o.id for o in objects], by=list(by))

    def turn(self, *objects, axis, degrees, pivot=None):
        """Objects turned together about an axis by degrees, around the last one's centre or `pivot`."""
        extra = {"pivot": list(pivot)} if pivot is not None else {}
        shown = f", pivot={_t(pivot)}" if pivot is not None else ""
        self._call("scene_turn", f"scene.turn({self._refs(objects)}, axis={_axis(axis)}, degrees={degrees}{shown})",
                   ids=[o.id for o in objects], axis=axis if isinstance(axis, str) else list(axis), degrees=degrees, **extra)

    def scale(self, *objects, by):
        """Objects scaled together by factors along their own axes."""
        shown = by if isinstance(by, (int, float)) else _t(by)
        self._call("scene_scale", f"scene.scale({self._refs(objects)}, by={shown})", ids=[o.id for o in objects],
                   by=by if isinstance(by, (int, float)) else list(by))

    def describe(self):
        """The scene in words: the bodies as a tree of joints, their shapes, masses, roles and drives; the
        sensors and where they look; the batteries."""
        return self._call("scene_describe")["text"]

    def play(self):
        self._call("sim_play", "scene.play()")

    def pause(self):
        self._call("sim_pause", "scene.pause()")

    def stop(self):
        """The world dropped: the scene is as it was before it ran, except that an edit which stopped the world (a body
        added, a joint changed) is kept; an edit the world took while it ran is gone with it."""
        self._call("sim_stop", "scene.stop()")

    def step(self, frames=1):
        """`frames` frames of 1/60 s, then paused; the state after them. One call takes at most 6000 frames (100 s):
        more is cut to 6000 and the answer's completed_frames says how many ran."""
        return self._call("sim_step", f"scene.step({frames})", frames=frames)

    def run(self, seconds):
        """`seconds` of simulation (whole frames, at least one, at most 100 s per call), then paused; the state after them."""
        frames = max(1, round(seconds * 60))
        return self._call("sim_step", f"scene.run({seconds})", frames=frames)

    def measure(self, scene=False, objects=(), group=(), joints=(), goals=()):
        """The plots' channels measured now - the physics engine's truth, for checks, not a robot's sensors: the scene's
        energy and momentum, bodies, a group of bodies as one, joints, IK goals. {channel id: value}."""
        ids = lambda xs: [getattr(x, "id", x) for x in xs]
        args = {k: v for k, v in (("objects", ids(objects)), ("group", ids(group)), ("joints", ids(joints)), ("goals", ids(goals))) if v}
        answer = self._call("sim_measure", scene=bool(scene), **args)
        return {c["id"]: c["value"] for c in answer["channels"]}

    def push(self, body, impulse, point=None):
        """One impulse on a running body, N s (at `point`, world metres; else at its centre of mass)."""
        extra = {"point": list(point)} if point is not None else {}
        self._call("sim_impulse", f"scene.push({body.ref}, {_t(impulse)})", body=body.id, impulse=list(impulse), **extra)

    @property
    def time(self):
        return self._call("sim_state").get("time")

    def undo(self):
        self._call("edit_undo", "scene.undo()")

    def redo(self):
        self._call("edit_redo", "scene.redo()")

    def screenshot(self, file, window=False):
        return self._call("view_screenshot", file=file, window=window)
