"""The glTF import that comes with the editor (plugins/gltf): a small .glb made here - a red cube under a node that rises -
is read into a scene with no window (ke.Scene()), and what came is held to what the file says: the node tree as groups and
a shape, the place, the turn and the baked scale, the colour, the mesh written beside the file, the animation as a
sequence. Needs build/kinetica-core.

    cd tools && python3 -m unittest test_gltf
"""
import importlib.util
import json
import math
import os
import re
import struct
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "clients", "python"))
import kinetica as ke  # noqa: E402
from kinetica.core import find_core  # noqa: E402


def built():
    try:
        return bool(find_core())
    except FileNotFoundError:
        return False


def plugin():
    spec = importlib.util.spec_from_file_location("gltf_plugin", os.path.join(ROOT, "plugins", "gltf", "plugin.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def cube_glb(path):
    """A unit cube (8 corners, 12 triangles) as node «Cube» - 1 m along x, turned 90 degrees about y, scale 2, red - under
    node «Root» at 1 m up, which an animation «Lift» raises to 2 m in a second."""
    corners = [(x, y, z) for x in (-0.5, 0.5) for y in (-0.5, 0.5) for z in (-0.5, 0.5)]
    order = [0, 1, 3, 0, 3, 2, 4, 6, 7, 4, 7, 5, 0, 4, 5, 0, 5, 1, 2, 3, 7, 2, 7, 6, 0, 2, 6, 0, 6, 4, 1, 5, 7, 1, 7, 3]
    blob = b"".join(struct.pack("<3f", *c) for c in corners)                           # 0: 96 bytes
    blob += struct.pack(f"<{len(order)}H", *order)                                       # 96: 72 bytes
    blob += struct.pack("<2f", 0.0, 1.0)                                                 # 168: 8 bytes
    blob += struct.pack("<6f", 0.0, 1.0, 0.0, 0.0, 2.0, 0.0)                            # 176: 24 bytes
    half = math.sqrt(0.5)
    doc = {
        "asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "Root", "translation": [0, 1, 0], "children": [1]},
                  {"name": "Cube", "mesh": 0, "translation": [1, 0, 0], "rotation": [0, half, 0, half], "scale": [2, 2, 2]}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 1, "material": 0}]}],
        "materials": [{"pbrMetallicRoughness": {"baseColorFactor": [1, 0, 0, 1]}}],
        "animations": [{"name": "Lift", "channels": [{"sampler": 0, "target": {"node": 0, "path": "translation"}}],
                        "samplers": [{"input": 2, "output": 3}]}],
        "buffers": [{"byteLength": len(blob)}],
        "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 96}, {"buffer": 0, "byteOffset": 96, "byteLength": 72},
                        {"buffer": 0, "byteOffset": 168, "byteLength": 8}, {"buffer": 0, "byteOffset": 176, "byteLength": 24}],
        "accessors": [{"bufferView": 0, "componentType": 5126, "count": 8, "type": "VEC3", "min": [-0.5] * 3, "max": [0.5] * 3},
                      {"bufferView": 1, "componentType": 5123, "count": 36, "type": "SCALAR"},
                      {"bufferView": 2, "componentType": 5126, "count": 2, "type": "SCALAR"},
                      {"bufferView": 3, "componentType": 5126, "count": 2, "type": "VEC3"}],
    }
    text = json.dumps(doc).encode("utf-8")
    text += b" " * (-len(text) % 4)
    blob += b"\0" * (-len(blob) % 4)
    with open(path, "wb") as f:
        f.write(struct.pack("<4sII", b"glTF", 2, 12 + 8 + len(text) + 8 + len(blob)))
        f.write(struct.pack("<II", len(text), 0x4E4F534A) + text)
        f.write(struct.pack("<II", len(blob), 0x004E4942) + blob)


def block(text, kind, name):
    """The lines of one block of the scene's text."""
    found = re.search(rf'^{kind} "{re.escape(name)}"\n(.*?)^end$', text, re.S | re.M)
    return found.group(1) if found else ""


def numbers(line, key, n=3):
    words = line.split()
    at = words.index(key)
    return [float(w) for w in words[at + 1: at + 1 + n]]


@unittest.skipUnless(built(), "build/kinetica-core is not built; run: cmake --build build --target kinetica-core")
class Gltf(unittest.TestCase):
    def test_a_cube_under_a_rising_node(self):
        with tempfile.TemporaryDirectory() as folder, ke.Scene() as scene:
            path = os.path.join(folder, "box.glb")
            cube_glb(path)
            answer = plugin().load(scene, path)
            text = scene.ed.scene_get()["text"]
            ids = {o["name"]: o["id"] for o in scene.ed.scene_list()["objects"]}
            root, cube = block(text, "group", "Root"), block(text, "entity", "Cube")
            shape = next(line for line in cube.splitlines() if line.strip().startswith("shape"))
            file = re.search(r'file "([^"]+)"', shape).group(1)
            with open(file, encoding="utf-8") as f:
                mesh = f.read().split("\n")
            lift = block(text, "sequence", "Lift")
            keys = [line for line in lift.splitlines() if line.strip().startswith("key")]

            self.assertEqual((answer["shapes"], answer["sequences"]), (1, 1), answer["note"])
            self.assertIn("box", ids)                                                    # the file's own group
            self.assertEqual(numbers(next(l for l in root.splitlines() if "pose" in l), "position"), [0, 1, 0])
            self.assertIn(f"parent {ids['Root']}", cube)                                   # the tree kept
            self.assertEqual(numbers(shape, "position"), [1, 0, 0])
            self.assertEqual([round(x) for x in numbers(shape, "rotation")], [0, 90, 0])  # a quarter turn about y
            self.assertEqual(numbers(shape, "size"), [2, 2, 2])                           # the scale baked into the mesh
            self.assertEqual(numbers(shape, "color"), [1, 0, 0])
            self.assertEqual(os.path.dirname(file), os.path.join(folder, "box-meshes"))  # written beside the file
            self.assertEqual((sum(l.startswith("v ") for l in mesh), sum(l.startswith("f ") for l in mesh)), (8, 12))
            self.assertIn(f"track object {ids['Root']}", lift)                            # the node that rises, not the cube
            self.assertEqual([numbers(k, "position") for k in keys], [[0, 1, 0], [0, 2, 0]])
            self.assertEqual([numbers(k, "time", 1)[0] for k in keys], [0, 1])


if __name__ == "__main__":
    unittest.main()
