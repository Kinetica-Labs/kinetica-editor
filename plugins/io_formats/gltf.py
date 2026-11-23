"""glTF: a scene from the open 3D format most programs write (.gltf, .glb) - its node tree as groups and shapes, each
mesh as a shape the editor draws, the material's colour, and its animations of place and turn as sequences. It comes
with the editor (plugins/io_formats, gltf.py): «Файл → Импорт…», or a .gltf / .glb dropped on the view; an agent calls
``ed.io_formats_gltf_load(path=...)``. All of it is one undo step.

What it reads: glTF 2.0 - a .gltf with its buffers (files beside it or data: URIs) or a .glb; the default scene's nodes
(translation, rotation, scale or a matrix); mesh primitives of triangles (strips and fans become triangles); the
material's baseColorFactor; animation channels of translation and rotation (linear, step, cubic). glTF stands y up in
metres, as the editor does. Each primitive is written as an OBJ beside the file (folder <name>-meshes; the temporary
folder when that one cannot be written) - the editor draws a mesh from a file and fits it to its size. A scale is baked
into the mesh and into the places under it. What it does not carry, said in the answer's note: textures laid by UV
(the editor lays textures by the world, so many per metre), skins and morph targets (Kinetica's bodies are rigid), the
animation of scale, points and lines. The shapes come as geometry: their roles - a rigid body, a collider - are given
in the editor after.
"""
import base64
import json
import math
import os
import struct
import tempfile


ENGLISH = os.environ.get("KE_UI_LANGUAGE") == "en"
TYPES = {5120: "b", 5121: "B", 5122: "h", 5123: "H", 5125: "I", 5126: "f"}
WIDTH = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}
NORM = {5120: 127.0, 5121: 255.0, 5122: 32767.0, 5123: 65535.0}


def read(path):
    """The glTF's JSON and its buffers' bytes."""
    with open(path, "rb") as f:
        data = f.read()
    base, binary = os.path.dirname(os.path.abspath(path)), None
    if data[:4] == b"glTF":  # .glb: a header, then chunks - JSON, then BIN
        doc, at = None, 12
        while at < len(data):
            length, kind = struct.unpack_from("<II", data, at)
            chunk = data[at + 8: at + 8 + length]
            if kind == 0x4E4F534A:
                doc = json.loads(chunk.decode("utf-8"))
            elif kind == 0x004E4942:
                binary = chunk
            at += 8 + length
    else:
        doc = json.loads(data.decode("utf-8"))
    buffers = []
    for k, b in enumerate(doc.get("buffers", [])):
        uri = b.get("uri")
        if uri is None:
            buffers.append(binary or b"")
        elif uri.startswith("data:"):
            buffers.append(base64.b64decode(uri.split(",", 1)[1]))
        else:
            with open(os.path.join(base, uri.replace("%20", " ")), "rb") as f:
                buffers.append(f.read())
    return doc, buffers


def accessor(doc, buffers, index):
    """An accessor's elements: tuples of floats (normalized integers as fractions)."""
    a = doc["accessors"][index]
    n, width, kind = a["count"], WIDTH[a["type"]], a["componentType"]
    if "bufferView" not in a:  # all zeros (sparse values on top are not read)
        return [(0.0,) * width for _ in range(n)]
    view = doc["bufferViews"][a["bufferView"]]
    if view["buffer"] >= len(buffers):
        raise ValueError(f"buffer {view['buffer']} is not in the file")
    data = buffers[view["buffer"]]
    start = view.get("byteOffset", 0) + a.get("byteOffset", 0)
    size = struct.calcsize("<" + TYPES[kind])
    stride = view.get("byteStride") or size * width
    fmt = "<" + TYPES[kind] * width
    scale = NORM.get(kind) if a.get("normalized") else None
    out = []
    for i in range(n):
        v = struct.unpack_from(fmt, data, start + i * stride)
        out.append(tuple(max(-1.0, x / scale) for x in v) if scale else tuple(float(x) for x in v))
    return out


def quat_matrix(q):
    x, y, z, w = q
    return ((1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)),
            (2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)),
            (2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)))


def rot(m, v):
    return tuple(sum(m[i][k] * v[k] for k in range(3)) for i in range(3))


