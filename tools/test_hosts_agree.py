"""The window and the windowless host answer alike: a fresh hidden window and a fresh `kinetica-core` get the same requests -
the committed transcript (tests/data/window-transcript.jsonl, read as its first line says), then what it does not hold (the
UR5e run, a scene of every kind of edit with undo and redo, a scene saved by one host and opened by the other) - and every
answer is compared as parsed JSON after the recorder's masks. Then each difference docs/contracts/operations.md names
(«Хозяева операций») is asserted the other way round. The window is the recorder's own `Window`; the host is a pair of
pipes. Run from anywhere; a missing build or display skips with the reason, it never passes silently.

    python3 tools/test_hosts_agree.py        (under a machine with no display: xvfb-run -a python3 tools/test_hosts_agree.py)
"""
import json
import os
import queue
import shutil
import subprocess
import sys
import tempfile
import threading
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "clients", "python"))
import record_window_transcript as rec  # noqa: E402
from kinetica.core import find_core  # noqa: E402

UR5E = os.path.join(ROOT, "examples", "scenes", "ur5e.kscene")


def why_not():
    """The one line that says what is missing, or None."""
    if not os.path.isfile(os.path.join(ROOT, "build", "kinetica")):
        return "the window is not built: cmake --build build --target kinetica"
    if os.name == "posix" and not (os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY") or os.environ.get("QT_QPA_PLATFORM")):
        return "no display: run under xvfb-run -a"
    try:
        find_core()
    except FileNotFoundError:
        return "kinetica-core is not built: cmake --build build --target kinetica-core"
    return None


class CoreWire:
    """The pipes of kinetica-core with the Wire's two methods, so that the recorder's `play` drives either host."""

    def __init__(self, proc):
        self.proc, self.lines = proc, queue.Queue()
        threading.Thread(target=self.pump, daemon=True).start()

    def pump(self):
        for line in self.proc.stdout:
            self.lines.put(line.decode("utf-8").rstrip("\n"))
        self.lines.put(None)

    def send(self, data):
        self.proc.stdin.write(data + b"\n")
        self.proc.stdin.flush()

    def read_line(self, timeout):
        try:
            line = self.lines.get(timeout=timeout)
        except queue.Empty:
            return None
        if line is None:
            raise ConnectionError("kinetica-core ended")
        return line


class CoreHost:
    """A fresh kinetica-core with a folder of its own, shaped like the recorder's Window (`wire`, `dir`, `call`)."""

    def __init__(self):
        self.dir = tempfile.mkdtemp(prefix="ke-core-")
        self.proc = subprocess.Popen([find_core()], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, cwd=self.dir)
        self.wire = CoreWire(self.proc)

    def call(self, op, args=None, ident=1):
        self.wire.send(json.dumps({"id": ident, "op": op, "args": args or {}}, ensure_ascii=False).encode())
        return json.loads(self.wire.read_line(120))["result"]

    def finish(self):
        self.proc.stdin.close()
        try:
            self.proc.wait(10)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait()
        self.proc.stdout.close()
        shutil.rmtree(self.dir, ignore_errors=True)


def seen(answer, host=None):
    """What is compared: the masks of the recorder, the frame timing (the window's alone) left out, the host's own folder as {DIR}."""
    def drop(v):
        if isinstance(v, dict):
            return {k: drop(x) for k, x in v.items() if k != "frame_timing"}
        return [drop(x) for x in v] if isinstance(v, list) else v
    return drop(json.loads(json.dumps(rec.mask_answer(answer, None), ensure_ascii=False).replace(host.dir if host else "\0", rec.DIR_MARK)))


def ask(host, op, **args):
    return host.call(op, json.loads(json.dumps(args).replace(rec.DIR_MARK, host.dir)))


def placed(lines, made, light):
    """Where the window stood the light (1.5 m under the point it took) or the camera (its eye) that line `made` added, read off its scene_get."""
    for line in lines[made + 1:]:
        if line["request"]["op"] == "scene_get" and line["request"]["args"]["id"] == lines[made]["answer"]["id"]:
            for entry in line["answer"]["lines"]:
                if entry[0] == "pose" and len(entry) > 4:
                    return [entry[2], entry[3] - (1.5 if light else 0), entry[4]]
    raise AssertionError(f"no scene_get after line {lines[made]['n']} shows where the window put it")


def core_entries(recorded, fresh, ids_ops):
    """The transcript's entries for the host: what the window filled in from its selection or view is supplied, no settling, no typing gate."""
    selection, out = [], []
    for at, (line, entry) in enumerate(zip(recorded, rec.entries_of(recorded))):
        entry.update(settle=None, sleep_s=0)
        if "op" in entry:
            args = entry["args"] = json.loads(json.dumps(entry["args"])) if isinstance(entry["args"], dict) else entry["args"]
            if entry["op"] == "selection_set":
                selection = args["ids"]
            filled = line.get("filled") or []
            if entry["op"].startswith("scene_add_"):
                for key in filled:
                    args[key] = placed(fresh, at, key == "at")
            if filled and entry["op"] in ids_ops and isinstance(args, dict) and "ids" not in args:
                args["ids"] = selection
        out.append(entry)
    return out


def alike(line, window, core):
    """None when the two answers of a transcript line agree as they should, else the words that say how they do not."""
    if "write_file" in line:
        return None
    if "raw" in line:
        if "by_design" in line:
            return None
        both = [json.loads(r) if r else r for r in (window["reply"], core["reply"])]
        for reply in both:
            if reply and str(reply.get("result", {}).get("error", "")).startswith("нет операции «"):  # each host names itself
                reply["result"]["error"] = reply["result"]["error"].split("»")[0]
        return None if both[0] == both[1] else f"window {window['reply']!r} host {core['reply']!r}"
    if "window_only" in line:
        return None
    want = window["answer"]
    if "by_design" in line and "mask" not in line and isinstance(want, dict) and "until_stop" in want:
        want = {**{k: v for k, v in want.items() if k != "until_stop"}, "play_stopped": True, "reload": "joints"}
    want, got = seen(want), seen(core["answer"])
    return None if want == got else f"window {json.dumps(want, ensure_ascii=False)[:400]}\n    host   {json.dumps(got, ensure_ascii=False)[:400]}"


@unittest.skipIf(why_not(), why_not() or "")
class HostsAgree(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.window = cls.core = None
        try:
            cls.window = rec.Window(0)
            cls.core = CoreHost()
            cls.recorded = rec.read_transcript(os.path.join(rec.DATA, "window-transcript.jsonl"))[1]
            cls.fresh = rec.play(cls.window, rec.entries_of(cls.recorded))
            cls.window_ops = cls.window.call("ops_list")["ops"]
            ids_ops = {o["name"] for o in cls.window_ops if "ids" in o["schema"]["properties"]}
            cls.hosted = rec.play(cls.core, core_entries(cls.recorded, cls.fresh, ids_ops))
        except BaseException:
            cls.tearDownClass()
            raise

    @classmethod
    def tearDownClass(cls):
        if cls.core:
            cls.core.finish()
        if cls.window:
            cls.window.finish()
            cls.window.wire.close()

    def both(self, op, **args):
        """The same request to both hosts; the answers must be equal; the window's is returned."""
        w, c = ask(self.window, op, **args), ask(self.core, op, **args)
        self.assertEqual(seen(w, self.window), seen(c, self.core), f"{op} {json.dumps(args, ensure_ascii=False)[:200]}")
        return w

    def fresh_scene(self):
        self.both("sim_stop")
        self.both("scene_new")

    def test_the_transcript(self):
        self.assertEqual(len(self.recorded), len(self.hosted))
        bad = [f"line {line['n']} ({line['note']})\n    {how}" for line, w, c in zip(self.recorded, self.fresh, self.hosted)
               if (how := alike(line, w, c))]
        self.assertFalse(bad, "\n".join(bad))

    def test_the_ur5e_runs_alike(self):
        self.fresh_scene()
        self.assertTrue(self.both("scene_open", file=UR5E)["ok"])
        self.both("sim_step", frames=60)
        self.both("sim_state")
        objects = self.both("scene_list")["objects"]
        joints = [o["id"] for o in objects if o["kind"] == "joint"]
        self.assertTrue(joints)
        self.both("sim_measure", scene=True, objects=[objects[1]["id"]], joints=joints)
        self.assertTrue(self.both("sensor_read")["sensors"])
        self.assertTrue(self.both("robot_command", commands={str(joints[0]): {"target": 0.3}})["ok"])
        self.both("sim_step", frames=30)
        self.both("sim_measure", joints=joints)
        self.both("sim_state")
        self.both("sim_stop")

    def test_every_kind_of_edit_with_undo_and_redo(self):
        self.fresh_scene()
        moving = self.both("scene_add", shape="box", role="rigid", kinematic=True, position=[0, 1, 0], name="Ходит")["id"]
        zone = self.both("scene_add", shape="box", role="collider", position=[1, 0.5, 0], size=[1, 1, 1], name="Зона")["id"]
        self.assertTrue(self.both("scene_collider", id=zone, kind="box", size=[1, 1, 1], trigger=True)["ok"])
        self.both("scene_tag", ids=[moving, zone], tag="watched")
        row = self.both("scene_array", ids=[zone], count=3, step=[0, 0, 1.5])
        self.both("component_set", id=moving, type="probe.part", values={"zeta": 1, "alpha": [1, 2, 3], "on": True})
        self.both("scene_list", tag="watched")
        for undo in ("edit_undo", "edit_undo", "edit_redo", "edit_undo", "edit_redo", "edit_redo", "edit_redo"):
            self.both(undo)
        for each in self.both("scene_list")["objects"]:
            self.both("scene_get", id=each["id"])
        self.assertTrue(row["ok"])

    def test_a_scene_saved_by_one_host_is_opened_by_the_other(self):
        self.fresh_scene()
        self.both("scene_add", shape="sphere", role="rigid", position=[0, 2, 0], name="Шар")
        self.both("scene_add_light", kind="point", at=[0, 3, 0])
        force = self.both("scene_add_force", shape="radial", mode="impulse", at=[0, 1, 0])["id"]
        self.assertFalse(ask(self.window, "force_fire", id=force)["ok"])
        self.both("force_fire", id=force)
        self.both("scene_save", file=rec.DIR_MARK + "/a.kscene")
        text = {}
        for host in (self.window, self.core):
            with open(os.path.join(host.dir, "a.kscene"), encoding="utf-8") as f:
                text[host] = f.read()
        self.assertEqual(text[self.window], text[self.core], "the two hosts write the same scene text")
        for sender, taker in ((self.window, self.core), (self.core, self.window)):
            shutil.copy(os.path.join(sender.dir, "a.kscene"), os.path.join(taker.dir, "from.kscene"))
        self.both("scene_new")
        self.both("scene_open", file=rec.DIR_MARK + "/from.kscene")
        self.both("scene_list")
        self.both("scene_save", file=rec.DIR_MARK + "/b.kscene")
        for host in (self.window, self.core):
            with open(os.path.join(host.dir, "b.kscene"), encoding="utf-8") as f:
                self.assertEqual(f.read(), text[self.window], "a scene opened from the other host's file and saved again is the same text")

    def test_the_host_has_no_clock(self):
        answer = ask(self.core, "sim_play")
        self.assertFalse(answer["ok"])
        self.assertIn("sim_step", answer["error"])
        self.assertIn("kinetica-core", answer["error"])
        self.assertTrue(ask(self.window, "sim_state")["ok"])
        names = {o["name"] for o in self.window_ops}
        self.assertTrue({"sim_play", "sim_pause"} <= names)
        self.assertFalse({"sim_play", "sim_pause"} & {o["name"] for o in ask(self.core, "ops_list")["ops"]})

    def test_an_unknown_operation_names_the_host(self):
        core, window = ask(self.core, "no_such_operation"), ask(self.window, "no_such_operation")
        self.assertIn("kinetica-core", core["error"])
        self.assertNotIn("kinetica-core", window["error"])
        self.assertIn("no_such_operation", window["error"])

    def test_sim_state_has_no_frame_timing(self):
        self.fresh_scene()
        self.both("scene_add", shape="box", role="rigid", position=[0, 1, 0])
        self.both("sim_step", frames=2)
        self.assertNotIn("frame_timing", ask(self.core, "sim_state"))
        self.assertIn("frame_timing", ask(self.window, "sim_state"))
        self.both("sim_stop")

    def test_the_hosts_own_operation_set(self):
        theirs = {o["name"]: o for o in ask(self.core, "ops_list")["ops"]}
        ours = {o["name"]: o for o in ask(self.window, "ops_list")["ops"]}
        self.assertLess(set(theirs), set(ours))
        for name in ("selection_set", "view_get", "plugin_list", "ui_trigger", "journal_lines"):
            self.assertIn(name, ours)
            self.assertNotIn(name, theirs)
        self.assertEqual([n for n in theirs if theirs[n] != ours[n]], [], "each shared entry equals the window's")


if __name__ == "__main__":
    unittest.main()
