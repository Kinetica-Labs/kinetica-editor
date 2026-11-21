#!/usr/bin/env python3
"""Record editor-agent experiments with role checks and evidence-based contracts.

Only the running editor is used, through its public client (ke_editor.py): an agent in the role "ui"
may only look and use the mouse and keys, one in the role "api" only the published operations. Every
attempted call - denials too - is a numbered line of the run's journal, and a report is checked against
it. This logger is an evaluation aid, not a security sandbox; source-reading restrictions remain part of
the agent's role contract.
"""
import argparse
import collections
import datetime
import json
from pathlib import Path
import re
import sys
import time


UI_OPS = {"view_screenshot", "ui_pointer", "ui_key", "ui_type"}
OBSERVATIONS = {"ops_list", "sim_state", "sim_step", "sim_measure", "scene_list", "scene_get",
                "scene_describe", "world_get", "view_screenshot"}
SAVE_REPLAY = {"scene_open", "scene_save"}
MAST_KEYS = [f"FM-{group}.{i}" for group, size in ((1, 5), (2, 6), (3, 3))
             for i in range(1, size + 1)]
PHASES = ("plan", "build", "verify", "critique")


def utc_now():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def write_json(path, value):
    Path(path).write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def journal(run):
    path = Path(run) / "journal.jsonl"
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()
            if line.strip()] if path.exists() else []