def degrees(m):
    """The editor's rotation words of a turn: yaw about y, then pitch about x, then roll about z (entityRotation)."""
    pitch = math.asin(max(-1.0, min(1.0, -m[1][2])))
    if abs(m[1][2]) < 0.999999:
        yaw, roll = math.atan2(m[0][2], m[2][2]), math.atan2(m[1][0], m[1][1])
    else:  # looking straight up or down: the roll folded into the yaw
        yaw, roll = math.atan2(-m[2][0], m[0][0]), 0.0
    return tuple(math.degrees(a) for a in (pitch, yaw, roll))


def trs(node):
    """A node's translation, rotation (x, y, z, w) and scale - from its own words or its matrix (column-major)."""
    if "matrix" in node:
        m = node["matrix"]
        cols = [m[0:3], m[4:7], m[8:11]]
        s = [math.sqrt(sum(c * c for c in col)) or 1.0 for col in cols]
        r = [[cols[j][i] / s[j] for j in range(3)] for i in range(3)]  # rows of the rotation
        tr = r[0][0] + r[1][1] + r[2][2]
        if tr > 0:
            k = math.sqrt(tr + 1.0) * 2
            q = ((r[2][1] - r[1][2]) / k, (r[0][2] - r[2][0]) / k, (r[1][0] - r[0][1]) / k, k / 4)
        else:
            i = max(range(3), key=lambda d: r[d][d])
            j, l = (i + 1) % 3, (i + 2) % 3
            k = math.sqrt(1.0 + r[i][i] - r[j][j] - r[l][l]) * 2
            v = [0.0, 0.0, 0.0]
            v[i], v[j], v[l] = k / 4, (r[j][i] + r[i][j]) / k, (r[l][i] + r[i][l]) / k
            q = (v[0], v[1], v[2], (r[l][j] - r[j][l]) / k)
        return tuple(m[12:15]), q, tuple(s)
    return (tuple(node.get("translation", (0.0, 0.0, 0.0))), tuple(node.get("rotation", (0.0, 0.0, 0.0, 1.0))),
            tuple(node.get("scale", (1.0, 1.0, 1.0))))


def triangles(doc, buffers, primitive):
    """The primitive's points and its triangles (strips and fans unfolded); None - not triangles."""
    mode = primitive.get("mode", 4)
    if mode not in (4, 5, 6) or "POSITION" not in primitive.get("attributes", {}):
        return None
    points = accessor(doc, buffers, primitive["attributes"]["POSITION"])
    order = [int(i[0]) for i in accessor(doc, buffers, primitive["indices"])] if "indices" in primitive else list(range(len(points)))
    if mode == 4:
        tris = [tuple(order[i:i + 3]) for i in range(0, len(order) - 2, 3)]
    elif mode == 5:
        tris = [(order[i], order[i + 1], order[i + 2]) if i % 2 == 0 else (order[i + 1], order[i], order[i + 2]) for i in range(len(order) - 2)]
    else:
        tris = [(order[0], order[i], order[i + 1]) for i in range(1, len(order) - 1)]
    return points, tris


def word(x):
    return f"{x:.6g}" if abs(x) > 1e-9 else "0"


def words(v):
    return " ".join(word(x) for x in v)


def plural(n, one, few, many):
    tail = n % 100
    form = one if tail % 10 == 1 and tail != 11 else few if 2 <= tail % 10 <= 4 and not 12 <= tail <= 14 else many
    return f"{n} {form}"


def quote(name):
    return name.replace('"', "'")


