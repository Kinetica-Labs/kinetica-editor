"""The Kinetica package (plugins/io_formats/kpkg.py) without a window: a scene with a model packed into one zip - the model
inside, the scene's path rewritten to it, a missing file said -, unpacked and opened from beside the package; a member that
would land outside the package's folder (a ..) refused. A stand-in scene answers scene_get and records scene_open."""
import importlib.util
import os
import sys
import tempfile
import unittest
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def module():
    spec = importlib.util.spec_from_file_location("kpkg", os.path.join(ROOT, "plugins", "io_formats", "kpkg.py"))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


class Ed:
    def __init__(self, text, folder):
        self.text, self.folder, self.opened = text, folder, None

    def scene_get(self):
        return {"text": self.text, "folder": self.folder}

    def scene_open(self, file):
        self.opened = file
        return {}


class Scene:
    def __init__(self, ed):
        self.ed = ed


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        self.kpkg = module()

    def test_out_and_in(self):
        os.makedirs(os.path.join(self.dir.name, "models"))
        with open(os.path.join(self.dir.name, "models", "arm.obj"), "w") as f:
            f.write("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n")
        text = 'entity "A"\n  shape mesh file "models/arm.obj" size 1 1 1\n  texture kind stone file ""\nend\nentity "B"\n  shape mesh file "gone.obj"\nend\n'
        package = os.path.join(self.dir.name, "out", "scene.kpkg")
        os.makedirs(os.path.dirname(package))
        said = self.kpkg.save(Scene(Ed(text, self.dir.name)), package)
        with zipfile.ZipFile(package) as z:
            inside = z.read("scene.kscene").decode()
            self.assertIn("files/arm.obj", z.namelist())
        self.assertIn('file "files/arm.obj"', inside)
        self.assertIn('file ""', inside)  # an empty path stays empty
        self.assertEqual((said["files"], said["missing"]), (1, ["gone.obj"]))
        ed = Ed("", "")
        got = self.kpkg.load(Scene(ed), package)
        self.assertEqual(ed.opened, os.path.join(self.dir.name, "out", "scene-files", "scene.kscene"))
        self.assertTrue(os.path.isfile(os.path.join(got["folder"], "files", "arm.obj")))

    def test_model_with_materials(self):
        """An OBJ with a material library and a picture goes in a folder of its own with them, under the names it calls
        them by; a picture it names and lacks is said missing."""
        home = os.path.join(self.dir.name, "crate")
        os.makedirs(os.path.join(home, "pictures"))
        with open(os.path.join(home, "crate.obj"), "w") as f:
            f.write("mtllib crate.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0 1\nusemtl wood\nf 1/1 2/2 3/3\n")
        with open(os.path.join(home, "crate.mtl"), "w") as f:
            f.write("newmtl wood\nKd 0.6 0.4 0.2\nmap_Kd -s 1 1 1 pictures\\wood.png\nnewmtl lid\nmap_Kd lid.png\n")
        with open(os.path.join(home, "pictures", "wood.png"), "wb") as f:
            f.write(b"\x89PNG")
        text = 'entity "Crate"\n  shape mesh file "crate/crate.obj"\nend\n'
        package = os.path.join(self.dir.name, "crate.kpkg")
        said = self.kpkg.save(Scene(Ed(text, self.dir.name)), package)
        with zipfile.ZipFile(package) as z:
            names = set(z.namelist())
            inside = z.read("scene.kscene").decode()
        self.assertTrue({"files/crate/crate.obj", "files/crate/crate.mtl", "files/crate/pictures/wood.png"} <= names, names)
        self.assertIn('file "files/crate/crate.obj"', inside)
        self.assertEqual((said["files"], said["missing"]), (3, ["lid.png"]))

    def test_member_outside_is_refused(self):
        package = os.path.join(self.dir.name, "evil.kpkg")
        with zipfile.ZipFile(package, "w") as z:
            z.writestr("scene.kscene", "world gravity 0 -9.81 0\n")
            z.writestr("../outside.txt", "no")
        got = self.kpkg.load(Scene(Ed("", "")), package)
        self.assertEqual(got["refused"], ["../outside.txt"])
        self.assertFalse(os.path.exists(os.path.join(self.dir.name, "outside.txt")))

    def test_no_scene_no_package(self):
        package = os.path.join(self.dir.name, "empty.kpkg")
        with zipfile.ZipFile(package, "w") as z:
            z.writestr("x.txt", "")
        with self.assertRaises(ValueError):
            self.kpkg.load(Scene(Ed("", "")), package)


if __name__ == "__main__":
    unittest.main()
