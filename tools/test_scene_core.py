"""``ke.Scene()`` against the real program build/kinetica-core, with no window: a falling box's height, the errors a
script meets where they happen, a world restarted by an edit, relative paths, the child's life and death, and that
nothing but answers ever goes to the program's stdout."""
import gc
import json
import os
import shutil
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

    def test_an_interrupted_call_does_not_break_the_session(self):
        core = Core()
        core.call("scene_list")
        real = core._proc.stdout

        class Break:
            def readline(self_):
                core._proc.stdout = real
                raise KeyboardInterrupt

        core._proc.stdout = Break()
        with self.assertRaises(KeyboardInterrupt):
            core.call("scene_list")
        self.assertTrue(core.call("scene_list")["ok"])
        core.close()

    def test_a_refused_line_names_its_cause(self):
        with ke.Scene() as scene:
            with self.assertRaises(OperationError) as caught:
                scene.add("box", position=(float("nan"), 0, 0))
            self.assertIn("JSON", str(caught.exception))
            self.assertTrue(scene.ed.scene_list()["ok"])

    def test_close_after_the_child_died_is_quiet(self):
        core = Core()
        core._proc.kill()
        core._proc.wait()
        core.close()
        core.close()
        self.assertIsNotNone(core._proc.returncode)
        self.assertFalse(core._drainer.is_alive())

    def test_data_is_found_beside_the_real_program(self):
        exe = find_core()
        with tempfile.TemporaryDirectory() as tmp:
            bundle = os.path.join(tmp, "usr")
            os.makedirs(os.path.join(bundle, "bin"))
            os.symlink(os.path.join(ROOT, "data"), os.path.join(bundle, "data"))
            real = os.path.join(bundle, "bin", "kinetica-core")
            shutil.copy(exe, real)
            os.makedirs(os.path.join(tmp, "links"))
            link = os.path.join(tmp, "links", "kc")
            os.symlink(real, link)
            out = subprocess.run([link], input='{"op": "sensor_models"}\n', capture_output=True, text=True, timeout=30)
            self.assertEqual(out.returncode, 0, out.stderr)
            # The program names the folder by its real path (Windows: the temporary folder's long name, not RUNNER~1).
            files = json.loads(out.stdout.splitlines()[0])["result"]["files"]
            want = os.path.normcase(os.path.join(os.path.realpath(bundle), "data", "sensors"))
            self.assertTrue(files)
            self.assertEqual({os.path.normcase(os.path.dirname(os.path.normpath(f))) for f in files}, {want})

    def test_a_bad_data_dir_is_an_error(self):
        out = subprocess.run([find_core(), "--data-dir", "/nonexistent"], input="", capture_output=True, text=True, timeout=30)
        self.assertEqual(out.returncode, 2)
        self.assertIn("kinetica-core: нет каталога sensors", out.stderr)

    def test_one_step_call_is_cut_at_6000_frames(self):
        with ke.Scene() as scene:
            falling(scene)
            self.assertEqual(scene.step(7000)["completed_frames"], 6000)

    def test_a_pendulum_swings_through_the_pipe(self):
        with ke.Scene() as scene:
            bob = scene.add("sphere", position=(1, 2, 0), role="rigid")
            scene.ed.assembly_joint(a=bob.id, b=0, type="hinge", anchor=[0, 2, 0], axis=[0, 0, 1])
            scene.run(0.5)
            self.assertLess(height(scene, bob), 1.95)

    def test_a_scene_left_open_ends_with_its_script(self):
        code = textwrap.dedent(f"""
            import sys
            sys.path.insert(0, {os.path.join(ROOT, "python")!r})
            import kinetica as ke
            scene = ke.Scene()
            scene.add("box")
        """)
        out = subprocess.run([sys.executable, "-c", code], capture_output=True, text=True, timeout=60)
        self.assertEqual((out.returncode, out.stderr), (0, ""))

    def test_a_dropped_scene_ends_its_child(self):
        scene = ke.Scene()
        proc = scene._client._proc
        scene.step()
        del scene
        gc.collect()
        self.assertEqual(proc.wait(5), 0)

    def test_the_four_forces_and_their_properties(self):
        with ke.Scene() as scene:
            kinds = [scene.add_force(shape, mode) for shape, mode in (("directional", "impulse"), ("radial", "impulse"),
                                                                       ("directional", "constant"), ("radial", "constant"))]
            self.assertEqual([(f.kind, f.shape, f.mode) for f in kinds], [("force", s, m) for s, m in (
                ("directional", "impulse"), ("radial", "impulse"), ("directional", "constant"), ("radial", "constant"))])
            self.assertEqual([f.strength for f in kinds], [3, 6, 12, -15])  # fields: m/s and m/s^2
            push = kinds[0]
            push.strength, push.falloff, push.start, push.acts_on = 7, "inverse_square", 2.5, "crate"
            self.assertEqual((push.strength, push.falloff, push.start, push.acts_on), (7, "inverse_square", 2.5, "crate"))
            push.tag("blast")
            self.assertEqual((scene.objects.tagged("blast"), scene.objects.tagged("crate")), ([push], []))
            push.acts_on = None
            self.assertEqual(push.acts_on, "")
            aimed = scene.add_force("radial", "constant", at=(0, 1, 0), strength=3, radius=2)
            self.assertEqual((aimed.strength, aimed.radius), (3, 2))

    def test_a_radial_force_needs_its_radius_first(self):
        with ke.Scene() as scene:
            with self.assertRaises(OperationError) as caught:
                scene.add_force("radial", "impulse", radius=0)
            self.assertIn("radius", str(caught.exception))
            push = scene.add_force()
            push.radius = 0  # every body, wherever it stands: a reach a radial force cannot have
            with self.assertRaises(OperationError):
                push.shape = "radial"
            push.radius = 3
            push.shape = "radial"
            self.assertEqual(push.shape, "radial")

    def test_a_typo_is_answered_with_what_it_meant(self):
        with ke.Scene() as scene:
            box = scene.add("box")
            for op, args, said in [("scene_ad", {"shape": "box"}, "может, «scene_add»?"),
                                   ("scene_add", {"shap": "box"}, "нет поля «shap» (может, «shape»?); поля: "),
                                   ("scene_add", {"shape": "boxx"}, "«boxx» не из списка (может, «box»?): box, sphere"),
                                   ("scene_set", {"id": box.id, "line": "rigid", "key": "mas", "values": [1]},
                                    "cannot read 'mas' - did you mean 'mass'? the keys here: density, friction")]:
                with self.assertRaises(ke.OperationError) as caught:
                    getattr(scene.ed, op)(**args)
                self.assertIn(said, str(caught.exception))
            with self.assertRaises(ke.OperationError) as caught:  # nothing near: the line's keys, no guess
                scene.ed.scene_set(id=box.id, line="rigid", key="zzzz", values=[1])
            self.assertNotIn("did you mean", str(caught.exception))

    def test_the_format_tells_its_own_kinds_lines_and_keys(self):
        with ke.Scene() as scene:
            kinds = {k["kind"]: k["parts"] for k in scene.ed.schema_list()["kinds"]}
            self.assertEqual(set(kinds), {"world", "physics", "entity", "group", "array", "light", "force", "camera", "joint", "battery",
                                          "driver", "sensor", "wheel", "controller", "sequence"})
            self.assertIn("rigid", kinds["entity"])
            joint = {p["word"]: p for p in scene.ed.schema_get(kind="joint")["parts"]}
            drive = {k["word"]: k for k in joint["drive"]["keys"]}
            self.assertEqual((drive["target"]["form"], drive["target"]["unit"]), ("number", "rad or m"))
            self.assertIn("tx ty tz rx ry rz", joint["drive"]["lead"])
            rigid = {k["word"]: k.get("unit", "") for p in scene.ed.schema_get(kind="entity")["parts"] if p["word"] == "rigid" for k in p["keys"]}
            self.assertEqual((rigid["mass"], rigid["angular_velocity"]), ("kg", "rad/s"))
            box = scene.add("box")  # what the schema says the reader reads, scene_set writes and scene_get reads back
            scene.ed.scene_set(id=box.id, line="rigid", key="mass", values=[2.5])
            self.assertEqual(scene.ed.scene_get(id=box.id, line="rigid", key="mass")["values"], [2.5])
            with self.assertRaisesRegex(ke.OperationError, "no kind 'robot'; the kinds: world physics entity"):
                scene.ed.schema_get(kind="robot")

    def test_the_scene_links_are_one_list_both_ways(self):
        with ke.Scene() as scene:
            low, high = scene.add("box", position=(0, 0.5, 0)), scene.add("box", position=(0, 1.5, 0))
            joint = scene.ed.assembly_joint(a=high.id, b=low.id, type="hinge", anchor=[0, 1, 0], axis=[0, 0, 1])["id"]
            group = scene.group(low, high)
            links = {(r["kind"], r["from"], r["to"]) for r in scene.ed.relation_list()["relations"]}
            self.assertEqual(links, {("object.parent", low.id, group.id), ("object.parent", high.id, group.id),
                                     ("link.a", joint, high.id), ("link.b", joint, low.id)})
            self.assertEqual(scene.ed.relation_list(to=low.id)["relations"], [{"kind": "link.b", "from": joint, "to": low.id}])
            with self.assertRaisesRegex(ke.OperationError, "no relation kind 'parent'.*object.parent"):
                scene.ed.relation_list(kind="parent")

    def test_the_play_range_is_a_word_of_the_world_line(self):
        # scene.world.duration: where a window's play and take end; this host has no clock, so it only keeps the number
        with ke.Scene() as scene:
            self.assertEqual(scene.world.duration, 0)
            scene.world.duration = 2.5
            self.assertEqual(scene.world.duration, 2.5)
            self.assertIn(" duration 2.5", scene.ed.world_get()["text"])
            scene.world.duration = 0
            self.assertNotIn("duration", scene.ed.world_get()["text"])

    def test_the_take_starts_before_the_play_range_ends(self):
        # scene.world.record_from: where the window's take starts; never at or after the end, the refusal names both
        with ke.Scene() as scene:
            scene.world.duration = 5
            scene.world.record_from = 2
            self.assertEqual(scene.world.record_from, 2)
            self.assertIn(" record_from 2", scene.ed.world_get()["text"])
            with self.assertRaisesRegex(ke.OperationError, "record_from 6 is not before duration 5"):
                scene.world.record_from = 6
            self.assertEqual(scene.world.record_from, 2)

    def test_a_force_of_its_own_body(self):
        # body=: the force is that body's own - a metre of arrow is 3 m/s of it; a neighbour half a metre away feels nothing
        with ke.Scene() as scene:
            scene.world.gravity = (0, 0, 0)
            box = scene.add("box", position=(0, 2, 0), role="rigid")
            near = scene.add("box", position=(0.5, 2, 0), role="rigid")
            scene.add_force(body=box)
            scene.step(2)
            moving = scene.measure(objects=[box, near])
            self.assertAlmostEqual(moving[f"{box.id}/velocity y"], 3, delta=1e-2)
            self.assertAlmostEqual(moving[f"{near.id}/velocity y"], 0, delta=1e-6)
            with self.assertRaises(OperationError) as caught:
                scene.add_force("radial", "impulse", body=box)
            self.assertIn("directional", str(caught.exception))

    def test_fire_sends_its_id_alone(self):
        # force_fire refuses every key but id, and a window's client adds the console line as one more key
        with ke.Scene() as scene:
            push = scene.add_force()
            sent, real = [], scene._client.call
            scene._client.call = lambda op, py=None, **args: sent.append((op, py, args)) or real(op, py, **args)
            scene.step(1)
            push.fire()
        self.assertIn(("force_fire", None, {"id": push.id}), sent)

    def test_a_fired_force_gives_a_box_its_velocity(self):
        with ke.Scene() as scene:
            box = scene.add("box", position=(0, 3, 0), role="rigid")
            scene.world.gravity = (0, 0, 0)
            push = scene.add_force()
            push.start = 100
            with self.assertRaises(OperationError) as caught:
                push.fire()
            self.assertIn("not running", str(caught.exception))
            scene.step(1)
            self.assertAlmostEqual(scene.measure(objects=[box])[f"{box.id}/velocity y"], 0, delta=1e-4)
            push.fire()
            scene.step(2)
            channels = scene.measure(objects=[box])
            self.assertAlmostEqual(channels[f"{box.id}/velocity y"], 3, delta=1e-3)  # a field push: 3 m/s to every body, whatever it weighs


if __name__ == "__main__":
    unittest.main()
