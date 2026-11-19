#!/usr/bin/env python3
"""An MCP server (stdio, JSON-RPC 2.0) that lets an agent drive the Kinetica editor.

Every operation of the editor's API (editor/EditorOps.h, the same ones its buttons and magnet call)
becomes a tool, listed from the running editor; three tools are always there:

* ``editor_launch``  starts build/kinetica --api (optionally with a scene file) and waits;
* ``editor_call``    any operation by name with its arguments - works even when the list was read
                     before the editor ran.

Each operation's arguments come as its JSON Schema (ops_list: a plugin's fields, the core's line of arguments read):
a call with a field the operation has not is refused, naming the fields. An answer ``{"ok": false}`` is an MCP error
(``isError``). When the operations change - a plugin started, fell or was saved - the server says so
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
    {"name": "editor_call", "description": "Call any editor operation by name (editor_ops lists them) with its arguments.",
     "inputSchema": {"type": "object", "properties": {"op": {"type": "string"}, "args": OPEN}, "required": ["op"]}},
]

_editor = None
_names = None  # the operations' names at the last list: a change is said to the client
# What a model reads first (MCP initialize: instructions): where it is and the first calls that pay off.
INSTRUCTIONS = (
    "Kinetica Editor: a robotics editor - bodies, joints, motors, imperfect sensors - on the Box3D physics engine. Every "
    "button of its window is one of these tools, and the person watching sees what you do. A fresh editor opens with a "
    "UR5e arm on a table (scene_new: an empty floor). Start with scene_describe (the scene in words, ids in #) or "
    "scene_list. Build: scene_add a shape, assembly_attach a link to a face (or assembly_joint), assembly_drive a motor. "
    "Run: sim_play, or sim_step frames on pause, then sim_state. Look: view_frame an object, then view_screenshot. Every "
    "edit is one undo step (edit_undo answers what it changed). A wrong field is refused with the fields the tool has. "
    "Your own tool: plugin_new {name} writes a one-file Python plugin whose commands become tools here "
    "(docs/plugins.md). Units SI; angles in rad, except Euler degrees in the scene's rotation lines.")
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
    tools = [{"name": op["name"], "description": op["help"], "inputSchema": op.get("schema", OPEN)}
             for op in editor().ops_list()["ops"] if op["name"] not in WIRING]
    _names = {t["name"] for t in tools}
    return tools


def tools_changed():
    """Whether the editor's operations are no longer those of the last list (a plugin came, went or fell)."""
    if _names is None or not ke_editor.reachable():
        return False
    return {op["name"] for op in editor().ops_list()["ops"] if op["name"] not in WIRING} != _names


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
        return {"protocolVersion": params.get("protocolVersion", PROTOCOL), "capabilities": {"tools": {"listChanged": True}}, "instructions": INSTRUCTIONS,
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
