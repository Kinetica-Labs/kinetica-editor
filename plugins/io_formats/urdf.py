"""URDF: a robot from its description - the links as shapes, the joints as motors that hold the pose they are made
in, with their limits. It comes with the editor (plugins/io_formats, urdf.py): «Файл → Импорт…», or a .urdf dropped on the view; an
agent calls ``ed.io_formats_urdf_load(path=...)``. All of it is one undo step.

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



# --- Out: the scene's robot as URDF ------------------------------------------------------------------------------------
# The inverse of load: each rigid body a <link>, each joint a <joint>; a link's frame is its joint's frame (URDF turns a
# child about its joint's origin), its shape the <visual> and <collision> under it; the editor's y up turned back to
# URDF's z up; meshes copied beside the file into <name>-meshes/. What URDF cannot say is said in the note.

def turn(pitch, yaw, roll):
    """The editor's rotation words as a matrix: Ry(yaw) Rx(pitch) Rz(roll) (the inverse of degrees)."""
    def about(axis, a):
        c, s = math.cos(math.radians(a)), math.sin(math.radians(a))
        if axis == 0:
            return ((1.0, 0.0, 0.0), (0.0, c, -s), (0.0, s, c))
        if axis == 1:
            return ((c, 0.0, s), (0.0, 1.0, 0.0), (-s, 0.0, c))
        return ((c, -s, 0.0), (s, c, 0.0), (0.0, 0.0, 1.0))
    return mul(mul(about(1, yaw), about(0, pitch)), about(2, roll))


def transpose(m):
    return tuple(tuple(m[j][i] for j in range(3)) for i in range(3))


def inverse(frame):
    r, p = frame
    t = transpose(r)
    return t, tuple(-x for x in rot(t, p))


def roll_pitch_yaw(m):
    """URDF's rpy of a matrix R = Rz(yaw) Ry(pitch) Rx(roll) (the inverse of rpy)."""
    pitch = math.asin(max(-1.0, min(1.0, -m[2][0])))
    if abs(m[2][0]) < 0.999999:
        return math.atan2(m[2][1], m[2][2]), pitch, math.atan2(m[1][0], m[0][0])
    return 0.0, pitch, math.atan2(-m[0][1], m[1][1]) # gimbal lock: the roll folded into the yaw


def after(words, key, n):
    """The n numbers after a key in a line read by scene_get (a list of words)."""
    k = words.index(key)
    return tuple(float(x) for x in words[k + 1:k + 1 + n])


def clean(x):
    """A number for the file: the rounding of a turn's sines and cosines (3e-18, -0) is 0."""
    return word(0.0 if abs(x) < 1e-9 else x)


def origin(element, frame):
    r, p = frame
    ET.SubElement(element, "origin", xyz=" ".join(clean(x) for x in p), rpy=" ".join(clean(x) for x in roll_pitch_yaw(r)))


