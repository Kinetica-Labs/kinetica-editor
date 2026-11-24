#!/usr/bin/env python3
"""An MCP server (stdio, JSON-RPC 2.0) that lets an agent drive the Kinetica editor.

Every operation of the editor's API (editor/EditorOps.h, the same ones its buttons and magnet call)
becomes a tool, listed from the running editor; three tools are always there:

* ``editor_launch``  starts build/kinetica --api (optionally with a scene file) and waits;
* ``editor_call``    any operation by name with its arguments - works even when the list was read
                     before the editor ran;
* ``guide``          a topic of the guide (docs/agent/<topic>.md): a car, an arm, power.

Each operation's arguments come as its JSON Schema (ops_list: a plugin's fields, the core's line of arguments read):
a call with a field the operation has not is refused, naming the fields. An answer ``{"ok": false}`` is an MCP error
(``isError``). Each tool carries MCP's annotations from the operation's kind, as the editor says it (ops_list): a query
is read-only; an edit of the scene and a change of the window's state are not destructive - an edit is one undo step;
an action (a file written, the program stopped, a plugin's command) is left as it may be.

The profile, by default "agent": the hyper-operations first - a ready assembly in one call, terrain in one call, a whole
robot in one scene_batch - and the few operations a build is checked with (AGENT); measured on a weak model (Haiku), the
full list of 150 tools - some 25 000 tokens a turn - and learning the file format cost it about 18 calls a task; the
instructions are docs/agent-guide.md, the lessons of those runs, short enough that no client cuts them; the tool ``guide``
gives a topic of docs/agent/ (a car, an arm, power) when the model builds that kind of robot.
``KE_MCP_FAMILIES=all`` offers every operation, ``KE_MCP_FAMILIES=scene,assembly,sim`` those of the families (the first
word of a name). The rest stay one call away:
``editor_call`` reaches any operation, and ``ops_list {families: [...]}`` describes them. When the operations change - a plugin started, fell or was saved - the server says so
(``notifications/tools/list_changed``). The plugins' own wiring (plugin_register, plugin_wait, plugin_reply,
plugin_panel_set: a plugin process with its token) is not offered as tools. ``view_screenshot`` answers with the
picture itself, so the agent sees what the editor shows.
For filming (KE_MCP_TYPING=1): before each call the editor's console types it into its line (console_type), so the
viewer sees the call written, then run.
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
# The guide's topics (docs/agent/<topic>.md): what a model reads when it builds that kind of robot - the instructions
# stay short, as a client cuts them (Claude Code: at about 4 000 characters), and the rest is disclosed on asking.
# KE_AGENT_GUIDE: another folder with agent-guide.md and agent/ (a translation measured on the bench, tools/agent_bench.py --guide).
DOCS = os.environ.get("KE_AGENT_GUIDE") or os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "docs")
TOPICS = os.path.join(DOCS, "agent")
TYPING = os.environ.get("KE_MCP_TYPING") == "1"
UNTYPED = {"narration_say", "console_note", "view_screenshot"}  # seen in the view or as a comment, or the agent's own look


def topics():
    """{topic: its title} of the guide's topics."""
    found = {}
    for name in sorted(os.listdir(TOPICS)) if os.path.isdir(TOPICS) else []:
        if name.endswith(".md"):
            with open(os.path.join(TOPICS, name), encoding="utf-8") as f:
                found[name[:-3]] = f.readline().lstrip("# ").strip()
    return found


LOCAL_TOOLS.append({"name": "guide", "description": "A topic of the guide to building with this editor - read yours before building: "
                    + "; ".join(f"{k} - {v}" for k, v in topics().items()),
                    "inputSchema": {"type": "object", "properties": {"topic": {"type": "string", "enum": sorted(topics())}}, "required": ["topic"]},
                    "annotations": {"readOnlyHint": True}})
FAMILIES = [f for f in os.environ.get("KE_MCP_FAMILIES", "agent").replace(" ", "").split(",") if f]  # agent, all, or families
AGENT = {"assembly_list", "assembly_add", "assembly_save", "assembly_joint", "assembly_actuator", "assembly_drive", "assembly_power", "assembly_sensor",
         "assembly_goal", "controller_add", "terrain_make", "scene_batch",
         "scene_get", "scene_list", "scene_new", "scene_delete", "scene_set", "scene_put", "sim_play", "sim_step", "sim_measure", "sim_stop",
         "controller_input", "view_screenshot", "view_follow", "view_draw", "formula_callout", "narration_say", "scene_add", "ops_list"}
# An operation's kind (ops_list) as MCP's annotations of its tool.
HINTS = {"query": {"readOnlyHint": True}, "edit": {"readOnlyHint": False, "destructiveHint": False},
         "window": {"readOnlyHint": False, "destructiveHint": False}, "action": {"readOnlyHint": False}}

_editor = None
_names = None  # the operations' names at the last list: a change is said to the client
# What a model reads first (MCP initialize: instructions): docs/agent-guide.md - the distilled way to build fast, one
# source for people and models.
GUIDE = os.path.join(DOCS, "agent-guide.md")
INSTRUCTIONS = (open(GUIDE, encoding="utf-8").read() if os.path.exists(GUIDE) else
                "Kinetica Editor: a robotics editor; start with assembly_list, terrain_make and scene_batch.")
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
    if name == "guide":
        topic = args.get("topic", "")
        if topic not in topics():
            return content({"ok": False, "error": f"no topic «{topic}»; there are: {', '.join(sorted(topics()))}"}, name), True
        with open(os.path.join(TOPICS, topic + ".md"), encoding="utf-8") as f:
            return [{"type": "text", "text": f.read()}], False
    if name == "editor_launch":
        ke_editor.launch(scene=args.get("scene"))
        _editor = None
        return content({"ok": True, "port": ke_editor.DEFAULT_PORT}, name), False
    op, op_args = (args.get("op", ""), args.get("args", {})) if name == "editor_call" else (name, args)
    try:
        if TYPING and op not in UNTYPED:
            editor().call("console_type", op=op, args=op_args)
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
