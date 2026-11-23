#!/usr/bin/env python3
"""An MCP server (stdio, JSON-RPC 2.0) that lets an agent drive the Kinetica editor.

Every operation of the editor's API (editor/EditorOps.h, the same ones its buttons and magnet call)
becomes a tool, listed from the running editor; three tools are always there:

* ``editor_launch``  starts build/kinetica --api (optionally with a scene file) and waits;
* ``editor_call``    any operation by name with its arguments - works even when the list was read
                     before the editor ran.

Each operation's arguments come as its JSON Schema (ops_list: a plugin's fields, the core's line of arguments read):
a call with a field the operation has not is refused, naming the fields. An answer ``{"ok": false}`` is an MCP error
(``isError``). Each tool carries MCP's annotations from the operation's kind, as the editor says it (ops_list): a query
is read-only; an edit of the scene and a change of the window's state are not destructive - an edit is one undo step;
an action (a file written, the program stopped, a plugin's command) is left as it may be.

The profile, by default "agent": the hyper-operations first - a ready assembly in one call, terrain in one call, a whole
robot in one scene_batch - and the few operations a build is checked with (AGENT); measured on a weak model (Haiku), the
full list of 150 tools - some 25 000 tokens a turn - and learning the file format cost it about 18 calls a task.
``KE_MCP_FAMILIES=all`` offers every operation, ``KE_MCP_FAMILIES=scene,assembly,sim`` those of the families (the first
word of a name). The rest stay one call away:
``editor_call`` reaches any operation, and ``ops_list {families: [...]}`` describes them. When the operations change - a plugin started, fell or was saved - the server says so
(``notifications/tools/list_changed``). The plugins' own wiring (plugin_register, plugin_wait, plugin_reply,
plugin_panel_set: a plugin process with its token) is not offered as tools. ``view_screenshot`` answers with the
picture itself, so the agent sees what the editor shows.
Registered for Claude Code in the repository's .mcp.json as "ke-editor"; the port is KE_EDITOR_PORT (else 47801);
only the Python standard library::

    python3 tools/ke_editor_mcp.py            # speaks MCP on stdin / stdout
"""
import base64
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ke_editor  # noqa: E402

PROTOCOL = "2025-06-18"
OPEN = {"type": "object", "additionalProperties": True}
LOCAL_TOOLS = [
    {"name": "editor_launch", "description": "Start the editor with its API (if it is not running) and wait for it. "
     "scene: a .kscene file to open.",
     "inputSchema": {"type": "object", "properties": {"scene": {"type": "string"}}}},
    {"name": "editor_call", "description": "Call any editor operation by name with its arguments - also one that is not a tool here. "
     "The operation ops_list names them all with their arguments ({families: [\"scene\"]}: one family).",
     "inputSchema": {"type": "object", "properties": {"op": {"type": "string"}, "args": OPEN}, "required": ["op"]}},
]
FAMILIES = [f for f in os.environ.get("KE_MCP_FAMILIES", "agent").replace(" ", "").split(",") if f]  # agent, all, or families
AGENT = {"assembly_list", "assembly_add", "assembly_save", "assembly_joint", "assembly_drive", "assembly_power", "terrain_make", "scene_batch",
         "scene_get", "scene_list", "scene_new", "scene_delete", "scene_set", "scene_put", "sim_play", "sim_step", "sim_measure", "sim_stop",
         "controller_input", "view_screenshot", "view_follow", "ops_list"}
# An operation's kind (ops_list) as MCP's annotations of its tool.
HINTS = {"query": {"readOnlyHint": True}, "edit": {"readOnlyHint": False, "destructiveHint": False},
         "window": {"readOnlyHint": False, "destructiveHint": False}, "action": {"readOnlyHint": False}}

