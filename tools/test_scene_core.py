"""``ke.Scene()`` against the real program build/kinetica-core, with no window: the hello's height, the errors a
script meets where they happen, a world restarted by an edit, relative paths, the child's life and death, and that
nothing but answers ever goes to the program's stdout."""
import json
import os
import subprocess
import sys
import tempfile
import textwrap
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "python"))
import kinetica as ke  # noqa: E402
from kinetica.client import OperationError  # noqa: E402
from kinetica.core import Core, find_core  # noqa: E402


def built():
    try:
        return bool(find_core())
    except FileNotFoundError:
        return False


def height(scene, body):
    return scene.measure(objects=[body])[f"{body.id}/height"]


def falling(scene):
    box = scene.add("box", position=(0, 1, 0), role="rigid")
    scene.run(1.0)
    return box


@unittest.skipUnless(built(), "build/kinetica-core is not built; run: cmake --build build --target kinetica-core")
class SceneCore(unittest.TestCase):
    def test_hello(self):
        with ke.Scene() as scene:
            self.assertAlmostEqual(height(scene, falling(scene)), 0.12, delta=0.02)

    def test_errors_name_the_cause(self):
        with ke.Scene() as scene:
            for call, words in ((lambda: scene.editor.selection, "kinetica-core"),
                                (lambda: scene.open("nope.kscene"), "нет файла"),
                                (lambda: scene.ed.scene_list(bogus=1), "нет поля"),
                                (lambda: scene.ed.sim_play(), "нет операции")):
                with self.assertRaises(OperationError) as caught:
                    call()
                self.assertIn(words, str(caught.exception))
            self.assertEqual(scene.ed.scene_list()["objects"][0]["id"], 1)

    def test_an_edit_restarts_the_world(self):
        with ke.Scene() as scene:
            falling(scene)
            answer = scene.ed.scene_add(shape="box")
            self.assertTrue(answer["play_stopped"])
            self.assertEqual(scene.ed.sim_state()["mode"], "edit")

    def test_relative_path_is_the_callers(self):
        here = os.getcwd()
        with tempfile.TemporaryDirectory() as folder:
            try:
                os.chdir(folder)
                with ke.Scene() as scene:
                    scene.save("mine.kscene")
                self.assertTrue(os.path.isfile(os.path.join(folder, "mine.kscene")))
            finally:
                os.chdir(here)

    def test_two_scenes_agree(self):
        with ke.Scene() as one, ke.Scene() as two:
            self.assertEqual(one.measure(objects=[falling(one)]), two.measure(objects=[falling(two)]))

    def test_close_ends_the_child(self):
        scene = ke.Scene()
        proc = scene._client._proc
        scene.close()
        self.assertIsNotNone(proc.poll())
        scene.close()
        with self.assertRaises(ConnectionError):
            scene.ed.scene_list()

    @unittest.skipIf(os.name == "nt", "the parent is killed by a signal")
    def test_parent_death_ends_the_child(self):
        code = textwrap.dedent(f"""
            import os, sys
            sys.path.insert(0, {os.path.join(ROOT, "python")!r})
            import kinetica as ke
            scene = ke.Scene()
            print(scene._client._proc.pid, flush=True)
            os.kill(os.getpid(), 9)
        """)
        out = subprocess.run([sys.executable, "-c", code], capture_output=True, text=True)
        pid = int(out.stdout)
        for _ in range(100):
            try:
                os.kill(pid, 0)
            except ProcessLookupError:
                return
            time.sleep(0.05)
        self.fail("the child outlived its parent")

    def test_missing_program(self):
        old = os.environ.get("KE_CORE")
        os.environ["KE_CORE"] = "/nonexistent"
        try:
            with self.assertRaises(FileNotFoundError) as caught:
                ke.Scene()
            self.assertIn("/nonexistent", str(caught.exception))
        finally:
            os.environ.pop("KE_CORE")
            if old is not None:
                os.environ["KE_CORE"] = old

    def test_a_path_is_not_a_connection(self):
        with self.assertRaises(TypeError):
            ke.Scene("x")

    def test_stdout_is_only_the_wire(self):
        core = Core()
        lines = []
        real = core._proc.stdout
        readline = real.readline

        class Tap:
            def readline(self_):
                line = readline()
                lines.append(line)
                return line

            def close(self_):
                real.close()

        core._proc.stdout = Tap()
        scene = ke.Scene(core)
        falling(scene)
        scene.describe()
        scene.close()
        self.assertGreater(len(lines), 3)
        for line in lines:
            self.assertIn("result", json.loads(line))

    def test_a_bad_line_is_answered_and_the_session_goes_on(self):
        core = Core()
        core._proc.stdin.write(b"not json\n")
        core._proc.stdin.flush()
        self.assertIn("не JSON", json.loads(core._proc.stdout.readline())["result"]["error"])
        self.assertTrue(core.call("scene_list")["ok"])
        core.close()

    def test_the_hello_runs(self):
        out = subprocess.run([sys.executable, os.path.join(ROOT, "examples", "python", "01_hello.py")], cwd=ROOT,
                             capture_output=True, text=True, timeout=60)
        self.assertEqual(out.returncode, 0, out.stderr)
        self.assertIn("m high", out.stdout)


if __name__ == "__main__":
    unittest.main()