def initialize(run, role, port, task, budget):
    """Create a run once; never reuse an experiment's directory."""
    run = Path(run).resolve()
    if role not in ("ui", "api") or budget < 1 or not 1 <= port <= 65535 or not task.strip():
        raise ValueError("role, port, task or budget is invalid")
    if run.exists() and any(run.iterdir()):
        raise ValueError("run directory must be empty")
    run.mkdir(parents=True, exist_ok=True)
    manifest = dict(schema_version=1, role=role, port=port, task=task, budget=budget,
                    created_at=utc_now(), transport="editor-tcp", logger_version=1)
    with (run / "manifest.json").open("x", encoding="utf-8") as stream:
        json.dump(manifest, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
    return manifest


def confined_path(run, value):
    """Resolve symlinks as well as '..'; a file must be inside this run."""
    if not isinstance(value, str) or not value:
        raise ValueError("file path is required")
    path = Path(value)
    path = (Path(run) / path).resolve() if not path.is_absolute() else path.resolve()
    if not path.is_relative_to(Path(run).resolve()) or path == Path(run).resolve():
        raise ValueError("file must be inside the run directory")
    return path


def role_problem(role, op, args, run):
    """Return a denial reason before sending anything to the editor."""
    if not isinstance(args, dict):
        return "arguments must be an object"
    if op == "batch" or "batch" in op or any(k in args for k in ("ops", "operations", "commands")):
        return "nested/batch operations would hide individual steps"
    if role == "api":
        if not re.fullmatch(r"[a-z][a-z0-9_]*", op) or op.startswith(("ui_", "console_", "eval_", "exec", "script")):
            return "API agent must use published semantic tools, without UI or code execution shortcuts"
        if op == "view_screenshot":
            try:
                confined_path(run, args.get("file"))
            except ValueError as error:
                return str(error)
        return None
    if op not in UI_OPS:
        return "UI agent may only use screenshots, pointer, keys and typing"
    if op == "view_screenshot":
        if args.get("screen") is not True:
            return "UI screenshots must include the whole window (screen: true)"
        try:
            confined_path(run, args.get("file"))
        except ValueError as error:
            return str(error)
    if op == "ui_pointer" and (args.get("space") != "window" or args.get("cursor", False) is not False):
        return "pointer must use window pixels without moving the desktop cursor"
    if op == "ui_pointer" and args.get("action") not in ("move", "click", "press", "drag", "release", "wheel"):
        return "unknown pointer action"
    if op == "ui_key" and args.get("window") is not True:
        return "keys must go through the editor window (window: true)"
    return None


def editor_client(port):
    """Use the editor's public stdlib client, never an in-process core."""
    from ke_editor import Editor
    return Editor(port=port)


def perform_call(run, op, args, expect, see="", phase="build", client_factory=editor_client):
    """Log every attempted operation, including denials; counters survive CLI exits."""
    run = Path(run).resolve()
    manifest = read_json(run / "manifest.json")
    rows = journal(run)
    if len(rows) >= manifest["budget"]:
        raise ValueError("budget exhausted; no operation sent")
    rec = dict(n=len(rows) + 1, task=manifest["task"], phase=phase, op=op, args=args,
               expect=expect, see=see, started_at=utc_now(), sent=False)
    start = time.perf_counter()
    problem = role_problem(manifest["role"], op, args, run)
    if not expect.strip() or phase not in PHASES:
        problem = "expect and a valid phase are required"
    if manifest["role"] == "ui" and op != "view_screenshot" and not see.strip():
        problem = "UI action needs a description of the observed screen (--see)"
    client = None
    try:
        if problem:
            raise ValueError(problem)
        wire_args = dict(args)
        if op == "view_screenshot":
            wire_args["file"] = str(confined_path(run, args["file"]))
        client = client_factory(manifest["port"])
        rec["sent"] = True
        rec["answer"] = client.call(op, **wire_args)
        rec["ok"] = rec["answer"].get("ok", True) is not False
        if not rec["ok"]:
            rec["error"] = str(rec["answer"].get("error", "editor rejected operation"))
    except Exception as error:
        rec.update(ok=False, error=str(error), denied=bool(problem))
    finally:
        if client is not None:
            try:
                client.close()
            except OSError as error:
                rec.update(ok=False, error=f"closing editor connection: {error}")
        rec.update(ms=round((time.perf_counter() - start) * 1000, 3), finished_at=utc_now())
        with (run / "journal.jsonl").open("a", encoding="utf-8") as stream:
            stream.write(json.dumps(rec, ensure_ascii=False, allow_nan=False) + "\n")
    return rec


class ServingEditor:
    """Lazily borrow one TCP client; per-call close must not close the session."""
    def __init__(self, factory, port):
        self.factory, self.port, self.client = factory, port, None

    def borrow(self, port):
        if port != self.port:
            raise ValueError("run port changed during the session")
        if self.client is None:
            self.client = self.factory(port)
        return self

    def call(self, op, **args):
        return self.client.call(op, **args)

    def close(self):
        pass  # perform_call borrows the connection; serve owns its lifetime

    def finish(self):
        if self.client is not None:
            self.client.close()


def stream_reply(output, response):
    output.write(json.dumps(response, ensure_ascii=False, allow_nan=False) + "\n")
    output.flush()


def invalid_constant(value):
    raise ValueError(f"nonfinite JSON number {value} is not allowed")


def serve_request(line):
    """Decode data only: no shell, scripts, code evaluation or manifest overrides."""
    request = json.loads(line, parse_constant=invalid_constant)
    if not isinstance(request, dict):
        raise ValueError("request must be a JSON object")
    if request.get("quit") is True and len(request) == 1:
        return request
    if set(request) - {"op", "args", "expect", "see", "phase"}:
        raise ValueError("only op, args, expect, see and phase are accepted")
    if not isinstance(request.get("op"), str) or not request["op"]:
        raise ValueError("op must be a nonempty string")
    if not isinstance(request.get("args", {}), dict):
        raise ValueError("args must be a JSON object")
    if not isinstance(request.get("expect"), str) or not request["expect"].strip():
        raise ValueError("expect must be a nonempty string")
    if not isinstance(request.get("see", ""), str) or request.get("phase", "build") not in PHASES:
        raise ValueError("see must be text and phase must be a registered phase")
    return request


def serve(run, input_stream=None, output_stream=None, client_factory=editor_client):
    """Read bounded JSONL requests and retain the same checked editor connection."""
    source, output = input_stream or sys.stdin, output_stream or sys.stdout
    run = Path(run).resolve()
    manifest = read_json(run / "manifest.json")
    session = ServingEditor(client_factory, manifest["port"])
    local_errors = 0
    stream_reply(output, dict(ready=True, role=manifest["role"], task=manifest["task"],
                              budget=manifest["budget"], remaining=max(0, manifest["budget"] - len(journal(run)))))
    try:
        while True:
            line = source.readline(1024 * 1024 + 1)
            if not line:
                return 0
            if len(line) > 1024 * 1024:
                stream_reply(output, dict(ok=False, error="request exceeds 1 MiB; logger closed"))
                return 2
            try:
                request = serve_request(line)
                if request.get("quit") is True and len(request) == 1:
                    stream_reply(output, dict(ok=True, closed=True))
                    return 0
                if read_json(run / "manifest.json") != manifest:
                    stream_reply(output, dict(ok=False, error="manifest changed; logger closed"))
                    return 2
                response = perform_call(run, request["op"], request.get("args", {}), request["expect"],
                                        request.get("see", ""), request.get("phase", "build"), session.borrow)
                stream_reply(output, response)
            except (OSError, ValueError, KeyError, TypeError) as error:
                local_errors += 1
                stream_reply(output, dict(ok=False, error=str(error), local_errors=local_errors))
                if local_errors >= 100 or len(journal(run)) >= manifest["budget"]:
                    return 2
    finally:
        session.finish()


def arguments(row):
    return row["args"] if isinstance(row.get("args"), dict) else {}


def category(row):
    op, args = row["op"], arguments(row)
    if op in OBSERVATIONS:
        return "observation"
    if op in SAVE_REPLAY:
        return "save_replay"
    if op == "ui_pointer" and args.get("action") == "move":
        return "pointer_move"
    if op in UI_OPS and row.get("phase") != "build":
        return row["phase"] + "_action"
    return "authoring"


def metrics(run):
    manifest, rows = read_json(Path(run) / "manifest.json"), journal(run)
    counts = collections.Counter(category(row) for row in rows)
    last = rows[-1]["finished_at"] if rows else manifest["created_at"]
    elapsed = (datetime.datetime.fromisoformat(last) -
               datetime.datetime.fromisoformat(manifest["created_at"])).total_seconds()
    pointer = collections.Counter(arguments(row).get("action") for row in rows if row["op"] == "ui_pointer")
    return dict(role=manifest["role"], task=manifest["task"], attempts=len(rows),
                sent_calls=sum(row.get("sent", False) for row in rows),
                errors=sum(row.get("ok") is False for row in rows),
                denied=sum(row.get("denied", False) for row in rows), categories=dict(counts),
                clicks=pointer["click"], keys=sum(row["op"] == "ui_key" for row in rows),
                hand_actions=sum(row["op"] in UI_OPS - {"view_screenshot"} and
                                 not (row["op"] == "ui_pointer" and arguments(row).get("action") == "move")
                                 for row in rows),
                typed_characters=sum(len(str(arguments(row).get("text", ""))) for row in rows if row["op"] == "ui_type"),
                pointer_actions=dict(pointer), screenshots=sum(row["op"] == "view_screenshot" for row in rows),
                operation_ms=round(sum(row.get("ms", 0) for row in rows), 3),
                elapsed_seconds=round(elapsed, 3), phases=dict(collections.Counter(row["phase"] for row in rows)))


def references(item, role, numbers, problems, label):
    key = "actions" if role == "ui" else "calls"
    refs = item.get(key)
    if not isinstance(refs, list) or not refs or any(type(n) is not int or n not in numbers for n in refs):
        problems.append(f"{label}: {key} must reference existing journal entries")
        return []
    return refs


def screenshot_evidence(run, item, refs, rows, problems, label):
    try:
        shot = confined_path(run, item.get("shot"))
        if not shot.is_file():
            raise ValueError("screenshot does not exist")
        sources = [r for r in rows if r["n"] in refs and r["op"] == "view_screenshot" and r.get("ok")]
        if not any(confined_path(run, r["args"].get("file")) == shot for r in sources):
            raise ValueError("screenshot must cite its successful screenshot call")
    except (ValueError, TypeError) as error:
        problems.append(f"{label}: {error}")


def check_items(run, report, manifest, rows, problems):
    role, numbers = manifest["role"], {r.get("n") for r in rows}
    for key in ("criteria", "findings"):
        items = report.get(key)
        if not isinstance(items, list) or (key == "criteria" and not items):
            problems.append(f"report {key} must be a {'nonempty ' if key == 'criteria' else ''}list")
            continue
        for i, item in enumerate(items):
            label = f"{key}[{i}]"
            if not isinstance(item, dict):
                problems.append(f"{label}: expected an object")
                continue
            refs = references(item, role, numbers, problems, label)
            if role == "ui":
                screenshot_evidence(run, item, refs, rows, problems, label)
            if key == "criteria":
                if not item.get("name") or not item.get("expected") or type(item.get("pass")) is not bool:
                    problems.append(f"{label}: name, expected and boolean pass are required")
                evidence = "seen" if role == "ui" else "measured"
                unavailable = role == "api" and "measured" in item and item["measured"] is None and item.get("pass") is False
                if not unavailable and (evidence not in item or item[evidence] is None or item[evidence] == ""):
                    problems.append(f"{label}: {evidence} is required")
                evidence_ops = {"view_screenshot"} if role == "ui" else {"sim_step", "sim_state"}
                successful = [r for r in rows if r["n"] in refs and r.get("ok") and
                              r.get("phase") == "verify" and r.get("op") in evidence_ops]
                if not unavailable and not successful:
                    problems.append(f"{label}: no successful verification-phase evidence")
            elif any(not item.get(k) for k in ("id", "did", "expected", "got")):
                problems.append(f"{label}: id, did, expected and got are required")


def check_report(report, manifest, problems):
    expected_agent = "ui-human-tester" if manifest["role"] == "ui" else "api-agent-tester"
    if report.get("agent") != expected_agent or report.get("task") != manifest["task"]:
        problems.append("report agent/task does not match manifest")
    if report.get("outcome") not in ("done", "partial", "failed", "blocked"):
        problems.append("report outcome is invalid")
    criteria = report.get("criteria")
    passed = [c.get("pass") is True for c in criteria if isinstance(c, dict)] if isinstance(criteria, list) else []
    if report.get("outcome") == "done" and (not passed or not all(passed)):
        problems.append("done requires every reported criterion to pass")
    if not isinstance(report.get("top3"), list):
        problems.append("report top3 must be a list")
    mast = report.get("mast", {})
    if not isinstance(mast, dict):
        mast = {}
    for key in MAST_KEYS:
        val = mast.get(key)
        if not isinstance(val, str) or not (val == "ok" or any(val.startswith(p) and val[len(p):].strip()
                                                              for p in ("violated:", "not_applicable:"))):
            problems.append(f"{key}: use ok, violated: reason, or not_applicable: reason")


def check(run):
    """Check evidence contracts only; physical success requires an independent checker."""
    run = Path(run).resolve()
    manifest, rows, problems = read_json(run / "manifest.json"), journal(run), []
    if not rows:
        problems.append("journal is empty")
    if [r.get("n") for r in rows] != list(range(1, len(rows) + 1)):
        problems.append("journal counters are not contiguous")
    if len(rows) > manifest["budget"]:
        problems.append("journal exceeded budget")
    for row in rows:
        reason = role_problem(manifest["role"], row.get("op", ""), row.get("args"), run)
        if reason:
            problems.append(f"entry {row.get('n')}: role violation: {reason}")
        if row.get("task") != manifest["task"] or row.get("phase") not in PHASES:
            problems.append(f"entry {row.get('n')}: task/phase mismatch")
    try:
        report = read_json(run / "report.json")
        if not isinstance(report, dict):
            raise ValueError("report must be an object")
        check_report(report, manifest, problems)
        check_items(run, report, manifest, rows, problems)
    except (OSError, ValueError) as error:
        problems.append(f"report: {error}")
    result = dict(contract_ok=not problems, problems=problems, physics_verified=False,
                  note="Contract validation does not establish physical accuracy or human usability.")
    write_json(run / "check.json", result)
    return result


def parser():
    root = argparse.ArgumentParser(description=__doc__)
    commands = root.add_subparsers(dest="command", required=True)
    init = commands.add_parser("init")
    init.add_argument("run")
    init.add_argument("--role", choices=("ui", "api"), required=True)
    init.add_argument("--port", type=int, default=47801)
    init.add_argument("--task", required=True)
    init.add_argument("--budget", type=int, default=150)
    call = commands.add_parser("call")
    call.add_argument("run")
    call.add_argument("op")
    call.add_argument("args", nargs="?", default="{}")
    call.add_argument("--expect", required=True)
    call.add_argument("--see", default="")
    call.add_argument("--phase", choices=PHASES, default="build")
    for name in ("metrics", "check", "serve"):
        commands.add_parser(name).add_argument("run")
    return root


def main(argv=None):
    options = parser().parse_args(argv)
    try:
        if options.command == "serve":
            return serve(options.run)
        if options.command == "init":
            result = initialize(options.run, options.role, options.port, options.task, options.budget)
        elif options.command == "call":
            result = perform_call(options.run, options.op, json.loads(options.args), options.expect,
                                  options.see, options.phase)
        else:
            result = metrics(options.run) if options.command == "metrics" else check(options.run)
        print(json.dumps(result, ensure_ascii=False, indent=2, allow_nan=False))
        return 1 if result.get("ok") is False or result.get("contract_ok") is False else 0
    except (OSError, ValueError, KeyError) as error:
        print(str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
