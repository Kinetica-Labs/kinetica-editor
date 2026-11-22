#!/usr/bin/env python3
"""Records what the present editor window answers, as the oracle every later host is checked against (only the Python standard library).

The window is started hidden (build/kinetica --agent-eval, no --scene) in a throwaway config, the requests of TRANSCRIPT below
go over a raw TCP socket one JSON line at a time, and two data files are written next to the C++ tests:

    tests/data/window-transcript.jsonl   line 1 {"about": ...}; then one request per line with the window's answer
    tests/data/ops-list.json             the answer of ops_list {}, sorted keys, one operation per line

    python3 tools/record_window_transcript.py             record (rewrites both files)
    python3 tools/record_window_transcript.py --check     a second fresh window, the committed requests replayed, every answer
                                                           compared after masking; the exit code is the number of differing lines
    options: --port N (default: a free port), --out DIR (default tests/data)

The requests are data, not code: the list is read top to bottom, each line says what it pins. Where the transcript's text is
what the window printed, it is kept as printed; what changes from run to run is masked ("<masked>"), and what the future
windowless host says differently on purpose is marked "by_design". Where the file is read, {DIR} stands for the folder files
are written to and read from (a temporary one here).
"""
import argparse
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, "tests", "data")
MASK = "<masked>"
MASKED_ANYWHERE = ("step_ms", "frame_timing")  # wall-clock bookkeeping of a step; the key and its whole value
DIR_MARK = "{DIR}"
MAX_EXIT = 255


# ---------------------------------------------------------------------------------------------------------------------------
# The transcript: (note, op, args, flags) or (note, raw line, flags). Flags: window_only, filled (keys the window fills in by
# itself), mask (top-level answer keys whose text is the machine's), by_design (why the windowless host will say it otherwise),
# settle (the answer to wait for: the request is repeated until the window says exactly this, for a change it learns of a frame later),
# sleep_s (wait before sending), write_file (a file the line needs: {"path", "text"}), kept_raw (the line is sent as is).
# ---------------------------------------------------------------------------------------------------------------------------
def R(note, op, args=None, **flags):
    return dict(note=note, op=op, args=args, **flags)


def W(note, line, **flags):
    return dict(note=note, raw=line, **flags)


def F(note, path, text):
    return dict(note=note, write_file={"path": path, "text": text})


IO_TEXT = ("the window prints the operating system's message as the Qt catalogue of that machine translates it (masked: a Qt without "
           "the Russian catalogue says it in English, and the empty name is Qt's English text); the host will say it from its own Russian set")
JSON_TEXT = "the window prints Qt's parser message; the host prints «не JSON: <что> (знак N)»"
UNKNOWN_OP = "the window says «нет операции «X» (ops_list называет все)»; the host names itself and what only the window has"
NO_SELECTION = "the host has no selection: the operation gets the ids it is given, and with none refuses in its own words (a replay passes the window's selection)"
JOINT_SHAPE = "the window learns of a joint's refusal a frame later and answers until_stop; the host calls setJoints in place and answers play_stopped with reload joints"
BOX = {"shape": "box", "role": "rigid"}

