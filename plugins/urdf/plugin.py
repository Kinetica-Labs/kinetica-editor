"""The old name of the urdf import, kept alive (names only grow: docs/contracts/names.md): it moved into io_formats
(plugins/io_formats/urdf.py). An obsolete command - an operation, no button -, it forwards there."""
from kinetica.plugin import command


@command(obsolete="io_formats_urdf_load")
def load(scene, path: str):
    """Obsolete: io_formats_urdf_load."""
    return scene.ed.io_formats_urdf_load(path=path)
