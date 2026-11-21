"""URDF: a robot from its description (ROS) - the links as shapes, the joints as motors that hold the pose they are made
in, with their limits. It comes with the editor (plugins/urdf): «Файл → Импорт…», or a .urdf dropped on the view; an
agent calls ``ed.urdf_load(path=...)``. All of it is one undo step.

What it reads: <link> with <visual> (else <collision>) - box, cylinder, sphere, or a mesh in STL or OBJ (the formats
the editor reads; a DAE visual gives way to the link's collision mesh), its <origin>, <material> colour and the
<inertial> mass; <joint> revolute, continuous, prismatic and fixed with <origin>, <axis> and <limit>. ``package://``
paths are found in the folders above the file. URDF stands z up, the editor y up: the robot is turned so its up is
the editor's up. A link with nothing to show is folded into its parent; one that moves without a shape becomes a small
ball. Masses: the link's mass over its shape's volume (a mesh's own volume - its collider, the hull, is a bit larger).
"""
import math
import os
import re
import struct
import xml.etree.ElementTree as ET

from kinetica.plugin import command

UP = ((1.0, 0.0, 0.0), (0.0, 0.0, 1.0), (0.0, -1.0, 0.0))  # URDF's z up as the editor's y up
I3 = ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))
ENGLISH = os.environ.get("KE_UI_LANGUAGE") == "en"


def mul(a, b):
    return tuple(tuple(sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)) for i in range(3))


def rot(m, v):
    return tuple(sum(m[i][k] * v[k] for k in range(3)) for i in range(3))


def add(a, b):
    return tuple(x + y for x, y in zip(a, b))


def rpy(r, p, y):
    """URDF's roll, pitch, yaw: about x, then y, then z, all fixed axes."""
    cr, sr, cp, sp, cy, sy = math.cos(r), math.sin(r), math.cos(p), math.sin(p), math.cos(y), math.sin(y)
    return ((cy * cp, cy * sp * sr - sy * cr, cy * sp * cr + sy * sr),
            (sy * cp, sy * sp * sr + cy * cr, sy * sp * cr - cy * sr),
            (-sp, cp * sr, cp * cr))


def degrees(m):
    """The editor's rotation words of a turn: yaw about y, then pitch about x, then roll about z (entityRotation)."""
    pitch = math.asin(max(-1.0, min(1.0, -m[1][2])))
    if abs(m[1][2]) < 0.999999:
        yaw, roll = math.atan2(m[0][2], m[2][2]), math.atan2(m[1][0], m[1][1])
    else:  # looking straight up or down: the roll folded into the yaw
        yaw, roll = math.atan2(-m[2][0], m[0][0]), 0.0
    return tuple(math.degrees(a) for a in (pitch, yaw, roll))


def numbers(text, n=3, default=0.0):
    words = (text or "").split()
    return tuple(float(words[i]) if i < len(words) else default for i in range(n))


def pose(element):
    """An <origin>: its turn and place."""
    o = element.find("origin") if element is not None else None
    if o is None:
        return I3, (0.0, 0.0, 0.0)
    return rpy(*numbers(o.get("rpy"))), numbers(o.get("xyz"))


def compose(a, b):
    (ra, pa), (rb, pb) = a, b
    return mul(ra, rb), add(pa, rot(ra, pb))