TRANSCRIPT = [
    # --- a fresh window, edit mode: the floor, the empty history, every world operation without a world -------------------
    R("the floor that a fresh window holds, id 1", "scene_list"),
    R("scene text of the fresh scene: the floor only, gravity, no history", "scene_get"),
    R("edit mode: no world, only the mode", "sim_state"),
    R("nothing to undo in a fresh window", "edit_undo"),
    R("nothing to redo in a fresh window", "edit_redo"),
    R("sim_measure with no world and no fields", "sim_measure"),
    R("sim_measure with a field, no world", "sim_measure", {"scene": True}),
    R("sensor_read with no world", "sensor_read"),
    R("robot_command with no world", "robot_command", {"commands": {"1": {"force": [0, 1, 0]}}}),
    R("controller_input with no world and no controller", "controller_input", {"values": {"move": [0, 1]}}),
    R("sim_impulse with no world", "sim_impulse", {"body": 1, "impulse": [0, 1, 0]}),
    R("sim_stop in edit mode", "sim_stop"),
    R("sim_stop twice", "sim_stop"),
    R("sim_pause in edit mode (the host has no clock and will not have the operation)", "sim_pause", window_only=True),
    R("ops_list of the sim family: the host lists only its own subset of these names; entries of shared names are equal", "ops_list",
      {"families": ["sim"]}, window_only=True),
    R("ops_list of a family that is not there", "ops_list", {"families": ["nosuchfamily"]}),
    R("an operation the window does not have", "no_such_operation", mask=["error"], by_design=UNKNOWN_OP),
    R("a field the operation does not take", "scene_list", {"zz": 1}),
    R("scene_open with no file: the required-field text", "scene_open"),
    R("scene_save with no file: the required-field text", "scene_save"),
    R("edit_undo with a field: it takes none", "edit_undo", {"x": 1}),
    R("sim_measure with group in edit mode: no world", "sim_measure", {"group": [1]}),
    R("sim_measure with goals that is not a list", "sim_measure", {"goals": True}),

    # --- the scene, built only by operations (every id is given; the window's ids count from 1) ---------------------------
    R("a rigid box that will fall: id 2", "scene_add", dict(BOX, position=[0, 1, 0], size=[0.2, 0.2, 0.2], name="Падающий")),
    R("a fixed post: id 3", "scene_add", dict(BOX, fixed=True, position=[1, 0.5, 0], size=[0.1, 1, 0.1], name="Стойка")),
    R("a rod beside the post: id 4", "scene_add", dict(BOX, position=[1.4, 1.0, 0], size=[0.8, 0.05, 0.05], name="Штанга")),
    R("the rod on a hinge with a servo (joint id 5)", "assembly_actuator", {"kind": "rotary", "a": 3, "b": 4, "axis": [0, 0, 1], "anchor": [1, 1, 0]}),
    R("a controller for the servo (id 6): the one controller_input drives", "controller_add", {"joint": 5, "action": "swing", "speed": 1}),
    R("a chassis far from the others: id 7", "scene_add", dict(BOX, position=[-1, 0.14, 0], size=[0.4, 0.08, 0.26], name="Шасси")),
    R("a wheel module: motor joint 11, battery 13, driver 14 (the duty is edited later)", "assembly_wheel", {"target": 7, "side": "front_left"}),
    R("an IMU with a datasheet model on the chassis top (id 15)", "assembly_sensor", {"target": 7, "kind": "imu", "face": "+y"}),
    R("a sonar on the chassis front (id 16)", "assembly_sensor", {"target": 7, "kind": "sonar", "face": "+x"}),
    R("a light with its place given (id 17)", "scene_add_light", {"kind": "point", "at": [0, 2, 0]}),
    R("a camera with eye and target given (id 18)", "scene_add_camera", {"eye": [3, 2, 3], "target": [0, 0, 0]}),
    R("the whole scene as text: the oracle of what scene_get says, floats as the window formats them", "scene_get"),
    R("scene_list of the built scene", "scene_list"),
    R("scene_get of one object", "scene_get", {"id": 5}),
    R("scene_get of an id that is not there", "scene_get", {"id": 999}),
    R("the history after the build: nothing to redo, undo_steps counts the steps the edits made", "edit_redo"),
    R("scene_open of a relative name that is not there (relative paths of a success are not recorded: the folder is the process's own)", "scene_open",
      {"file": "no-such-relative.kscene"}, mask=["error"], by_design=IO_TEXT),

    # --- files: save, a missing file, a file that does not parse -------------------------------------------------------------
    R("scene_save: the answer names the file (the folder is {DIR})", "scene_save", {"file": "{DIR}/scene.kscene"}),
    R("scene_save to a folder that is not there", "scene_save", {"file": "{DIR}/nodir/x.kscene"}, mask=["error"], by_design=IO_TEXT),
    R("scene_open of a missing file", "scene_open", {"file": "{DIR}/missing.kscene"}, mask=["error"], by_design=IO_TEXT),
    R("scene_open of a folder", "scene_open", {"file": "{DIR}"}, mask=["error"], by_design=IO_TEXT),
    R("scene_open with an empty name", "scene_open", {"file": ""}, mask=["error"], by_design=IO_TEXT),
    F("a file that is not a scene", "{DIR}/bad.kscene", "this is not a scene\nfoo bar\n"),
    R("scene_open of a file that does not parse: the core's English text 'line N: cannot read'", "scene_open", {"file": "{DIR}/bad.kscene"}),
    F("a block with no end", "{DIR}/noend.kscene", "# Kinetica scene\nentity \"A\"\n  shape box size 1 1 1\n"),
    R("scene_open of a block with no end", "scene_open", {"file": "{DIR}/noend.kscene"}),
    R("the failed opens changed nothing: still no step to undo beyond the edits", "edit_redo"),
    R("scene_open of the saved file: the ids stay, the history takes one step", "scene_open", {"file": "{DIR}/scene.kscene"}),
    R("the scene after the reopen equals what was saved", "scene_get"),
    R("undo of the open: the step the open made", "edit_undo"),
    R("redo of the open", "edit_redo"),

    # --- two rapid API edits of one field and the undo: do they merge by the 700 ms typing gate? ---------------------------------
    R("the step count before: nothing to redo, undo_steps says how many steps there are", "edit_redo"),
    R("first rapid scene_set of the light's intensity", "scene_set", {"id": 17, "line": "kind", "key": "intensity", "values": [2]}),
    R("second rapid scene_set of the same field, at once", "scene_set", {"id": 17, "line": "kind", "key": "intensity", "values": [3]}),
    R("the step count after the two: one more means they merged, two more means they did not", "edit_redo"),
    R("edit_undo: does one undo take back one set or both", "edit_undo"),
    R("the intensity after the undo: 1 is both taken back, 2 is one", "scene_get", {"id": 17, "line": "kind", "key": "intensity"}),
    R("edit_redo restores", "edit_redo"),
    R("the intensity after the redo", "scene_get", {"id": 17, "line": "kind", "key": "intensity"}),
    R("first set of another value after a pause of 0.9 s", "scene_set", {"id": 17, "line": "kind", "key": "intensity", "values": [4]}, sleep_s=0.9),
    R("second set of the same field after another pause of 0.9 s", "scene_set", {"id": 17, "line": "kind", "key": "intensity", "values": [5]}, sleep_s=0.9),
    R("the step count after the paused pair", "edit_redo"),
    R("edit_undo after the paused pair", "edit_undo"),
    R("the intensity after that undo: 4 is one step back, 3 is both", "scene_get", {"id": 17, "line": "kind", "key": "intensity"}),
    R("edit_redo", "edit_redo"),
    R("the scene's links as one list: the hinge's two bodies, the wheel module's parts on the chassis, the driver's joint and battery, the controller's action", "relation_list"),
    R("what refers to the chassis (id 7): the links into it", "relation_list", {"to": 7}),
    R("the format's kinds of block and their lines: what the scene file holds", "schema_list"),
    R("a joint's lines and keys as the reader reads them: forms and units", "schema_get", {"kind": "joint"}),

    # --- what the window fills in by itself: selection and view -----------------------------------------------------------------
    R("the selection of a window that has not selected anything by hand", "selection_get", window_only=True),
    R("selection_set of a known object", "selection_set", {"ids": [3]}, window_only=True),
    R("a light with no `at` after selection_set: the window puts it above the selection", "scene_add_light", {"kind": "point"},
      window_only=True, filled=["at"], by_design="the host fills nothing: the light goes to the scene centre"),
    R("a camera with no `eye` after selection_set: the window takes its view", "scene_add_camera", window_only=True, filled=["eye"],
      by_design="the host fills nothing: the camera goes to the centre + (2.5, 1.5, 2.5)"),
    R("the selection after the two additions: the added object", "selection_get", window_only=True),
    R("a camera again with the same view: only the first camera is active", "scene_add_camera", window_only=True, filled=["eye"],
      by_design="as above"),
    R("selection_set of nothing", "selection_set", {"ids": []}, window_only=True),
    R("a light with no `at` and nothing selected: the window takes the scene's centre spawn point", "scene_add_light", {"kind": "point"},
      window_only=True, filled=["at"], by_design="as above"),
    R("scene_get of the first light: where the window put it over the selection", "scene_get", {"id": 19}, window_only=True, filled=["at"]),
    R("scene_get of the second light: where the window put it with nothing selected", "scene_get", {"id": 22}, window_only=True, filled=["at"]),
    R("scene_get of the first camera: the eye the window took from its view", "scene_get", {"id": 20}, window_only=True, filled=["eye"]),
    R("scene_get of the third camera: the same view, not the active camera", "scene_get", {"id": 21}, window_only=True, filled=["eye"]),
    R("selection_set of the post and the box", "selection_set", {"ids": [3, 2]}, window_only=True),
    R("scene_tag with no ids and a selection: the window fills ids from the selection", "scene_tag", {"tag": "probe"},
      window_only=True, filled=["ids"], by_design=NO_SELECTION),
    R("scene_list of the tag: which objects the selection carried", "scene_list", {"tag": "probe"}, window_only=True, filled=["ids"]),
    R("selection_set of nothing again", "selection_set", {"ids": []}, window_only=True),
    R("scene_tag with no ids and nothing selected: refused by the operation itself", "scene_tag", {"tag": "probe2"},
      window_only=True, filled=["ids"], by_design=NO_SELECTION),
    R("scene_list of the second tag: nobody", "scene_list", {"tag": "probe2"}, window_only=True, filled=["ids"]),
    R("scene_move with no ids and nothing selected", "scene_move", {"by": [0, 0, 0]}, window_only=True, filled=["ids"],
      by_design=NO_SELECTION),
    R("scene_delete with no ids and nothing selected", "scene_delete", window_only=True, filled=["ids"],
      by_design=NO_SELECTION),
    R("an empty list of ids given: refused with the same text as no ids", "scene_tag", {"ids": [], "tag": "probe3"}),
    R("scene_duplicate with an empty list of ids", "scene_duplicate", {"ids": []}),
    R("scene_group with no ids and nothing selected", "scene_group", window_only=True, filled=["ids"], by_design=NO_SELECTION),

    # --- ids in odd forms (an operation that takes ids; the tag is harmless) -------------------------------------------------------
    R("ids as a number", "scene_tag", {"ids": 2, "tag": "t1"}),
    R("ids as a string", "scene_tag", {"ids": "2", "tag": "t2"}),
    R("ids as a list with a string", "scene_tag", {"ids": [2, "3"], "tag": "t3"}),
    R("ids as a list with a float", "scene_tag", {"ids": [2.5], "tag": "t4"}),
    R("ids as a list with a whole float", "scene_tag", {"ids": [2.0], "tag": "t5"}),
    R("ids as a list with a null", "scene_tag", {"ids": [None, 2], "tag": "t6"}),
    R("ids with an id that is not there", "scene_tag", {"ids": [999], "tag": "t7"}),
    R("ids as an object", "scene_tag", {"ids": {"a": 1}, "tag": "t8"}),
    R("the tags the odd forms left", "scene_list", {"tag": "t1"}),
    R("tag t2", "scene_list", {"tag": "t2"}),
    R("tag t3", "scene_list", {"tag": "t3"}),
    R("tag t4", "scene_list", {"tag": "t4"}),
    R("tag t5", "scene_list", {"tag": "t5"}),
    R("tag t6", "scene_list", {"tag": "t6"}),
    R("tag t7", "scene_list", {"tag": "t7"}),
    R("tag t8", "scene_list", {"tag": "t8"}),
    R("scene_move with ids as a string", "scene_move", {"ids": "2", "by": [0, 0, 0]}),
    R("scene_move with a two-item vector", "scene_move", {"ids": [2], "by": [1, 2]}),
    R("scene_move with a four-item vector", "scene_move", {"ids": [2], "by": [0, 0, 0, 5]}),
    R("scene_add with a two-item position", "scene_add", {"shape": "box", "position": [1, 2]}),
    R("scene_add with a four-item position", "scene_add", {"shape": "box", "position": [1, 2, 3, 4]}),

    # --- component_set order, assembly_sensor's bad spec -----------------------------------------------------------------------
    R("component_set with two value keys given in reverse alphabetical order", "component_set",
      {"id": 2, "type": "probe.part", "values": {"zeta": 1, "alpha": 2}}),
    R("component_list: the keys come back sorted", "component_list"),
    R("scene_get of the object: the order the keys are stored in (its component line)", "scene_get", {"id": 2}),
    R("component_set with three more keys in reverse order, a string and a list among them", "component_set",
      {"id": 2, "type": "probe.part", "values": {"zz": 1, "mid": "x", "aa": [1, 2]}}),
    R("component_list after the second set", "component_list", {"type": "probe.part"}),
    R("component_set with a bool and an object as values", "component_set", {"id": 2, "type": "probe.part", "values": {"nest": {"b": 1, "a": 2}, "mid": True}}),
    R("component_list: how the bool and the object were stored", "component_list"),
    R("scene_get of the object: the stored order after three sets", "scene_get", {"id": 2}),
    R("component_set with no values", "component_set", {"id": 2, "type": "probe.part", "values": {}}),
    R("component_set with a type that has no dot", "component_set", {"id": 2, "type": "probe", "values": {"a": 1}}),
    R("component_remove of the component", "component_remove", {"id": 2, "type": "probe.part"}),
    R("component_list after the removal", "component_list"),
    R("assembly_sensor with a spec of two bad keys in reverse order: which is named first", "assembly_sensor",
      {"target": 7, "kind": "imu", "face": "-y", "spec": {"zzz": 1, "aaa": 2}}),
    R("assembly_sensor with one bad spec key", "assembly_sensor", {"target": 7, "kind": "imu", "face": "-y", "spec": {"aaa": 2}}),
    R("assembly_sensor with a good spec key and a bad one", "assembly_sensor", {"target": 7, "kind": "imu", "face": "-y", "spec": {"rate_hz": 500, "aaa": 2}}),

    # --- the world: build, step, read, command ---------------------------------------------------------------------------------
    R("sim_step 0 in edit mode builds the world paused", "sim_step", {"frames": 0}),
    R("sim_state of a built world, frame 0", "sim_state"),
    R("sim_step 3", "sim_step", {"frames": 3}),
    R("sim_state after three frames", "sim_state"),
    R("sim_step with no frames: one", "sim_step"),
    R("sim_measure with no fields on a built world", "sim_measure"),
    R("sim_measure: the scene, a body, a joint", "sim_measure", {"scene": True, "objects": [2], "joints": [5]}),
    R("sim_measure of an id that is not there", "sim_measure", {"objects": [999]}),
    R("sim_measure of a group on a built world: the group's mass and centre", "sim_measure", {"group": [2]}),
    R("sim_measure with group that is not a list", "sim_measure", {"group": "x"}),
    R("sim_measure with goals on a built world", "sim_measure", {"goals": [1]}),
    R("sensor_read of all sensors: a datasheet's reading of the IMU and the sonar", "sensor_read"),
    R("sensor_read of one sensor", "sensor_read", {"ids": [15]}),
    R("sensor_read of an id that is not a sensor", "sensor_read", {"ids": [77]}),
    R("robot_command: a servo target", "robot_command", {"commands": {"5": {"target": 0.2}}}),
    R("robot_command: a driver's duty", "robot_command", {"commands": {"14": {"duty": 0.5}}}),
    R("robot_command: a force on a body", "robot_command", {"commands": {"2": {"force": [0, 5, 0]}}}),
    R("robot_command with an id that is not there", "robot_command", {"commands": {"9999": {"force": [0, 1, 0]}}}),
    R("robot_command with a word it does not know", "robot_command", {"commands": {"2": {"wat": 0.5}}}),
    R("robot_command with no commands", "robot_command", {"commands": {}}),
    R("robot_command with an empty entry", "robot_command", {"commands": {"2": {}}}),
    R("robot_command with a key that is not a number: Qt's toUInt gives 0", "robot_command", {"commands": {"7x": {"force": [0, 1, 0]}}}),
    R("robot_command with two bad entries in reverse order: the joined error text is sorted by the key text, not by the order given", "robot_command",
      {"commands": {"9": {"bogus": 1}, "5": {"wat": 2}}}),
    R("robot_command with keys 10 and 9 given in that order: string order puts 10 first, numeric order would put 9 first", "robot_command",
      {"commands": {"9": {"bogus": 1}, "10": {"wat": 2}}}),
    R("robot_command with three bad entries in reverse order", "robot_command", {"commands": {"zz": {"force": [0, 1, 0]}, "999": {"wat": 1}, "5": {"bogus": 2}}}),
    R("sim_step 1 after the commands", "sim_step", {"frames": 1}),
    R("controller_input drives the controller by its action", "controller_input", {"values": {"swing": 1}}),
    R("controller_input with the id given", "controller_input", {"id": 6, "values": {"swing": -1}}),
    R("controller_input with an action it does not have", "controller_input", {"values": {"jog": 1}}),
    R("controller_input with no values", "controller_input", {"values": {}}),
    R("controller_input with an id that is not a controller", "controller_input", {"id": 999, "values": {"swing": 1}}),
    R("controller_input with no arguments", "controller_input"),
    R("sim_impulse on a free body", "sim_impulse", {"body": 2, "impulse": [0, 2, 0]}),
    R("sim_impulse with a point", "sim_impulse", {"body": 2, "impulse": [0, 0.1, 0], "point": [0.05, 1, 0]}),
    R("sim_impulse on a fixed body", "sim_impulse", {"body": 3, "impulse": [0, 2, 0]}),
    R("sim_impulse on an id that is not there", "sim_impulse", {"body": 999, "impulse": [0, 2, 0]}),
    R("sim_impulse on body 0", "sim_impulse", {"body": 0, "impulse": [0, 2, 0]}),
    R("sim_impulse with a two-item vector", "sim_impulse", {"body": 2, "impulse": [0, 2]}),
    R("sim_impulse with a four-item vector", "sim_impulse", {"body": 2, "impulse": [0, 2, 0, 1]}),
    R("sim_impulse with an unknown plain key", "sim_impulse", {"body": 2, "impulse": [0, 2, 0], "bogus": 1}),
    R("sim_impulse with an unknown underscore key (the client's _py travels so)", "sim_impulse", {"body": 2, "impulse": [0, 2, 0], "_py": "x = 1"}),
    R("sim_impulse with a three-item vector of strings", "sim_impulse", {"body": 2, "impulse": ["a", "b", "c"]}),
    R("sim_impulse with no impulse", "sim_impulse", {"body": 2}),
    R("sim_step 1: what the impulse and the commands did", "sim_step", {"frames": 1}),

    # --- sim_step's frames, odd values (the window is Qt: QJsonValue::toInt) --------------------------------------------------
    R("sim_step frames as a string", "sim_step", {"frames": "3"}),
    R("sim_step frames 2.5", "sim_step", {"frames": 2.5}),
    R("sim_step frames 1e10 (outside int): requested_frames 1 is the default taken when the value does not fit an int, not a clamp", "sim_step", {"frames": 1e10}),
    R("sim_step frames -1", "sim_step", {"frames": -1}),
    R("sim_step frames -5", "sim_step", {"frames": -5}),
    R("sim_step frames null", "sim_step", {"frames": None}),
    R("sim_step frames true", "sim_step", {"frames": True}),
    R("sim_step frames 4.0 (a whole float)", "sim_step", {"frames": 4.0}),
    R("sim_step frames as a list", "sim_step", {"frames": [2]}),
    R("sim_step with a field it does not know", "sim_step", {"frames": 1, "bogus": 1}),
    R("sim_step frames 100000: clamped to 6000", "sim_step", {"frames": 100000}),
    R("sim_state after the long run", "sim_state"),
    R("sim_stop of a built world", "sim_stop"),
    R("sim_stop twice", "sim_stop"),
    R("sim_state after the stop: edit", "sim_state"),
    R("sim_step 0 after the stop: a world from frame 0", "sim_step", {"frames": 0}),
    R("sim_measure with an empty request on a fresh world", "sim_measure"),
    R("sim_stop", "sim_stop"),

    # --- sim_play / sim_pause: the window's clock; each followed at once by a stop -----------------------------------------------
    R("sim_play from edit (time of its run is not recorded)", "sim_play", window_only=True),
    R("sim_play twice", "sim_play", window_only=True),
    R("sim_pause of a playing scene", "sim_pause", window_only=True),
    R("sim_pause twice", "sim_pause", window_only=True),
    R("sim_stop after the play", "sim_stop", window_only=True),
    R("sim_stop twice after the play", "sim_stop", window_only=True),
    R("sim_state after it: edit", "sim_state", window_only=True),

    # --- edits while the world is built -----------------------------------------------------------------------------------------
    R("a world to edit: sim_step 2 builds it", "sim_step", {"frames": 2}),
    R("baseline: the motor joint 11 before any edit", "sim_measure", {"joints": [11]}),
    R("five frames with no edit: how far the motor joint moves by itself", "sim_step", {"frames": 5}),
    R("the motor joint after the baseline run", "sim_measure", {"joints": [11]}),
    R("the driver's duty edited while built, and nothing else: the world takes it", "scene_set", {"id": 14, "line": "bridge", "key": "duty", "values": [0.5]}),
    R("five frames with the duty only", "sim_step", {"frames": 5}),
    R("the motor joint after the duty run: it moved much further than in the baseline, so the duty is taken (the light's pickup is not pinned: nothing reads a light back)", "sim_measure", {"joints": [11]}),
    R("sim_state right after: still built", "sim_state"),
    R("a light moved while built: the answer says taken (unproven: no line reads a light of the world)", "scene_set", {"id": 17, "line": "pose", "key": "position", "values": [0, 3, 0]}),
    R("a joint's drive target edited while built: taken", "scene_set", {"id": 5, "line": "drive", "key": "target", "values": [0.3]}),
    R("the mode after the three taken edits", "sim_state"),
    R("sim_step 2: the world went on from frame 2 (the duty and the target act)", "sim_step", {"frames": 2}),
    R("sim_stop: the taken edits are lost with the world", "sim_stop"),
    R("the duty after the stop", "scene_get", {"id": 14}),
    R("the light after the stop", "scene_get", {"id": 17}),
    R("the joint's target after the stop", "scene_get", {"id": 5}),
    R("a world again", "sim_step", {"frames": 2}),
    R("a body moved while built: the world cannot take it, so it stops and says so (play_stopped, reload)", "scene_move", {"ids": [2], "by": [0, 0.1, 0]}),
    R("the mode right after: the world is gone", "sim_state"),
    R("the body moved is kept after the world is gone", "scene_get", {"id": 2}),
    R("sim_step 1: a new world, from frame 0", "sim_step", {"frames": 1}),
    R("a body added while built: the world stops, play_stopped", "scene_add", dict(BOX, shape="sphere", position=[0, 2, 0], size=[0.1, 0.1, 0.1], name="Шар")),
    R("the mode right after the body was added", "sim_state"),
    R("sim_step 1 after the body: from frame 0", "sim_step", {"frames": 1}),
    R("the world's gravity edited while built: the world cannot take it, play_stopped", "world_set", {"line": "world", "key": "gravity", "values": [0, -5, 0]}),
    R("the mode after the gravity edit", "sim_state"),
    R("the scene text after the gravity edit: the world line keeps the new gravity", "scene_get"),
    R("sim_step 2: a new world, from frame 0", "sim_step", {"frames": 2}),
    R("a joint's shape edited while built (anchor on the post): the window learns of the refusal a frame later", "scene_set",
      {"id": 5, "line": "link", "key": "anchor_a", "values": [0, 0.4, 0]}, by_design=JOINT_SHAPE),
    R("sim_state polled until it says only the mode (the sim thread's refusal arrives asynchronously: the first reading says edit but still carries the dropped world's keys)",
      "sim_state", settle={"mode": "edit", "ok": True}),
    R("the joint's anchor after the refusal: the edit is kept", "scene_get", {"id": 5, "line": "link", "key": "anchor_a"}),
    R("sim_step 1 after the refusal: a new world, from frame 0", "sim_step", {"frames": 1}),
    R("edit_undo while built: the last edit was the joint's anchor; like any edit it says until_stop (the window learns of the joint's refusal a frame later)", "edit_undo", by_design=JOINT_SHAPE),
    R("sim_state polled until clean after the undo while built (the first reading says edit with the dropped world's keys)", "sim_state", settle={"mode": "edit", "ok": True}),
    R("a world to redo into", "sim_step", {"frames": 1}),
    R("edit_redo while built: until_stop as the undo above", "edit_redo", by_design=JOINT_SHAPE),
    R("sim_state polled until clean after the redo while built", "sim_state", settle={"mode": "edit", "ok": True}),
    R("a world once more", "sim_step", {"frames": 2}),
    R("scene_open of a missing file while built: the world must stay", "scene_open", {"file": "{DIR}/missing.kscene"}, mask=["error"], by_design=IO_TEXT),
    R("the mode after: still built", "sim_state"),
    R("scene_open of a file that does not parse while built: the world must stay", "scene_open", {"file": "{DIR}/bad.kscene"}),
    R("sim_step 1: the same world goes on", "sim_step", {"frames": 1}),
    R("a world once more", "sim_step", {"frames": 2}),
    R("scene_save while built: works in any mode", "scene_save", {"file": "{DIR}/built.kscene"}),
    R("scene_open of a good file while built: parsed first, then the world goes", "scene_open", {"file": "{DIR}/scene.kscene"}),
    R("edit_redo as a no-op that shows undo_steps after the open while built", "edit_redo"),
    R("the mode after the open", "sim_state"),
    R("a world once more", "sim_step", {"frames": 2}),
    R("scene_new while built: the world goes", "scene_new"),
    R("edit_redo as a no-op that shows undo_steps after scene_new", "edit_redo"),
    R("the mode after scene_new", "sim_state"),
    R("scene_list after scene_new: a floor with the next id (the counter is never reset)", "scene_list"),
    R("edit_undo after scene_new: the scene is back", "edit_undo"),
    R("scene_list: the scene of the undo", "scene_list"),
    R("scene_get of the joint after the undo of scene_new", "scene_get", {"id": 5}),
    R("scene_new", "scene_new"),
    R("scene_new twice: the counter goes on", "scene_new"),
    R("scene_list of the second new scene", "scene_list"),
    R("a world of the floor only: sim_step 0", "sim_step", {"frames": 0}),
    R("controller_input in a world with no controller", "controller_input", {"values": {"move": [0, 1]}}),
    R("controller_input with an id in a world with no controller", "controller_input", {"id": 6, "values": {"move": [0, 1]}}),
    R("sensor_read in a world with no sensors", "sensor_read"),
    R("robot_command on the floor (it is fixed)", "robot_command", {"commands": {"26": {"force": [0, 1, 0]}}}),
    R("sim_stop", "sim_stop"),

    # --- the wire: lines that are not requests ----------------------------------------------------------------------------------
    R("args as a list instead of an object", "scene_list", [1], kept_raw=True),
    R("args as a string", "scene_list", "x", kept_raw=True),
    R("args as null", "scene_list", None, kept_raw=True),
    W("a line that is not JSON", "not json", by_design=JSON_TEXT),
    W("JSON that is a number", "42", by_design=JSON_TEXT),
    W("JSON that is a string", '"str"', by_design=JSON_TEXT),
    W("JSON that is null", "null", by_design=JSON_TEXT),
    W("JSON that is a list: an operation with no name", "[1,2]"),
    W("JSON with garbage after it", '{"id":9,"op":"scene_list"} trailing', by_design=JSON_TEXT),
    W("a request with NO id: the reply has no id key", '{"op":"scene_list","args":{"tag":"zz"}}'),
    W("a request with id null: the reply echoes null", '{"id":null,"op":"scene_list","args":{"tag":"zz"}}'),
    W("an id that is a string", '{"id":"abc","op":"scene_list","args":{"tag":"zz"}}'),
    W("an id that is a float", '{"id":1.5,"op":"scene_list","args":{"tag":"zz"}}'),
    W("an id that is a list", '{"id":[1],"op":"scene_list","args":{"tag":"zz"}}'),
    W("an id that is an object", '{"id":{"a":1},"op":"scene_list","args":{"tag":"zz"}}'),
    W("an id that is a big whole number", '{"id":12345678901,"op":"scene_list","args":{"tag":"zz"}}'),
    W("a request with no op", '{"id":4}'),
    W("a request whose op is a number", '{"id":5,"op":5}'),
    W("a request with an unknown key beside op (src is ignored)", '{"id":7,"op":"scene_list","src":"x","args":{"tag":"zz"}}'),
    W("a blank line: no answer", "", expect_no_reply=True),
    W("a request with a trailing carriage return", '{"id":10,"op":"scene_list","args":{"tag":"zz"}}\r'),
    W("a request whose key and value are written with \\u escapes (the key shape, the name Пол2): the window decodes both", '{"id":11,"op":"scene_add","args":{"sha\\u0070e":"box","name":"\\u041f\\u043e\\u043b2"}}'),
    R("scene_list: the object of the escaped line, name decoded", "scene_list"),
]


