#!/usr/bin/env python3
"""A bench of agents on the editor: tasks x models, each measured and its result checked by the bench itself.

For every task (agent_bench_tasks.json) and model: a hidden editor with the plugins (build/kinetica --agent-eval), a
headless Claude Code (claude -p) whose only tools are the MCP server ke-editor (tools/ke_editor_mcp.py, the profile
given), and the task's words. Measured from the run's stream: the wall time, the tool calls, the refused ones, the
model's turns, its tokens and its cost; asked at the end, the agent's own word on what in the tools got in its way. Then
the bench checks what is in the editor - never the agent's report, for any robot: its panel moves what it drives (a
car's chassis, an arm's effector), its joints are held by servos, there is ground, crates and balls are there (CHECKS). One line a run is appended to the bench's journal (JSON lines, never rewritten),
and a table is printed: a tool measured by accuracy, runtime, calls, tokens, errors - and the agent's feedback. Only the
standard library::

    python3 tools/agent_bench.py                                   # every task, haiku
    python3 tools/agent_bench.py --tasks ready_car --models haiku sonnet --repeat 3
    python3 tools/agent_bench.py --table                           # the journal as a table, nothing run
"""
import argparse
import datetime
import json
import os
import socket
import subprocess
import sys
import tempfile
import time

TOOLS = os.path.dirname(os.path.abspath(__file__))
EDITOR = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)
import ke_editor  # noqa: E402
from kinetica.client import data_folder  # noqa: E402

FEEDBACK = ("\n\nВ самом конце отчёта отдельной строкой, начиная с «ОТЗЫВ:», одной-двумя фразами: что в инструментах "
            "редактора мешало или было непонятно (или «ничего»).")


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def metrics(lines):
    """What a run's stream (claude -p --output-format stream-json) says: its calls, refusals, turns, tokens, cost."""
    out = {"calls": 0, "errors": 0, "turns": 0, "input_tokens": 0, "output_tokens": 0, "cost_usd": 0.0, "report": "", "ops": {}}
    for line in lines:
        try:
            d = json.loads(line)
        except ValueError:
            continue
        if d.get("type") == "assistant":
            for c in d["message"].get("content", []):
                if c.get("type") == "tool_use":
                    out["calls"] += 1
                    op = c["name"].split("__")[-1]
                    out["ops"][op] = out["ops"].get(op, 0) + 1
        elif d.get("type") == "user":
            for c in d["message"].get("content", []):
                if isinstance(c, dict) and c.get("type") == "tool_result":
                    text = json.dumps(c.get("content"), ensure_ascii=False)
                    if c.get("is_error") or '\\"ok\\": false' in text:
                        out["errors"] += 1
        elif d.get("type") == "result":
            u = d.get("usage", {})
            out["turns"] = d.get("num_turns", 0)
            out["input_tokens"] = u.get("input_tokens", 0) + u.get("cache_read_input_tokens", 0) + u.get("cache_creation_input_tokens", 0)
            out["output_tokens"] = u.get("output_tokens", 0)
            out["cost_usd"] = d.get("total_cost_usd", 0.0)
            out["report"] = d.get("result", "")
    report = out["report"]
    out["feedback"] = report[report.rfind("ОТЗЫВ:") + 6:].strip() if "ОТЗЫВ:" in report else ""
    return out


# ---- the checks: what the bench itself finds in the editor once the agent is done --------------------------------

def _objects(ed):
    return ed.scene_list()["objects"]


def _block(ed, id):
    return ed.scene_get(id=id)["text"]


def _moved(ed, target):
    """The body a panel's action moves: an IK goal's effector (an arm's), a motor driver's vehicle - the body its wheel's
    joint holds on (a duty action: the throttle on the drivers), else the target itself (a car's chassis)."""
    lines = [line.split() for line in _block(ed, target).splitlines()]
    goal = next((w for w in lines if w[:2] == ["goal", "effector"]), None)
    if goal:
        return int(goal[2])
    bridge = next((w for w in lines if w[:2] == ["bridge", "joint"]), None)
    if bridge:
        link = next(w for w in (line.split() for line in _block(ed, int(bridge[2])).splitlines()) if w[:1] == ["link"])
        return int(link[link.index("b") + 1])
    return target


def _centre(ed, body):
    m = {x["id"].split("/", 1)[1]: x["value"] for x in ed.sim_measure(objects=[body])["channels"]}
    return [m.get("centre " + a, 0.0) for a in "xyz"]