def read_mesh(path):
    """The box around an STL's or OBJ's points and the volume it closes (the editor reads the same two formats)."""
    with open(path, "rb") as f:
        data = f.read()
    triangles = []
    if path.lower().endswith(".stl"):
        if data[:5].lower() == b"solid" and b"facet" in data[:4096]:
            v = [tuple(map(float, m.split())) for m in re.findall(rb"vertex\s+(\S+\s+\S+\s+\S+)", data)]
            triangles = [v[i:i + 3] for i in range(0, len(v) - 2, 3)]
        else:
            count = struct.unpack_from("<I", data, 80)[0]
            for k in range(count):
                f = struct.unpack_from("<12f", data, 84 + 50 * k)
                triangles.append((f[3:6], f[6:9], f[9:12]))
    else:
        points, faces = [], []
        for line in data.decode("utf-8", "replace").splitlines():
            w = line.split()
            if not w:
                continue
            if w[0] == "v":
                points.append(tuple(float(x) for x in w[1:4]))
            elif w[0] == "f":
                idx = [int(x.split("/")[0]) for x in w[1:]]
                idx = [i - 1 if i > 0 else len(points) + i for i in idx]
                faces += [(idx[0], idx[i], idx[i + 1]) for i in range(1, len(idx) - 1)]
        triangles = [(points[a], points[b], points[c]) for a, b, c in faces]
    if not triangles:
        raise ValueError(f"{os.path.basename(path)}: no triangles")
    lo = [min(t[i][k] for t in triangles for i in range(3)) for k in range(3)]
    hi = [max(t[i][k] for t in triangles for i in range(3)) for k in range(3)]
    volume = abs(sum(a[0] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[0] * c[2] - b[2] * c[0]) + a[2] * (b[0] * c[1] - b[1] * c[0])
                     for a, b, c in triangles)) / 6.0
    return lo, hi, volume


def resolve(filename, base):
    """A mesh's file: ``package://pkg/x`` looked for in the folders above the URDF (as x, or pkg/x); else by its path."""
    if filename.startswith("file://"):
        filename = filename[7:]
    if filename.startswith("package://"):
        package, _, rest = filename[10:].partition("/")
        folder = base
        while True:
            for candidate in (os.path.join(folder, rest), os.path.join(folder, package, rest)):
                if os.path.isfile(candidate):
                    return candidate
            if os.path.dirname(folder) == folder:
                return None
            folder = os.path.dirname(folder)
    path = filename if os.path.isabs(filename) else os.path.join(base, filename)
    return path if os.path.isfile(path) else None


def colour_of(visual, materials):
    m = visual.find("material") if visual is not None else None
    if m is None:
        return None
    c = m.find("color")
    rgba = c.get("rgba") if c is not None else materials.get(m.get("name"))
    return numbers(rgba, 3, 0.7) if rgba else None


def shape_of(link, base, materials, notes):
    """The link's shape: its first visual the editor can draw, else its collision; None - nothing to show."""
    colour = colour_of(link.find("visual"), materials)
    for tag in ("visual", "collision"):
        for element in link.findall(tag):
            g = element.find("geometry")
            if g is None:
                continue
            frame = pose(element)
            if g.find("box") is not None:
                size = numbers(g.find("box").get("size"))
                return {"kind": "box", "frame": frame, "size": size, "volume": size[0] * size[1] * size[2], "colour": colour}
            if g.find("sphere") is not None:
                r = float(g.find("sphere").get("radius"))
                return {"kind": "sphere", "frame": frame, "size": (2 * r,) * 3, "volume": 4 / 3 * math.pi * r ** 3, "colour": colour}
            if g.find("cylinder") is not None:
                r, h = float(g.find("cylinder").get("radius")), float(g.find("cylinder").get("length"))
                upright = ((1.0, 0.0, 0.0), (0.0, 0.0, -1.0), (0.0, 1.0, 0.0))  # the editor's cylinder stands on its y, URDF's on z
                return {"kind": "cylinder", "frame": (mul(frame[0], upright), frame[1]), "size": (2 * r, h, 2 * r),
                        "volume": math.pi * r * r * h, "colour": colour}
            if g.find("mesh") is not None:
                name = g.find("mesh").get("filename", "")
                path = resolve(name, base)
                if path is None or not path.lower().endswith((".stl", ".obj")):  # a DAE, or no file: the next geometry
                    notes.append(f"{tag} {os.path.splitext(name)[1].lower() or name}")
                    continue
                s = numbers(g.find("mesh").get("scale"), 3, 1.0)
                lo, hi, volume = read_mesh(path)
                centre = tuple((lo[k] + hi[k]) / 2 * s[k] for k in range(3))
                largest = max((hi[k] - lo[k]) * abs(s[k]) for k in range(3))
                return {"kind": "mesh", "file": path, "frame": (frame[0], add(frame[1], rot(frame[0], centre))),
                        "size": (largest,) * 3, "volume": volume * abs(s[0] * s[1] * s[2]), "colour": colour}
    return None