def load(scene, path):
    """A scene from a glTF file: its nodes as groups and shapes in a group named after the file, its animations of place
    and turn as sequences."""
    ed = scene.ed
    doc, buffers = read(path)
    stem = os.path.splitext(os.path.basename(path))[0]
    folder = os.path.join(os.path.dirname(os.path.abspath(path)), stem + "-meshes")
    notes = set()
    try:
        os.makedirs(folder, exist_ok=True)
        probe = os.path.join(folder, ".write")
        open(probe, "w").close()
        os.remove(probe)
    except OSError:
        folder = tempfile.mkdtemp(prefix=f"{stem}-meshes-")
        notes.add(("meshes in the temporary folder " if ENGLISH else "сетки во временной папке ") + folder)

    colours = []
    for m in doc.get("materials", []):
        pbr = m.get("pbrMetallicRoughness", {})
        colours.append(tuple(pbr.get("baseColorFactor", (0.8, 0.8, 0.8, 1.0))[:3]))
        if "baseColorTexture" in pbr:
            notes.add("UV textures are not carried: the material's colour is" if ENGLISH else "текстуры по UV не переносятся - взят цвет материала")

    meshes = {}  # (mesh, primitive, scale) -> (file, size, centre, colour)
    def mesh_file(m, k, scale):
        key = (m, k, scale)
        if key not in meshes:
            primitive = doc["meshes"][m]["primitives"][k]
            try:
                got = triangles(doc, buffers, primitive)
            except (ValueError, KeyError, IndexError, struct.error):  # a mesh whose data the file does not hold
                notes.add("meshes without their data are left out" if ENGLISH else "сетки без данных в файле пропущены")
                meshes[key] = None
                return None
            if got is None:
                notes.add("points and lines are left out" if ENGLISH else "точки и линии не переносятся")
                meshes[key] = None
                return None
            if primitive.get("targets") or "JOINTS_0" in primitive.get("attributes", {}):
                notes.add("skins and morph targets are left out: shown as made" if ENGLISH else "скелеты и морфы не переносятся - показано как сделано")
            points, tris = got
            points = [tuple(p[i] * scale[i] for i in range(3)) for p in points]
            lo = [min(p[i] for p in points) for i in range(3)]
            hi = [max(p[i] for p in points) for i in range(3)]
            centre = tuple((lo[i] + hi[i]) / 2 for i in range(3))
            file = os.path.join(folder, f"mesh{m}_{k}" + (f"_{len(meshes)}" if scale != (1.0, 1.0, 1.0) else "") + ".obj")
            with open(file, "w", encoding="utf-8") as f:
                f.write("".join(f"v {words(p)}\n" for p in points))
                f.write("".join(f"f {a + 1} {b + 1} {c + 1}\n" for a, b, c in tris))
            colour = colours[primitive["material"]] if "material" in primitive and primitive["material"] < len(colours) else (0.8, 0.8, 0.8)
            meshes[key] = (file, max(hi[i] - lo[i] for i in range(3)) or 0.001, centre, colour)
        return meshes[key]

    def put_group(name, parent, position, turn):
        return ed.scene_put(block=f'group "{quote(name)}"\n  object id 0 visible 1 locked 0{f" parent {parent}" if parent else ""}\n'
                                  f"  pose position {words(position)} rotation {words(degrees(turn))} color 0.8 0.8 0.8\nend\n")["id"]

    def put_shape(name, parent, position, turn, mesh):
        file, size, _, colour = mesh
        return ed.scene_put(block=f'entity "{quote(name)}"\n  object id 0 visible 1 locked 0 parent {parent}\n'
                                  f'  shape mesh file "{file}" size {word(size)} {word(size)} {word(size)} position {words(position)} '
                                  f"rotation {words(degrees(turn))} color {words(colour)}\nend\n")["id"]

    top = ed.scene_put(block=f'group "{quote(stem)}"\n  object id 0 visible 1 locked 0\n'
                             "  pose position 0 0 0 rotation 0 0 0 color 0.8 0.8 0.8\nend\n")["id"]
    nodes = doc.get("nodes", [])
    placed = {}  # node -> (object id, the parent's scale, the offset of a lone shape's mesh centre)
    shapes = 0

    def visit(n, parent, scale_above):
        nonlocal shapes
        node = nodes[n]
        t, q, s = trs(node)
        turn = quat_matrix(q)
        here = tuple(t[i] * scale_above[i] for i in range(3))  # a parent's scale moves its children apart
        scale = tuple(scale_above[i] * s[i] for i in range(3))
        name = node.get("name") or f"node {n}"
        prims = doc["meshes"][node["mesh"]]["primitives"] if "mesh" in node else []
        children = node.get("children", [])
        lone = len(prims) == 1 and not children and mesh_file(node["mesh"], 0, scale) is not None
        if len(prims) == 1 and not children and not lone:
            return  # its one mesh could not be read: nothing to show
        if lone:  # one shape, nothing under it: the node is the shape, placed by its mesh's centre
            mesh = mesh_file(node["mesh"], 0, scale)
            ident = put_shape(name, parent, tuple(here[i] + rot(turn, mesh[2])[i] for i in range(3)), turn, mesh)
            placed[n] = (ident, scale_above, mesh[2])
            shapes += 1
            return
        if not prims and not children:
            return  # an empty, a camera, a light: nothing to show
        ident = put_group(name, parent, here, turn)
        placed[n] = (ident, scale_above, (0.0, 0.0, 0.0))
        for k in range(len(prims)):
            mesh = mesh_file(node["mesh"], k, scale)
            if mesh is not None:
                put_shape(f"{name} {k + 1}" if len(prims) > 1 else name, ident, mesh[2], quat_matrix((0, 0, 0, 1)), mesh)
                shapes += 1
        for c in children:
            visit(c, ident, scale)

    roots = doc["scenes"][doc.get("scene", 0)]["nodes"] if doc.get("scenes") else [i for i in range(len(nodes))
                                                                                       if not any(i in m.get("children", []) for m in nodes)]
    for n in roots:
        visit(n, top, (1.0, 1.0, 1.0))

    sequences = 0
    for k, animation in enumerate(doc.get("animations", [])):
        tracks = {}  # node -> {"translation": (times, values, interp), "rotation": ...}
        for channel in animation.get("channels", []):
            target, sampler = channel.get("target", {}), animation["samplers"][channel["sampler"]]
            n, kind = target.get("node"), target.get("path")
            if kind not in ("translation", "rotation") or n not in placed:
                if kind in ("scale", "weights"):
                    notes.add("the animation of scale and morphs is left out" if ENGLISH else "анимация масштаба и морфов не переносится")
                continue
            times = [v[0] for v in accessor(doc, buffers, sampler["input"])]
            values = accessor(doc, buffers, sampler["output"])
            interp = sampler.get("interpolation", "LINEAR")
            if interp == "CUBICSPLINE":  # in-tangent, value, out-tangent for each key: the values
                values = values[1::3]
            tracks.setdefault(n, {})[kind] = (times, values, interp)
        lines = []
        for n, kinds in tracks.items():
            ident, scale_above, centre = placed[n]
            t0, q0, _ = trs(nodes[n])
            times = sorted({t for times, _, _ in kinds.values() for t in times})
            interp = next((i for _, _, i in kinds.values() if i != "LINEAR"), "LINEAR")

            def at(kind, time, rest):
                if kind not in kinds:
                    return rest
                ts, vs, _ = kinds[kind]
                if time <= ts[0]:
                    return vs[0]
                for j in range(1, len(ts)):
                    if time <= ts[j]:
                        f = (time - ts[j - 1]) / ((ts[j] - ts[j - 1]) or 1.0)
                        return tuple(a + (b - a) * f for a, b in zip(vs[j - 1], vs[j]))
                return vs[-1]

            lines.append(f"  track object {ident}")
            for time in times:
                t = at("translation", time, t0)
                q = at("rotation", time, q0)
                norm = math.sqrt(sum(x * x for x in q)) or 1.0
                turn = quat_matrix(tuple(x / norm for x in q))
                where = tuple(t[i] * scale_above[i] + rot(turn, centre)[i] for i in range(3))
                tail = " interp step" if interp == "STEP" else " interp smooth" if interp == "CUBICSPLINE" else ""
                lines.append(f"  key time {word(time)} position {words(where)} rotation {words(degrees(turn))}{tail}")
        if lines:
            name = animation.get("name") or f"{stem} {k + 1}"
            ed.scene_put(block=f'sequence "{quote(name)}"\n  object id 0 visible 1 locked 0\n'
                               "  pose position 0 0 0 rotation 0 0 0 color 0.8 0.8 0.8\n"
                               f"  clip time 0 loop 0 active {int(sequences == 0)} play {int(sequences == 0)}\n" + "\n".join(lines) + "\nend\n")
            sequences += 1

    try:
        ed.selection_set(ids=[top])  # the window selects what came; a windowless scene has no selection
    except Exception:
        pass
    if not shapes:
        notes.add("the file holds no mesh to show" if ENGLISH else "в файле нет сетки, которую можно показать")
    if ENGLISH:
        said = f"«{stem}»: {shapes} {'shape' if shapes == 1 else 'shapes'}, {sequences} {'sequence' if sequences == 1 else 'sequences'}"
    else:
        said = f"«{stem}»: {plural(shapes, 'фигура', 'фигуры', 'фигур')}, {plural(sequences, 'секвенция', 'секвенции', 'секвенций')}"
    return {"group": top, "shapes": shapes, "sequences": sequences, "note": said + ("; " + "; ".join(sorted(notes)) if notes else "")}
