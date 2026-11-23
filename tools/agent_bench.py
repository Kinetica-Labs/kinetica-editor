#!/usr/bin/env python3
"""A bench of agents on the editor: tasks x models, each measured and its result checked by the bench itself.

For every task (agent_bench_tasks.json) and model: a hidden editor with the plugins (build/kinetica --agent-eval), a
headless Claude Code (claude -p) whose only tools are the MCP server ke-editor (tools/ke_editor_mcp.py, the profile
given), and the task's words. Measured from the run's stream: the wall time, the tool calls, the refused ones, the
model's turns, its tokens and its cost; asked at the end, the agent's own word on what in the tools got in its way. Then
the bench checks what is in the editor - never the agent's report: a car stands, its panel drives it, there is ground,
crates and balls are there (CHECKS). One line a run is appended to the bench's journal (JSON lines, never rewritten),
and a table is printed. As Anthropic's «Writing effective tools for agents» measures tools: accuracy, runtime, calls,
tokens, errors - and the agent's feedback. Only the standard library::

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

FEEDBACK = ("\n\nВ самом конце отчёта отдельной строкой, начиная с «ОТЗЫВ:», одной-двумя фразами: что в инструментах "
            "редактора мешало или было непонятно (или «ничего»).")


def _data_folder():
    if os.environ.get("APPDATA"):
        return os.path.join(os.environ["APPDATA"], "Kinetica", "Kinetica")
    mac = os.path.expanduser("~/Library/Application Support")
    base = mac if os.path.isdir(mac) else os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~/.local/share")
    return os.path.join(base, "Kinetica", "Kinetica")


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


def check_drives(ed):
    """A car stands on its wheels and its panel drives it: a controller with value actions steer and throttle on one
    body; played, throttle 0.5 for 2 s moves that body faster than 0.3 m/s, and it stays above the floor."""
    for c in ed.scene_list(kind="controller")["objects"]:
        actions = {w[1]: w for w in (line.split() for line in _block(ed, c["id"]).splitlines()) if w[:1] == ["action"]}
        throttle = actions.get("throttle")
        if not throttle or "value" not in throttle or "target" not in throttle:
            continue
        body = int(throttle[throttle.index("target") + 1])
        ed.sim_stop()
        ed.sim_play()
        time.sleep(1.5)  # the stabiliser hears its gyroscope's zero first
        ed.controller_input(id=c["id"], values={"throttle": 0.5})
        time.sleep(2.0)
        m = {x["id"].split("/", 1)[1]: x["value"] for x in ed.sim_measure(objects=[body])["channels"]}
        ed.sim_stop()
        return m.get("speed", 0) > 0.3 and m.get("centre y", 0) > 0.02, f"панель {c['id']} → тело {body}: {m.get('speed', 0):.2f} м/с"
    return False, "нет пульта с действием throttle (drive value)"


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
    """Pushable bodies: at least `crates` boxes and `balls` spheres that are rigid and small (≤ 0.4 m)."""
    found = {"box": 0, "sphere": 0}
    for o in _objects(ed):
        if o.get("kind") != "entity":
            continue
        text = _block(ed, o["id"])
        shape = next((w for w in (line.split() for line in text.splitlines()) if w[:1] == ["shape"]), [])
        if len(shape) > 1 and shape[1] in found and "\n  rigid " in text and "size" in shape:
            if max(float(v) for v in shape[shape.index("size") + 1:shape.index("size") + 4]) <= 0.4:
                found[shape[1]] += 1
    return found["box"] >= crates and found["sphere"] >= balls, f"ящиков {found['box']}, мячей {found['sphere']}"


CHECKS = {"drives": check_drives, "ground": check_ground, "bodies": check_bodies}


# ---- a run ---------------------------------------------------------------------------------------------------------

def run(task, model, profile, plugins, timeout):
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
            with open(config, "w") as f:
                json.dump({"mcpServers": {"ke-editor": {"command": sys.executable, "args": [os.path.join(TOOLS, "ke_editor_mcp.py")], "env": mcp_env}}}, f)
            start = time.monotonic()
            done = subprocess.run(["claude", "-p", task["text"] + FEEDBACK, "--model", model, "--tools", "", "--strict-mcp-config",
                                   "--mcp-config", config, "--allowedTools", "mcp__ke-editor", "--setting-sources", "",
                                   "--output-format", "stream-json", "--verbose", "--no-session-persistence"],
                                  cwd=cwd, capture_output=True, text=True, timeout=timeout)
            wall = time.monotonic() - start
        out = metrics(done.stdout.splitlines())
        out.update(task=task["id"], model=model, profile=profile, wall_s=round(wall, 1), checks={})
        for name in task["checks"]:
            try:
                ok, why = CHECKS[name](ed)
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
    p.add_argument("--plugins", default=os.path.join(os.path.dirname(EDITOR), "kinetica-plugins"), help="a plugins folder (KE_PLUGINS)")
    p.add_argument("--timeout", type=int, default=900, help="s an agent may take")
    p.add_argument("--journal", default=None, help="the journal (JSON lines); default: <user data>/agent-bench.jsonl")
    p.add_argument("--table", action="store_true", help="print the journal as a table and stop")
    a = p.parse_args(argv)
    journal = a.journal or os.path.join(_data_folder(), "agent-bench.jsonl")
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
                row = run(task, model, a.profile, a.plugins, a.timeout)
                row["at"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
                with open(journal, "a", encoding="utf-8") as f:  # appended, never rewritten
                    f.write(json.dumps({k: v for k, v in row.items() if k != "report"}, ensure_ascii=False) + "\n")
                rows.append(row)
                print(table([row]).splitlines()[-1], flush=True)
    print(table(rows))
    return 0 if all(r["passed"] for r in rows) else 1


if __name__ == "__main__":
    sys.exit(main())