def word(x):
    return f"{x:.7g}"


def plural(n, one, few, many):
    """«1 звено», «2 звена», «5 звеньев»."""
    if n % 10 == 1 and n % 100 != 11:
        return f"{n} {one}"
    return f"{n} {few if 2 <= n % 10 <= 4 and not 12 <= n % 100 <= 14 else many}"


@command(label="URDF Robot" if ENGLISH else "URDF — робот", opens=[".urdf"])
def load(scene, path):
    """A robot from a URDF file: links as shapes in a group named after the robot, joints as motors holding the pose."""
    ed = scene.ed
    root = ET.parse(path).getroot()
    base = os.path.dirname(os.path.abspath(path))
    materials = {m.get("name"): m.find("color").get("rgba") for m in root.findall("material") if m.find("color") is not None}
    links = {l.get("name"): l for l in root.findall("link")}
    joints = root.findall("joint")
    parent_of = {j.find("child").get("link"): j for j in joints}
    notes = []

    world = {}  # link -> its frame in URDF's world, the robot's root at the origin

    def frame(name):
        if name not in world:
            j = parent_of.get(name)
            world[name] = (I3, (0.0, 0.0, 0.0)) if j is None else compose(frame(j.find("parent").get("link")), pose(j))
        return world[name]

    moving = {j.find("child").get("link") for j in joints if j.get("type") in ("revolute", "continuous", "prismatic")}
    group = ed.scene_put(block=f'group "{root.get("name", "робот")}"\n  object id 0 visible 1 locked 0\n'
                               "  pose position 0 0 0 rotation 0 0 0 color 0.8 0.8 0.8\nend\n")["id"]
    shapes = {name: shape_of(link, base, materials, notes) for name, link in links.items()}

    def shown_above(j):
        """Whether a link above this fixed joint shows something: then the link is welded to it, not fixed in the world."""
        link = j.find("parent").get("link")
        while link is not None:
            if shapes.get(link) is not None:
                return True
            up = parent_of.get(link)
            link = up.find("parent").get("link") if up is not None else None
        return False

    entity = {}  # link -> the shape made of it
    for name, link in links.items():
        shape = shapes[name]
        if shape is None and name in moving:  # it moves but shows nothing: a small ball carries its mass
            shape = {"kind": "sphere", "frame": (I3, (0.0, 0.0, 0.0)), "size": (0.03,) * 3, "volume": 4 / 3 * math.pi * 0.015 ** 3, "colour": None}
        if shape is None:
            continue
        turn, place = compose(frame(name), shape["frame"])
        inertial = link.find("inertial")
        mass = float(inertial.find("mass").get("value")) if inertial is not None and inertial.find("mass") is not None else 0.0
        density = min(50000.0, max(10.0, mass / shape["volume"])) if mass > 0 and shape["volume"] > 0 else 1000.0
        j = parent_of.get(name)
        fixed = j is None or (j.get("type") == "fixed" and not shown_above(j))
        r, g, b = shape["colour"] or (0.7, 0.7, 0.72)
        where = rot(UP, place)
        turn_words = degrees(mul(UP, turn))
        file = f' file "{shape["file"]}"' if shape["kind"] == "mesh" else ""
        block = (f'entity "{name.replace(chr(34), chr(39))}"\n'
                 f"  object id 0 visible 1 locked 0 parent {group}\n"
                 f"  shape {shape['kind']}{file} size {' '.join(map(word, shape['size']))} position {' '.join(map(word, where))} "
                 f"rotation {' '.join(map(word, turn_words))} color {word(r)} {word(g)} {word(b)}\n"
                 f"  rigid density {word(density)} friction 0.8 restitution 0.1 fixed {int(fixed)}{f' mass {word(mass)}' if mass > 0 else ''}\n"
                 f"  collider kind {'hull' if shape['kind'] == 'mesh' else 'auto'} fit 1\n"
                 "end\n")
        entity[name] = ed.scene_put(block=block)["id"]

    def holder(link):
        """The shape a link hangs on: its own, else the nearest above it with one (0 - the world)."""
        while link is not None:
            if link in entity:
                return entity[link]
            j = parent_of.get(link)
            link = j.find("parent").get("link") if j is not None else None
        return 0

    made = 0
    for j in joints:
        kind, child, parent = j.get("type"), j.find("child").get("link"), j.find("parent").get("link")
        if child not in entity:
            continue
        turn, at = compose(frame(parent), pose(j))
        anchor = list(rot(UP, at))
        if kind == "fixed":
            if holder(parent):
                ed.assembly_joint(a=entity[child], b=holder(parent), type="weld", anchor=anchor)
                made += 1
            continue
        if kind not in ("revolute", "continuous", "prismatic"):
            notes.append(f"Skipped joint {j.get('name')} ({kind})" if ENGLISH else f"сустав {j.get('name')} ({kind}) пропущен")
            continue
        a = j.find("axis")
        axis = rot(UP, rot(turn, numbers(a.get("xyz") if a is not None else "1 0 0")))
        length = math.sqrt(sum(x * x for x in axis)) or 1.0
        answer = ed.assembly_actuator(kind="linear" if kind == "prismatic" else "rotary", a=entity[child], b=holder(parent),
                                      axis=[x / length for x in axis], anchor=anchor)
        made += 1
        limit = j.find("limit")
        if kind != "continuous" and limit is not None and limit.get("lower") is not None:
            lower, upper = float(limit.get("lower")), float(limit.get("upper", "0"))
            if kind == "revolute" and upper - lower >= 2 * math.pi - 1e-3:
                continue  # a whole turn or more: free, as a continuous one
            dof = "tx" if kind == "prismatic" else "rx"
            text = ed.scene_get(id=answer["id"])["text"]
            line = f"  limit {dof} lower {word(lower)} upper {word(upper)}"
            text, n = re.subn(rf"^  (?:free|limit) {dof}\b.*$", line, text, flags=re.M)
            if not n:
                text = re.sub(r"^(  link .*)$", lambda m: m.group(1) + "\n" + line, text, count=1, flags=re.M)
            ed.scene_put(block=text)
    ed.selection_set(ids=[group])
    if ENGLISH:
        count = len(entity)
        said = f"{root.get('name', 'robot')}: {count} {'link' if count == 1 else 'links'}, {made} {'joint' if made == 1 else 'joints'}"
        visuals = [n[7:] for n in notes if n.startswith("visual ")]
        warnings = [f"Missing collision mesh {n[10:]}" if n.startswith("collision ") else n
                    for n in notes if not n.startswith("visual ")]
        if visuals:
            formats = ", ".join(sorted(set(visuals)))
            warnings.append(f"Missing or unreadable visual meshes ({formats}); showing collision geometry")
        return {"group": group, "links": count, "joints": made,
                "note": said + ("; " + "; ".join(sorted(set(warnings))) if warnings else "")}
    gave_way = [n for n in notes if n.startswith("visual ")]  # "visual .dae": drawn by its collision mesh instead
    notes = [n for n in notes if not n.startswith("visual ")] + (
        [f"{plural(len(gave_way), 'меш', 'меша', 'мешей')} {', '.join(sorted({n[7:] for n in gave_way}))} не читаются или нет файла — "
         "показаны формы столкновений"] if gave_way else [])
    notes = [("нет меша " + n[10:]) if n.startswith("collision ") else n for n in notes]
    said = f"«{root.get('name', 'робот')}»: {plural(len(entity), 'звено', 'звена', 'звеньев')}, {plural(made, 'сустав', 'сустава', 'суставов')}"
    return {"group": group, "links": len(entity), "joints": made, "note": said + ("; " + "; ".join(sorted(set(notes))) if notes else "")}