# ---------------------------------------------------------------------------------------------------------------------------
class Wire:
    """One TCP connection, one JSON line each way, with a reader that survives a timeout."""

    def __init__(self, port):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=5)
        self.buf = b""

    def send(self, data):
        self.sock.sendall(data + b"\n")

    def read_line(self, timeout):
        deadline = time.time() + timeout
        while b"\n" not in self.buf:
            left = deadline - time.time()
            if left <= 0:
                return None
            self.sock.settimeout(left)
            try:
                chunk = self.sock.recv(65536)
            except socket.timeout:
                return None
            if not chunk:
                raise ConnectionError("the window closed the connection")
            self.buf += chunk
        line, self.buf = self.buf.split(b"\n", 1)
        return line.decode("utf-8")

    def close(self):
        self.sock.close()


class Window:
    """A fresh hidden window: a throwaway config (the first-minute hint done, so the scene is the floor only), a throwaway journal."""

    def __init__(self, port):
        self.port = port or free_port()
        self.proc = None
        self.work = tempfile.mkdtemp(prefix="ke-transcript-")
        try:
            self.start()
        except BaseException:
            self.kill()
            shutil.rmtree(self.work, ignore_errors=True)
            raise

    def start(self):
        try:
            socket.create_connection(("127.0.0.1", self.port), timeout=0.5).close()
        except OSError:
            pass
        else:
            raise RuntimeError(f"port {self.port} already has a listener: a stale window would be taken for the fresh one")
        self.dir = os.path.join(self.work, "files")
        os.makedirs(self.dir)
        env = dict(os.environ, KE_JOURNAL=os.path.join(self.work, "journal.jsonl"),
                   XDG_CONFIG_HOME=os.path.join(self.work, "config"), XDG_DATA_HOME=os.path.join(self.work, "data"))
        env.pop("KE_PLUGINS", None)
        self.proc = subprocess.Popen([os.path.join(ROOT, "build", "kinetica"), "--agent-eval", "--api", str(self.port)], cwd=ROOT, env=env,
                                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        deadline = time.time() + 90
        while True:
            if self.proc.poll() is not None:
                raise RuntimeError(f"the window exited with code {self.proc.returncode} (3: the two hidden-window slots of this user are taken)")
            try:
                socket.create_connection(("127.0.0.1", self.port), timeout=0.5).close()
                break
            except OSError:
                if time.time() > deadline:
                    raise TimeoutError("the window did not open its port")
                time.sleep(0.2)
        if self.proc.poll() is not None:
            raise RuntimeError("the window exited as its port opened: the listener was not ours")
        self.wire = Wire(self.port)
        self.plugin_commands = self.wait_plugins()
        time.sleep(1.5)
        first = self.call("scene_list")
        if first.get("objects") is None or [o.get("id") for o in first["objects"]] != [1]:
            raise RuntimeError(f"not a fresh floor-only window: {json.dumps(first, ensure_ascii=False)[:200]}")

    def call(self, op, args=None, ident=1):
        self.wire.send(json.dumps({"id": ident, "op": op, "args": args or {}}, ensure_ascii=False).encode())
        return json.loads(self.wire.read_line(120))["result"]

    def wait_plugins(self):
        """The shipped plugins start a moment after the window; their commands join ops_list when they are up."""
        deadline = time.time() + 30
        while True:
            plugins = self.call("plugin_list")["plugins"]
            if all(p["state"] == "работает" for p in plugins) or time.time() > deadline:
                break
            time.sleep(0.2)
        bad = [p["name"] for p in plugins if p["state"] != "работает"]
        if bad:
            raise RuntimeError(f"plugins not up: {bad}")
        return sorted(c["op"] for p in plugins for c in p["commands"])

    def finish(self):
        """app_quit, then the process must be gone."""
        try:
            self.call("app_quit")
        except BaseException:
            pass
        try:
            self.proc.wait(20)
        except (subprocess.TimeoutExpired, KeyboardInterrupt):
            self.kill()
            raise RuntimeError("the window did not end after app_quit")
        finally:
            shutil.rmtree(self.work, ignore_errors=True)

    def kill(self):
        if self.proc is not None and self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait()


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


# ---------------------------------------------------------------------------------------------------------------------------
def mask_answer(answer, per_line):
    """The stored form of an answer: wall-clock keys anywhere, and the line's own top-level keys, replaced by "<masked>"."""
    def walk(v):
        if isinstance(v, dict):
            return {k: (MASK if k in MASKED_ANYWHERE else walk(x)) for k, x in v.items()}
        if isinstance(v, list):
            return [walk(x) for x in v]
        return v
    out = walk(answer)
    if isinstance(out, dict):
        for k in per_line or []:
            if k in out:
                out[k] = MASK
    return out


def differing_keys(a, b, path=""):
    if isinstance(a, dict) and isinstance(b, dict):
        out = []
        for k in sorted(set(a) | set(b)):
            out += differing_keys(a.get(k, "<absent>"), b.get(k, "<absent>"), f"{path}.{k}" if path else k)
        return out
    if isinstance(a, list) and isinstance(b, list) and len(a) == len(b):
        out = []
        for i, (x, y) in enumerate(zip(a, b)):
            out += differing_keys(x, y, f"{path}[{i}]")
        return out
    return [] if a == b else [path or "<whole>"]


def play(window, entries, number_from=1):
    """Sends every entry to the window; returns the stored lines (with {DIR} for the folder and the masks applied)."""
    out = []
    to_dir = lambda text: text.replace(DIR_MARK, window.dir)
    from_dir = lambda text: text.replace(window.dir, DIR_MARK)
    for n, entry in enumerate(entries, number_from):
        line = {"n": n, "note": entry["note"]}
        if "write_file" in entry:
            path = to_dir(entry["write_file"]["path"])
            with open(path, "w", encoding="utf-8") as f:
                f.write(entry["write_file"]["text"])
            line["write_file"] = entry["write_file"]
            out.append(line)
            continue
        if entry.get("sleep_s"):
            time.sleep(entry["sleep_s"])
        if "raw" in entry:
            text = to_dir(entry["raw"])
            window.wire.send(text.encode("utf-8"))
            reply = window.wire.read_line(1.5 if entry.get("expect_no_reply") else 30)
            line["raw"] = entry["raw"]
            line["reply"] = None if reply is None else from_dir(reply)
            for key in ("by_design", "expect_no_reply"):
                if key in entry:
                    line[key] = entry[key]
            out.append(line)
            continue
        op, args = entry["op"], entry["args"]
        wire_args = json.loads(to_dir(json.dumps(args, ensure_ascii=False))) if args is not None else {}
        message = {"id": n, "op": op, "args": args if entry.get("kept_raw") else wire_args}  # a wrong shape is sent as it is
        window.wire.send(json.dumps(message, ensure_ascii=False).encode("utf-8"))
        reply = window.wire.read_line(120)
        if reply is None:
            raise TimeoutError(f"line {n} ({op}): no answer in 120 s")
        envelope = json.loads(reply)
        if envelope.get("id") != n:
            raise RuntimeError(f"line {n} ({op}): the reply's id is {envelope.get('id')!r}")
        answer = json.loads(from_dir(json.dumps(envelope["result"], ensure_ascii=False)))
        first_reading = None
        if entry.get("settle"):
            target, first = entry["settle"], mask_answer(answer, entry.get("mask"))
            deadline = time.time() + 5
            while mask_answer(answer, entry.get("mask")) != target and time.time() < deadline:
                time.sleep(0.05)
                window.wire.send(json.dumps(message, ensure_ascii=False).encode("utf-8"))
                answer = json.loads(from_dir(window.wire.read_line(30)))["result"]
            first_reading = first if first != target else None
        line["request"] = {"op": op, "args": args if entry.get("kept_raw") or args is not None else {}}
        line["answer"] = mask_answer(answer, entry.get("mask"))
        if first_reading is not None:
            line["first_reading"] = first_reading
        for key in ("window_only", "filled", "mask", "by_design", "settle", "sleep_s", "kept_raw"):
            if entry.get(key):
                line[key] = entry[key]
        out.append(line)
    return out


ABOUT = {
    "about": "The answers of the editor window (build/kinetica --agent-eval, started with no --scene in a throwaway config) to the requests below, in "
             "order, over one connection; recorded by tools/record_window_transcript.py before any logic left the window. Replayed offline "
             "by ke_tests against ke::Scene and live against both hosts. Every line is {n, note, request{op,args}, answer}; the "
             "request is sent as {\"id\": n, \"op\", \"args\"}. Fields: window_only - the windowless host has no such operation or no "
             "selection/view/clock, do not compare its answer there; the host still runs the line, for what it does to the scene is "
             "what later lines read; filled - keys the window fills in by itself from its selection or view (a replay supplies them: "
             "ids from the selection_set lines, a light's at and a camera's eye from the scene_get that follows), the host does not, "
             "so the answer differs by design; "
             "by_design - why the host's answer will differ in the text; settle - the request is repeated (up to 5 s) until "
             "the answer equals this one, because the window learns of a refusal a frame later and its first reading is a transient; "
             "first_reading - what the first reading said when that was not the settled answer (a race: never compared); sleep_s - wait this long before "
             "sending (the 700 ms typing gate of the window's history; the host has no gate, skip the wait); write_file - not a request: "
             "write this text to the path before the next lines; raw/reply - a line sent as is (bytes between the quotes, \"\" is a "
             "blank line) and the line received as is, null when there is none (expect_no_reply); in a raw line the reply has "
             "no \"id\" key when the request had none. kept_raw - args of a wrong shape, sent as they are. Numbers are what the "
             "window printed, parsed (so 1e-05 is 1e-05, and a whole number is whole).",
    "placeholder": {"{DIR}": "the folder files are written to and read from: replaced by the real folder in requests (and in write_file), and "
                              "the real folder is replaced by {DIR} in every answer and reply; the folder itself is a temporary one"},
    "masks": {"anywhere": list(MASKED_ANYWHERE) + [],
              "meaning": "the key and its whole value are replaced by \"<masked>\" at any depth: step_ms (sim_state, sim_step: wall-clock "
                         "seconds of the step's parts) and frame_timing (sim_state: the renderer's and the wall clock's bookkeeping); a line's own "
                         "\"mask\" lists top-level answer keys masked in that line only (the operating system's text of a file error). "
                         "Nothing else is masked; the recording was repeated and compared without the masks to find these."},
    "plugins": "The window starts the plugins shipped in plugins/ a moment after it opens (a Python process registers its commands: "
               "urdf_load, gltf_load). The recorder waits until they are up, and leaves their commands out of ops-list.json: the fixture is the "
               "window's own operations, the same whenever the plugin registers. The throwaway config (XDG_CONFIG_HOME, XDG_DATA_HOME) "
               "keeps the person's own plugins and language out; a window nobody watches keeps its other settings to itself and opens "
               "on the floor alone.",
}


def read_transcript(path):
    with open(path, encoding="utf-8") as f:
        lines = [json.loads(x) for x in f if x.strip()]
    return lines[0], lines[1:]


def entries_of(lines):
    """The committed lines as entries for replay (the same shape the TRANSCRIPT has)."""
    out = []
    for line in lines:
        e = {"note": line["note"]}
        if "write_file" in line:
            e["write_file"] = line["write_file"]
        elif "raw" in line:
            e.update(raw=line["raw"], expect_no_reply=line.get("expect_no_reply", False))
        else:
            e.update(op=line["request"]["op"], args=line["request"]["args"], mask=line.get("mask"), settle=line.get("settle"),
                     sleep_s=line.get("sleep_s", 0), kept_raw=line.get("kept_raw", False))
        out.append(e)
    return out


def ops_list_text(answer):
    """Sorted keys, one operation per line: a diff of two recordings reads."""
    rows = [json.dumps(op, ensure_ascii=False, sort_keys=True) for op in answer["ops"]]
    return '{"ok": true, "ops": [\n' + ",\n".join(rows) + "\n]}\n"


def record(out_dir, port):
    window = Window(port)
    try:
        lines = play(window, TRANSCRIPT)
        answer = window.call("ops_list")
        answer["ops"] = [o for o in answer["ops"] if o["name"] not in window.plugin_commands]
        plugins = window.plugin_commands
    finally:
        window.finish()
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "window-transcript.jsonl"), "w", encoding="utf-8") as f:
        f.write(json.dumps(ABOUT, ensure_ascii=False) + "\n")
        for line in lines:
            f.write(json.dumps(line, ensure_ascii=False) + "\n")
    with open(os.path.join(out_dir, "ops-list.json"), "w", encoding="utf-8") as f:
        f.write(ops_list_text(answer))
    print(f"recorded {len(lines)} lines and {len(answer['ops'])} operations (left out as the plugins' own: {', '.join(plugins) or 'none'})")
    return 0