def check_panel(ed):
    """The scene's panel moves what it drives, whatever the robot: its actions in turn (a vector2 at [0.5, 0], an axis
    at 0.5; buttons and grips left out), each held 2 s in a fresh run, until one moves its body - a car's chassis, an
    arm's effector - more than 5 cm, the body staying above the floor."""
    for c in ed.scene_list(kind="controller")["objects"]:
        for w in (line.split() for line in _block(ed, c["id"]).splitlines()):
            if w[:1] != ["action"] or "target" not in w or w[2] == "button" or w[w.index("drive") + 1:][:1] == ["grip"]:
                continue
            body = _moved(ed, int(w[w.index("target") + 1]))
            ed.sim_stop()
            ed.sim_play()
            time.sleep(1.5)  # a car's stabiliser hears its gyroscope's zero first; an arm settles on its servos
            before = _centre(ed, body)
            ed.controller_input(id=c["id"], values={w[1]: [0.5, 0] if w[2] == "vector2" else 0.5})
            time.sleep(2.0)
            after = _centre(ed, body)
            ed.sim_stop()
            moved = sum((a - b) ** 2 for a, b in zip(after, before)) ** 0.5
            if moved > 0.05 and after[1] > 0.02:
                return True, f"пульт {c['id']}: {w[1]} сдвинул тело {body} на {moved:.2f} м за 2 с"
    return False, "ни одно действие пульта не сдвинуло своё тело на 5 см (или пульта нет)"


def check_servos(ed, count=3):
    """A robot of its own parts: at least `count` joints held by servos (a drive line with stiffness)."""
    held = sum(any(w[:1] == ["drive"] and "stiffness" in w and float(w[w.index("stiffness") + 1]) > 0
                   for w in (line.split() for line in _block(ed, j["id"]).splitlines()))
               for j in ed.scene_list(kind="joint")["objects"])
    return held >= count, f"суставов с сервоприводом {held}"


def check_ground(ed):
    """Ground to drive on: a static mesh (shape mesh, no rigid line) or five static colliders and more."""
    meshes = statics = 0
    for o in _objects(ed):
        if o.get("kind") != "entity":
            continue
        text = _block(ed, o["id"])
        static = "\n  rigid " not in text
        meshes += static and "shape mesh" in text
        statics += static and o.get("name") != "Пол"
    return meshes > 0 or statics >= 5, f"сеток {meshes}, неподвижных {statics}"


def check_bodies(ed, crates=5, balls=3):
    """Pushable bodies: at least `crates` boxes and `balls` spheres, rigid, small (≤ 0.4 m) and free - a part of a robot
    (a body a joint holds: an arm's gripper) is not one."""
    held = {int(w[k + 1]) for j in ed.scene_list(kind="joint")["objects"] for w in (line.split() for line in _block(ed, j["id"]).splitlines())
            if w[:1] == ["link"] for k in (w.index(x) for x in ("a", "b") if x in w) if k + 1 < len(w)}
    found = {"box": 0, "sphere": 0}
    for o in _objects(ed):
        if o.get("kind") != "entity" or o["id"] in held:
            continue
        text = _block(ed, o["id"])
        shape = next((w for w in (line.split() for line in text.splitlines()) if w[:1] == ["shape"]), [])
        if len(shape) > 1 and shape[1] in found and "\n  rigid " in text and "size" in shape:
            if max(float(v) for v in shape[shape.index("size") + 1:shape.index("size") + 4]) <= 0.4:
                found[shape[1]] += 1
    return found["box"] >= crates and found["sphere"] >= balls, f"ящиков {found['box']}, мячей {found['sphere']}"


CHECKS = {"panel": check_panel, "servos": check_servos, "ground": check_ground, "bodies": check_bodies}


# ---- a run ---------------------------------------------------------------------------------------------------------

