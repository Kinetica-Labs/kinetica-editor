"""io_formats - the scene in and out of the formats the world uses, one plugin for all of them («Файл → Импорт…», a file
dropped on the view, an agent's `ed.io_formats_<format>_load(path=...)`). A file a format (urdf.py, gltf.py) - the code
they share has one place to live as it grows. It comes with the editor; the old `urdf_load` and `gltf_load` stay as
obsolete names forwarding here (plugins/urdf, plugins/gltf: names only grow)."""
import os

from kinetica.plugin import command

import gltf
import urdf

ENGLISH = os.environ.get("KE_UI_LANGUAGE") == "en"


@command(label="URDF Robot" if ENGLISH else "URDF — робот", opens=[".urdf"])
def urdf_load(scene, path):
    """A robot from its URDF: links as shapes, joints as motors holding the pose they are made in, their limits."""
    return urdf.load(scene, path)


@command(label="glTF Scene" if ENGLISH else "glTF — сцена", opens=[".gltf", ".glb"])
def gltf_load(scene, path):
    """A scene from glTF (.gltf, .glb): its node tree as groups and shapes, colours, animations of place and turn."""
    return gltf.load(scene, path)