def check(out_dir, port):
    about, recorded = read_transcript(os.path.join(out_dir, "window-transcript.jsonl"))
    if about != ABOUT or len(recorded) != len(TRANSCRIPT) or any(
            was["note"] != entry["note"] or was.get("mask") != entry.get("mask") for was, entry in zip(recorded, TRANSCRIPT)):
        print("the committed file is not the one this script's list records: its notes, masks or first line differ; record again")
        return MAX_EXIT
    window = Window(port)
    try:
        fresh = play(window, entries_of(recorded))
        answer = window.call("ops_list")
        answer["ops"] = [o for o in answer["ops"] if o["name"] not in window.plugin_commands]
    finally:
        window.finish()
    bad = 0
    assert len(recorded) == len(fresh)
    for was, now in zip(recorded, fresh):
        a = {k: v for k, v in was.items() if k in ("answer", "reply")}
        b = {k: v for k, v in now.items() if k in ("answer", "reply")}
        if a == b:
            continue
        bad += 1
        print(f"--- line {was['n']}: {was['note']}")
        print("request :", json.dumps(was.get("request", was.get("raw")), ensure_ascii=False))
        print("recorded:", json.dumps(a, ensure_ascii=False))
        print("now     :", json.dumps(b, ensure_ascii=False))
        print("differ  :", ", ".join(differing_keys(a, b)))
    with open(os.path.join(out_dir, "ops-list.json"), encoding="utf-8") as f:
        text = f.read()
    if text != ops_list_text(answer):
        bad += 1
        was = {o["name"]: o for o in json.loads(text)["ops"]}
        now = {o["name"]: o for o in answer["ops"]}
        print("--- ops_list differs")
        for name in sorted(set(was) | set(now)):
            if was.get(name) != now.get(name):
                print(" ", name, differing_keys(was.get(name, "<absent>"), now.get(name, "<absent>")))
    print(f"{len(recorded)} lines replayed, {bad} differ")
    return min(bad, MAX_EXIT)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--port", type=int, default=0)
    parser.add_argument("--out", default=DATA)
    args = parser.parse_args()
    return (check if args.check else record)(args.out, args.port)


if __name__ == "__main__":
    sys.exit(main())