_editor = None
_names = None  # the operations' names at the last list: a change is said to the client
# What a model reads first (MCP initialize: instructions): where it is and the first calls that pay off.
INSTRUCTIONS = (
    "Kinetica Editor: a robotics editor on the Box3D physics engine; the person watching sees every call. Axes: x forward, "
    "y up (the floor at y 0), z right; SI units; angles in rad, Euler degrees only in the scene text's rotation lines; a "
    "shape's position is its centre, its size its full extent (a sphere's: its diameter).\n"
    "Work top-down - each call is a turn of yours, the fewest win:\n"
    "1. Ready: assembly_list, then assembly_add {name, position} - a whole robot in one call (Спорткар: chassis, shocks, "
    "steering, DC motors on a battery, an IMU, a driving panel). Its parts change in the same call: overrides: [{name: part, line, "
    "key, values}] - e.g. {name: \"Батарея 1\", line: cells, key: parallel, values: [2]}.\n"
    "2. Ground: terrain_make {size, height, hills, flat, seed, track} - static hills, flat in the middle; track: true adds "
    "a hump, speed bumps, crates and balls.\n"
    "3. Anything new: ONE scene_batch {ops: [{op, args}, ...]} - \"$n\" is the id step n (from 0) made, \"$n.key\" that key "
    "of its answer. E.g. [{op: scene_add, args: {shape: box, position: [0, 0.3, 0], size: [1, 0.05, 0.4], role: rigid}}, "
    "{op: scene_add, args: {shape: cylinder, position: [-0.35, 0.1, 0.3], size: [0.2, 0.06, 0.2], role: rigid}}, "
    "{op: scene_turn, args: {ids: [\"$1\"], axis: x, degrees: 90}}, {op: assembly_joint, args: {a: \"$1\", b: \"$0\", type: hinge, "
    "anchor: [-0.35, 0.1, 0.27], axis: [0, 0, 1]}}, {op: assembly_drive, args: {joint: \"$3\", kind: dcmotor, voltage: -11.1, "
    "resistance: 0.13, constant: 0.29}}, {op: assembly_power, args: {joint: \"$3\"}}].\n"
    "4. A copy of a part: scene_get gives its blocks; assembly_add {text} puts blocks with ids of their own in (renumbered). "
    "One new block: scene_put {block} (id 0: a new object). A car's driving panel, CHASSIS its chassis' id: 'controller \"Пульт\"\\n"
    "  action steer axis drive value target CHASSIS speed 1.5\\n  action throttle axis drive value target CHASSIS speed 2\\n"
    "  bind steer keys Right Left\\n  bind steer gamepad right_x\\n  bind throttle keys Up Down\\n  bind throttle gamepad left_y\\nend'.\n"
    "5. Check once: sim_play, sim_step {frames: 120, state: false}, sim_measure {objects: [id]} (centre y, speed), sim_stop; "
    "view_screenshot to look. Anything else: editor_call {op, args}; ops_list {families: [...]} describes it.\n"
    "Learned the hard way: joint types hinge (about axis), slider, ball, weld, shock (spring and damper along axis), cylinder, "
    "universal, planar - anchor and axis in the world. A DC motor first (assembly_drive kind dcmotor), then assembly_power; a "
    "driver starts at duty 0. A cylinder's axis is its y: a wheel on an axle along z is turned x 90°. A car's panel: actions "
    "with drive value (a plugin reads them, the brain's stabiliser drives the car); keys by Qt's names (Up Down Left Right W "
    "Space), pad left_y right_x. Every block of text ends with 'end'. A light pushable body: scene_set {id, line: rigid, key: "
    "density, values: [150]}. While the scene plays, an edit lasts until sim_stop.")
WIRING = {"plugin_register", "plugin_wait", "plugin_reply", "plugin_panel_set"}  # a plugin process's own, with its token


def editor():
    global _editor
    if _editor is None:
        _editor = ke_editor.Editor()
    return _editor


