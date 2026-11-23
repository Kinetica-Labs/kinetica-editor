"""The old name of the gltf import, kept alive (names only grow: docs/contracts/names.md): it moved into io_formats
(plugins/io_formats/gltf.py). An obsolete command - an operation, no button -, it forwards there."""
from kinetica.plugin import command


@command(obsolete="io_formats_gltf_load")
def load(scene, path: str):
    """Obsolete: io_formats_gltf_load."""
    return scene.ed.io_formats_gltf_load(path=path)