def run(task, model, profile, plugins, timeout, guide=None):
    port = free_port()
    env = dict(os.environ, KE_PLUGINS=plugins) if plugins else dict(os.environ)
    editor = subprocess.Popen([os.path.join(EDITOR, "build", "kinetica"), "--agent-eval", "--api", str(port)], cwd=EDITOR, env=env,
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
    try:
        for _ in range(300):
            if ke_editor.reachable(port):
                break
            time.sleep(0.1)
        ed = ke_editor.Editor(port=port)
        ed.scene_new()
        with tempfile.TemporaryDirectory() as cwd:  # nothing of the repository's to read: the tools are all it has
            config = os.path.join(cwd, "mcp.json")
            mcp_env = {"KE_EDITOR_PORT": str(port)}
            if profile != "agent":
                mcp_env["KE_MCP_FAMILIES"] = profile
            if guide:
                mcp_env["KE_AGENT_GUIDE"] = os.path.abspath(guide)
            with open(config, "w") as f:
                json.dump({"mcpServers": {"ke-editor": {"command": sys.executable, "args": [os.path.join(TOOLS, "ke_editor_mcp.py")], "env": mcp_env}}}, f)
            start = time.monotonic()
            done = subprocess.run(["claude", "-p", task["text"] + FEEDBACK, "--model", model, "--tools", "", "--strict-mcp-config",
                                   "--mcp-config", config, "--allowedTools", "mcp__ke-editor", "--setting-sources", "",
                                   "--output-format", "stream-json", "--verbose", "--no-session-persistence"],
                                  cwd=cwd, capture_output=True, text=True, timeout=timeout)
            wall = time.monotonic() - start
        out = metrics(done.stdout.splitlines())
        out.update(task=task["id"], model=model, profile=profile + (f"+{os.path.basename(os.path.abspath(guide))}" if guide else ""), wall_s=round(wall, 1), checks={})
        for spec in task["checks"]:  # a check's name, or [its name, its arguments]
            name, kw = (spec, {}) if isinstance(spec, str) else spec
            try:
                ok, why = CHECKS[name](ed, **kw)
            except Exception as e:  # a check that cannot read the scene fails, and says why
                ok, why = False, f"{type(e).__name__}: {e}"
            out["checks"][name] = {"ok": bool(ok), "why": why}
        out["passed"] = all(c["ok"] for c in out["checks"].values())
        try:
            ed.app_quit()
        except Exception:
            pass
        return out
    finally:
        try:
            editor.wait(timeout=10)
        except subprocess.TimeoutExpired:
            editor.kill()


def table(rows):
    """The runs as a Markdown table, newest last."""
    head = "| задание | модель | профиль | итог | время, с | вызовы | ошибки | ходы | токены вход/выход | цена, $ | отзыв агента |"
    lines = [head, "|" + "---|" * 11]
    for r in rows:
        checks = " ".join(("✓" if c["ok"] else "✗") + k for k, c in r.get("checks", {}).items())
        lines.append(f"| {r['task']} | {r['model']} | {r.get('profile', 'agent')} | {checks} | {r['wall_s']} | {r['calls']} | {r['errors']} | "
                     f"{r['turns']} | {r['input_tokens'] // 1000}k/{r['output_tokens'] // 1000}k | {r['cost_usd']:.4f} | "
                     f"{r.get('feedback', '')[:80].replace('|', '/')} |")
    return "\n".join(lines)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("--tasks", nargs="*", help="the tasks' ids (none: all)")
    p.add_argument("--models", nargs="*", default=["haiku"])
    p.add_argument("--profile", default="agent", help="the MCP profile: agent, all, or families")
    p.add_argument("--repeat", type=int, default=1)
    p.add_argument("--guide", default=None, help="a folder with agent-guide.md and agent/ in place of docs/ (a translation); its name joins the profile")
    p.add_argument("--plugins", default=os.path.join(os.path.dirname(EDITOR), "kinetica-plugins"), help="a plugins folder (KE_PLUGINS)")
    p.add_argument("--timeout", type=int, default=900, help="s an agent may take")
    p.add_argument("--journal", default=None, help="the journal (JSON lines); default: <user data>/agent-bench.jsonl")
    p.add_argument("--table", action="store_true", help="print the journal as a table and stop")
    a = p.parse_args(argv)
    journal = a.journal or os.path.join(data_folder(), "agent-bench.jsonl")
    if a.table:
        rows = [json.loads(line) for line in open(journal, encoding="utf-8")] if os.path.exists(journal) else []
        print(table(rows))
        return 0
    with open(os.path.join(TOOLS, "agent_bench_tasks.json"), encoding="utf-8") as f:
        tasks = [t for t in json.load(f) if not a.tasks or t["id"] in a.tasks]
    os.makedirs(os.path.dirname(journal), exist_ok=True)
    rows = []
    for task in tasks:
        for model in a.models:
            for _ in range(a.repeat):
                row = run(task, model, a.profile, a.plugins, a.timeout, a.guide)
                row["at"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
                with open(journal, "a", encoding="utf-8") as f:  # appended, never rewritten
                    f.write(json.dumps({k: v for k, v in row.items() if k != "report"}, ensure_ascii=False) + "\n")
                rows.append(row)
                print(table([row]).splitlines()[-1], flush=True)
    print(table(rows))
    return 0 if all(r["passed"] for r in rows) else 1


if __name__ == "__main__":
    sys.exit(main())