def remote_tools():
    """The running editor's operations as tools (none when no editor answers)."""
    if not ke_editor.reachable():
        return []
    global _names
    tools = [{"name": op["name"], "description": op["help"], "inputSchema": op.get("schema", OPEN), "annotations": HINTS.get(op.get("kind"), {})}
             for op in listed() if op["name"] not in WIRING]
    _names = {t["name"] for t in tools}
    return tools


def listed():
    """The editor's operations this server offers as tools: the agent's, all of them, or the profile's families."""
    if FAMILIES == ["agent"]:
        return [op for op in editor().ops_list()["ops"] if op["name"] in AGENT]
    return editor().ops_list(**({"families": FAMILIES} if FAMILIES != ["all"] else {}))["ops"]


def tools_changed():
    """Whether the editor's operations are no longer those of the last list (a plugin came, went or fell)."""
    if _names is None or not ke_editor.reachable():
        return False
    return {op["name"] for op in listed() if op["name"] not in WIRING} != _names


def content(answer, op):
    """The answer as MCP content: its JSON, and a screenshot's picture as an image."""
    parts = [{"type": "text", "text": json.dumps(answer, ensure_ascii=False)}]
    if op == "view_screenshot" and answer.get("ok") and os.path.exists(answer.get("file", "")):
        with open(answer["file"], "rb") as f:
            parts.append({"type": "image", "data": base64.b64encode(f.read()).decode("ascii"), "mimeType": "image/png"})
    return parts


def call_tool(name, args):
    global _editor
    if name == "editor_launch":
        ke_editor.launch(scene=args.get("scene"))
        _editor = None
        return content({"ok": True, "port": ke_editor.DEFAULT_PORT}, name), False
    op, op_args = (args.get("op", ""), args.get("args", {})) if name == "editor_call" else (name, args)
    try:
        return content(editor().call(op, **op_args), op), False
    except ke_editor.EditorError as e:
        return content({"ok": False, "error": str(e)}, op), True


def handle(msg):
    method, params = msg.get("method"), msg.get("params", {})
    if method == "initialize":
        profile = (" Only the families " + ", ".join(FAMILIES) + " are tools here; any other operation: editor_call (ops_list names them)."
                   if FAMILIES not in (["agent"], ["all"]) else "")
        return {"protocolVersion": params.get("protocolVersion", PROTOCOL), "capabilities": {"tools": {"listChanged": True}}, "instructions": INSTRUCTIONS + profile,
                "serverInfo": {"name": "ke-editor", "version": "1"}}
    if method == "tools/list":
        return {"tools": LOCAL_TOOLS + remote_tools()}
    if method == "tools/call":
        try:
            parts, failed = call_tool(params["name"], params.get("arguments") or {})
            return {"content": parts, "isError": failed}
        except (OSError, RuntimeError) as e:
            global _editor
            _editor = None
            why = (f"the editor is not reachable: {e}. An editor started for an agent (--agent-eval) quits after 5 minutes "
                   "without a call (runtime_status: its lease; runtime_configure idle_timeout_ms: longer). editor_launch starts one")
            return {"content": [{"type": "text", "text": why}], "isError": True}
    if method == "ping":
        return {}
    raise KeyError(method)


def main():
    for line in sys.stdin:
        if not line.strip():
            continue
        msg = json.loads(line)
        if "id" not in msg:
            continue  # a notification (initialized, cancelled): nothing to answer
        try:
            reply = {"jsonrpc": "2.0", "id": msg["id"], "result": handle(msg)}
        except KeyError as e:
            reply = {"jsonrpc": "2.0", "id": msg["id"], "error": {"code": -32601, "message": f"unknown method {e}"}}
        if msg.get("method") == "tools/call" and tools_changed():  # said before the answer: the client lists again
            sys.stdout.write(json.dumps({"jsonrpc": "2.0", "method": "notifications/tools/list_changed"}) + "\n")
        sys.stdout.write(json.dumps(reply, ensure_ascii=False) + "\n")
        sys.stdout.flush()


if __name__ == "__main__":
    main()