def save(scene, path):
    """The scene's rigid bodies and joints as one URDF robot; meshes copied beside the file. Nothing in the scene changes."""
    ed = scene.ed
    stem = os.path.splitext(os.path.basename(path))[0]
    folder = os.path.dirname(os.path.abspath(path))
    notes = set()
    objects = {o["id"]: o for o in ed.scene_list()["objects"]}
    lines, raw = {}, {}

    def read(i):
        """An object's lines by their first word (the first of each); raw[i] keeps them all, in order."""
        if i not in lines:
            raw[i] = ed.scene_get(id=i)["lines"][1:]
            lines[i] = {}
            for w in raw[i]:
                lines[i].setdefault(w[0], w)
        return lines[i]

    def world(i):
        """An object's frame in the scene: its own pose composed under its parents'."""
        l = read(i)
        own = l.get("shape") or l.get("pose")
        here = (turn(*after(own, "rotation", 3)), after(own, "position", 3)) if own else (I3, (0.0, 0.0, 0.0))
        parent = after(l["object"], "parent", 1)[0] if "parent" in l["object"] else 0
        return compose(world(int(parent)), here) if parent else here

    bodies = [i for i, o in objects.items() if o.get("kind") == "entity" and "rigid" in read(i)]
    joints = [i for i, o in objects.items() if o.get("kind") == "joint"]
    parent_of = {}
    for j in joints:
        link = read(j)["link"]
        a, b = int(after(link, "a", 1)[0]), int(after(link, "b", 1)[0])
        if a in parent_of or a not in bodies or (b and b not in bodies):
            notes.add("сустав с телом, у которого уже есть родитель, пропущен: URDF - дерево" if a in parent_of else "сустав не между телами пропущен")
            continue
        parent_of[a] = j

    names, frames = {}, {0: (UP, (0.0, 0.0, 0.0))}  # the world: URDF's axes as the editor has them (y up)
    for i in bodies:
        name, n = objects[i].get("name") or f"link{i}", 2
        while name in names.values():
            name, n = f"{objects[i].get('name')} {n}", n + 1
        names[i] = name
        j = parent_of.get(i)
        frames[i] = world(i) if j is None else compose(world(i), (turn(*after(read(j)["link"], "frame_a", 3)), after(read(j)["link"], "anchor_a", 3)))

    robot = ET.Element("robot", name=stem)
    ET.SubElement(robot, "link", name="world")
    meshes = os.path.join(folder, stem + "-meshes")
    for i in bodies:
        l = read(i)
        shape, kind = l["shape"], l["shape"][1]
        size = after(shape, "size", 3)
        link = ET.SubElement(robot, "link", name=names[i])
        inside = compose(inverse(frames[i]), world(i))  # the body in its link's frame
        geometry, volume = None, 0.0
        if kind == "box":
            geometry, volume = ("box", {"size": " ".join(word(x) for x in size)}), size[0] * size[1] * size[2]
        elif kind == "sphere":
            geometry, volume = ("sphere", {"radius": word(size[0] / 2)}), 4 / 3 * math.pi * (size[0] / 2) ** 3
        elif kind == "cylinder":
            upright = ((1.0, 0.0, 0.0), (0.0, 0.0, -1.0), (0.0, 1.0, 0.0))  # URDF's cylinder stands on its z, the editor's on y
            inside = (mul(inside[0], transpose(upright)), inside[1])
            geometry, volume = ("cylinder", {"radius": word(size[0] / 2), "length": word(size[1])}), math.pi * (size[0] / 2) ** 2 * size[1]
        elif kind == "mesh":
            source = shape[shape.index("file") + 1]
            if os.path.isfile(source):
                lo, hi, closed = read_mesh(source)
                k = max(size) / (max(hi[m] - lo[m] for m in range(3)) or 1.0)  # the editor scales a mesh so its largest side is the size's
                centre = tuple((lo[m] + hi[m]) / 2 * k for m in range(3))
                inside = (inside[0], add(inside[1], tuple(-x for x in rot(inside[0], centre))))  # the file's points, not its centre
                os.makedirs(meshes, exist_ok=True)
                target = os.path.join(meshes, os.path.basename(source))
                if os.path.abspath(source) != os.path.abspath(target):
                    with open(source, "rb") as f, open(target, "wb") as out:
                        out.write(f.read())
                geometry, volume = ("mesh", {"filename": f"{stem}-meshes/{os.path.basename(source)}", "scale": " ".join([word(k)] * 3)}), closed * k ** 3
            else:
                notes.add(f"нет файла сетки {os.path.basename(source)}")
        else:
            notes.add(f"форма {kind} не выражается в URDF: звено без формы")
        rigid = l["rigid"]
        mass = after(rigid, "mass", 1)[0] if "mass" in rigid else after(rigid, "density", 1)[0] * volume
        if geometry:
            colour = after(shape, "color", 3) if "color" in shape else (0.7, 0.7, 0.72)
            for tag in ("visual", "collision"):
                element = ET.SubElement(link, tag)
                origin(element, inside)
                ET.SubElement(ET.SubElement(element, "geometry"), geometry[0], **geometry[1])
                if tag == "visual":
                    ET.SubElement(ET.SubElement(element, "material", name=f"color_{i}"), "color",
                                  rgba=" ".join(word(x) for x in colour) + " 1")
        if mass > 0:
            inertial = ET.SubElement(link, "inertial")
            origin(inertial, inside)
            ET.SubElement(inertial, "mass", value=word(mass))
            x, y, z = (size[0], size[2], size[1]) if kind == "cylinder" else size  # in the shape's own URDF frame
            if kind == "sphere":
                d = 0.4 * mass * (size[0] / 2) ** 2
                ixx = iyy = izz = d
            elif kind == "cylinder":
                r = size[0] / 2
                ixx = iyy = mass * (3 * r * r + size[1] ** 2) / 12
                izz = mass * r * r / 2
            else:  # a box, a mesh by its box
                ixx, iyy, izz = mass * (y * y + z * z) / 12, mass * (x * x + z * z) / 12, mass * (x * x + y * y) / 12
            ET.SubElement(inertial, "inertia", ixx=word(ixx), iyy=word(iyy), izz=word(izz), ixy="0", ixz="0", iyz="0")

    made = 0
    for i in bodies:
        j = parent_of.get(i)
        if j is None:  # a root: fixed in the world, or free (URDF's floating joint)
            fixed = after(read(i)["rigid"], "fixed", 1)[0] if "fixed" in read(i)["rigid"] else 0
            element = ET.SubElement(robot, "joint", name=f"{names[i]}_to_world", type="fixed" if fixed else "floating")
            ET.SubElement(element, "parent", link="world")
            ET.SubElement(element, "child", link=names[i])
            origin(element, compose(inverse(frames[0]), frames[i]))
            if not fixed:
                notes.add("свободное тело - сустав floating, его понимают не все программы")
            continue
        l = read(j)
        b = int(after(l["link"], "b", 1)[0])
        dofs = [w[1] for w in raw[j] if w[0] in ("free", "limit")]
        kind = "fixed"
        if len(dofs) == 1 and dofs[0] in ("rx", "ry", "rz"):
            kind = "revolute" if l.get("limit") else "continuous"
        elif len(dofs) == 1 and dofs[0] in ("tx", "ty", "tz"):
            kind = "prismatic"
        elif dofs:
            notes.add("сустав с несколькими степенями свободы записан как fixed: в URDF их нет")
        element = ET.SubElement(robot, "joint", name=objects[j].get("name") or f"joint{j}", type=kind)
        ET.SubElement(element, "parent", link=names[b] if b else "world")
        ET.SubElement(element, "child", link=names[i])
        origin(element, compose(inverse(frames[b]), frames[i]))
        if kind != "fixed":
            axis = {"x": "1 0 0", "y": "0 1 0", "z": "0 0 1"}[dofs[0][1]]
            ET.SubElement(element, "axis", xyz=axis)
            limit = l.get("limit")
            lower, upper = (after(limit, "lower", 1)[0], after(limit, "upper", 1)[0]) if limit else (-1.0, 1.0)
            if kind == "prismatic" and not limit:
                notes.add("у сдвигового сустава без предела записан предел ±1 м: URDF его требует")
            attrs = {"effort": "1000", "velocity": "10"} | ({} if kind == "continuous" else {"lower": word(lower), "upper": word(upper)})
            ET.SubElement(element, "limit", **attrs)
        made += 1
    ET.indent(robot)
    ET.ElementTree(robot).write(path, encoding="utf-8", xml_declaration=True)
    if any(any(ord(ch) > 127 for ch in n) for n in names.values()):
        notes.add("имена звеньев не латиницей: ROS и его инструменты ждут латиницу")
    said = f"«{stem}.urdf»: {plural(len(bodies), 'звено', 'звена', 'звеньев')}, {plural(made, 'сустав', 'сустава', 'суставов')}"
    return {"links": len(bodies), "joints": made, "note": said + ("; " + "; ".join(sorted(notes)) if notes else "")}
