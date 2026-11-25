#!/usr/bin/env python3
"""The algebra of the editor's operations: every edit and every ordered pair of edits, run on a scene of every kind of
object in kinetica-core (ke.Scene(), about 3 ms a call - no window, no agent), held to laws that need no expected answer.

The operations are the basis; an edit maps a scene to a scene. The laws:
- inverse: an edit and its undo are the identity - the scene's text bit for bit as before (the history makes every edit
  invertible: what it erased is kept to be given back), for one edit and for a pair, (AB)⁻¹ = B⁻¹A⁻¹;
- iso: a scene saved and opened is the same scene;
- words: every answer is a success or a refusal in words - never an internal error, never a dropped connection.
Measured, not a law: the commutator [A, B] - whether AB and BA leave the same scene (ids of what they made aside, blocks
in any order). Arguments come from each operation's schema and the scene's objects (a joint's field gets a joint, a list of
ids an entity); RECIPES gives the few a schema cannot (which line of the scene file to set). Only the standard library::

    python3 tools/op_algebra.py            # singles and every pair: about two minutes; the exit code is the laws broken
    python3 tools/op_algebra.py --sample 300 --json out.json
    python3 tools/op_algebra.py --walk 3 --json walk.json   # the automaton: 3 covers of walks, about a minute

The automaton: a scene's state is infinite, its class finite (the world's mode, the kinds of object held); walks are a
sequence covering array - every ordered triple of the 53 actions in that order in one of them - and each is undone
whole after (unless it undid the scene's own building steps: then the scene is built again). Its curve - classes and
transitions met against steps - flattening is the asymptote.
"""
import argparse
import json
import os
import random
import re
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "clients", "python"))
import kinetica as ke  # noqa: E402

LEFT_OUT = {"edit_undo", "edit_redo",            # the inverse itself
            "scene_new", "scene_open",           # a whole new scene: a single only
            "scene_batch"}                       # a composition of the others
# What a schema cannot say: which line of the scene file a set means, a block to put, a type of component, a point list.
E = lambda p, k=0: p["entity"][k]  # noqa: E731 - a body of the fixture
RECIPES = {
    "scene_set": lambda p: {"id": E(p), "line": "shape", "key": "color", "values": [0.9, 0.2, 0.2]},
    "world_set": lambda p: {"line": "world", "key": "gravity", "values": [0, -5, 0]},
    "scene_put": lambda p: {"block": 'entity "Вставка"\n  object id 0\n  shape box size 0.1 0.1 0.1 position 0 2 0 rotation 0 0 0 color 0.5 0.5 0.5\nend\n'},
    "scene_import": lambda p: {"file": os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "examples", "models", "house.obj")},
    "component_set": lambda p: {"id": E(p, 1), "type": "probe.mark", "values": {}},
    "component_remove": lambda p: {"id": E(p), "type": "probe.mark"},
    "assembly_add": lambda p: {"name": "Рука UR5e", "position": [-2, 0, 0]},
    "assembly_camera": lambda p: {"target": E(p), "face": "+x"},
    "assembly_preset": lambda p: {"target": E(p), "preset": "hinge", "face": "+y"},
    "assembly_sensor": lambda p: {"target": E(p), "kind": "imu"},
    "assembly_path": lambda p: {"goal": p["goal"][0], "points": [[2.2, 0.6, 0.2], [2.1, 0.7, 0.1]]},
    "assembly_goal": lambda p: {"effector": p["link"][-1]},
    "assembly_reach": lambda p: {"effector": p["effector"][0], "position": [2.2, 0.6, 0.2]},
    "assembly_drive": lambda p: {"joint": p["hinge"][0], "target": 0.2},
    "scene_turn": lambda p: {"ids": [E(p)], "axis": "y", "degrees": 30},
    "scene_ungroup": lambda p: {"ids": [p["group"][0]]},
    "assembly_power": lambda p: {"joint": p["motor"][0]},
    "controller_add": lambda p: {"goal": p["goal"][0]},
    "sensor_spec": lambda p: {"sensor": p["sensor"][0], "spec": {}},
    "sequence_key": lambda p: {"ids": [E(p, 1)], "time": 1},
    "sequence_delete_key": lambda p: {"id": E(p), "time": 0},
    "sequence_key_set": lambda p: {"id": E(p), "time": 0, "position": [0, 1, 0]},
}
# A field's name -> the kind of object (scene_list kind) whose id it takes; an id-like field is given even when optional.
POOLS = {"joint": "joint", "joints": "joint", "goal": "goal", "camera": "camera", "sequence": "sequence", "battery": "battery",
         "controller": "controller", "sensor": "sensor", "force": "force", "light": "light"}
ID_FIELDS = {"id", "ids", "target", "body", "camera"}
INTERNAL = re.compile(r"внутренняя ошибка|internal error|terminate|exception", re.I)


def fixture(ed):
    """A scene of every kind: the sports car, the UR5e arm, free shapes, a light, a camera, a force, a sequence with a key,
    a component, and a motor on one of the arm's joints that no driver feeds yet."""
    ed.assembly_add(name="Спорткар", position=[0, 0, 0])
    arm = ed.assembly_add(name="Рука UR5e", position=[2, 0, 0])
    made = [ed.scene_add(shape=shape, position=[x, 0.5, 1], size=[0.2, 0.2, 0.2], role="rigid")["id"]
            for shape, x in (("box", -1), ("sphere", -1.5), ("cylinder", -2))]
    ed.scene_add_light(kind="point", at=[0, 2, 0])
    ed.scene_add_camera(eye=[3, 2, 3], target=[0, 0, 0])
    ed.scene_add_force(shape="directional", mode="constant", at=[0, 1, 2])
    ed.sequence_add()
    ed.sequence_key(ids=[made[0]], time=0)
    ed.component_set(id=made[0], type="probe.mark", values={})
    hinges = re.findall(r'joint "[^"]*"\n  object id (\d+)\n(?:  [^\n]*\n)*?  free rx\n', ed.scene_get()["text"])
    joint = int(hinges[-1])  # the arm's last hinge: the arm went in after the car
    ed.assembly_drive(joint=joint, kind="dcmotor", voltage=12, resistance=1, constant=0.1)
    return {"motor": [joint], "made": made, "arm": arm["id"]}


def pools(ed, extra):
    """{kind: [ids]} of the scene; "entity": the fixture's free bodies first, then every body; "goal", "effector", "link"
    (a body a joint moves) read from the scene's text."""
    out = dict(extra)
    for o in ed.scene_list()["objects"]:
        out.setdefault(o["kind"], []).append(o["id"])
    text = ed.scene_get()["text"]
    blocks = {int(m.group(1)): m.group(0) for m in re.finditer(r"object id (\d+)[^\n]*\n(?:  [^\n]*\n)*", text)}
    out["goal"] = [i for i, b in blocks.items() if "\n  goal " in b]
    out["effector"] = [int(x) for x in re.findall(r"goal effector (\d+)", text)]
    hinges = {int(i): b for i, b in blocks.items() if "\n  free rx\n" in b and "\n  link a " in b}
    out["hinge"] = list(hinges)
    out["link"] = [int(re.search(r"link a (\d+)", b).group(1)) for b in hinges.values()]
    out["link"] = [i for i in out["link"] if i not in out["effector"]]
    out["entity"] = extra["made"] + [i for i, b in blocks.items() if "\n  rigid " in b and i not in extra["made"] and i not in out["goal"]]
    return out


def value(name, schema, p):
    """An argument from its schema and the scene."""
    if "enum" in schema:
        return schema["enum"][0]
    kind = POOLS.get(name)
    t = schema.get("type")
    if t == "array" and schema.get("minItems") == 3:
        return [0.1, 0.2, 0.3]
    if t == "array":
        return [p.get(kind or "entity", [0])[0]]
    if t == "integer":
        return p.get(kind or "entity", [1])[0]
    if t == "number":
        return 0.1
    if t == "boolean":
        return True
    if t == "string":
        return "Проба"
    if t == "object":
        return {}
    return [p["entity"][0]] if name == "ids" else p["entity"][0]  # an id given as any type


def args_for(op, p):
    if op["name"] in RECIPES:
        return RECIPES[op["name"]](p)
    s = op["schema"]
    given = set(s.get("required", [])) | (ID_FIELDS & set(s["properties"]))
    if {"id", "ids"} <= given:  # one of the two
        given.discard("id")
    return {k: value(k, s["properties"][k], p) for k in given}


class Bench:
    """One kinetica-core with the fixture, put back after every probe by undo - checked, not trusted."""

    def __init__(self):
        self.scene = ke.Scene()
        self.ed = self.scene.ed
        self.extra = fixture(self.ed)
        self.pools = pools(self.ed, self.extra)
        self.base, self.depth = self.text(), self.steps()
        self.ops = [o for o in self.ed.ops_list()["ops"] if o.get("kind") == "edit" and o["name"] not in LEFT_OUT]

    def text(self):
        return self.ed.scene_get()["text"]

    def steps(self):
        return self.ed.edit_redo()["undo_steps"]

    def call(self, op, args):
        """(ok, error), the answer kept in `last` - an exception of the transport is an answer too: the connection dropped."""
        self.last = {}
        try:
            self.last = getattr(self.ed, op)(**args) or {}
            return True, ""
        except ke.client.OperationError as e:
            return False, str(e)
        except Exception as e:  # noqa: BLE001 - the core died or the wire broke: the worst answer
            return False, f"TRANSPORT {type(e).__name__}: {e}"

    def back(self):
        """Undo to the fixture's depth - each undo says how deep the history still is, and nothing redoes on the way -;
        the text must be the fixture's again."""
        self.call("sim_stop", {})
        if self.text() == self.base:
            return True
        for _ in range(10000):
            a = self.ed.edit_undo()
            if not a.get("undone") or a.get("undo_steps", 0) <= self.depth:
                break
        return self.text() == self.base


def canonical(text, made):
    """A scene's text with the ids of what a probe made masked and its blocks sorted: the commutator compares these."""
    blocks = re.split(r"(?<=\nend\n)", text)
    word = re.compile(r"\b(%s)\b" % "|".join(map(str, sorted(made, reverse=True)))) if made else None
    return sorted(word.sub("#", b) if word else b for b in blocks)


def run(sample=None, seed=1):
    bench = Bench()
    ed, ops, p = bench.ed, bench.ops, bench.pools
    laws, singles = [], {}

    def law(kind, what, detail):
        laws.append({"law": kind, "ops": what, "detail": detail[:300]})

    # ---- singles: words, inverse, iso
    folder = tempfile.mkdtemp(prefix="ke-algebra-")
    for op in ops:
        args = args_for(op, p)
        ok, err = bench.call(op["name"], args)
        singles[op["name"]] = {"ok": ok, "error": err, "args": args}
        if INTERNAL.search(err) or err.startswith("TRANSPORT"):
            law("words", [op["name"]], err)
        if ok:
            text, file = bench.text(), os.path.join(folder, op["name"] + ".kscene")
            if bench.call("scene_save", {"file": file})[0] and bench.call("scene_open", {"file": file})[0] and bench.text() != text:
                law("iso", [op["name"]], "the scene saved and opened differs")
        if not bench.back():
            law("inverse", [op["name"]], "undo did not give the scene back")
            bench = Bench()
            ed, p = bench.ed, bench.pools
    usable = [o for o in ops if singles[o["name"]]["ok"]]

    # ---- pairs: inverse of the pair, words, the commutator
    pairs = [(a, b) for a in usable for b in usable]
    if sample:
        random.Random(seed).shuffle(pairs)
        pairs = pairs[:sample]
    commute = {}
    for a, b in pairs:
        if a is b:
            continue
        seen = []
        for first, second in ((a, b), (b, a)):
            made = set()
            for op in (first, second):
                ok, err = bench.call(op["name"], args_for(op, p))
                if INTERNAL.search(err) or err.startswith("TRANSPORT"):
                    law("words", [first["name"], second["name"]], err)
            seen.append(bench.text())
            if not bench.back():
                law("inverse", [first["name"], second["name"]], "undo of the pair did not give the scene back")
                bench = Bench()
                ed, p = bench.ed, bench.pools
        made = {int(i) for i in re.findall(r"object id (\d+)", seen[0] + seen[1])} - {int(i) for i in re.findall(r"object id (\d+)", bench.base)}
        commute[a["name"] + " " + b["name"]] = canonical(seen[0], made) == canonical(seen[1], made)
    bench.scene.close()
    return {"ops": [o["name"] for o in ops], "singles": singles, "pairs": len(commute), "commute": commute, "laws": laws}


# ---- the automaton: states as classes, walks as a sequence covering array ---------------------------------------------
# A scene's state is infinite (every coordinate a float, any number of objects); its class is finite: the mode of the
# world and which kinds of objects the scene holds. The walks: permutations of all the actions such that every ordered
# triple of actions appears in that order in one of them (a sequence covering array - Kuhn et al., NIST: their number
# grows as the logarithm of the actions, not as their cube). Each walk starts from the fixture and is undone whole after.
KINDS = ("entity", "joint", "sensor", "battery", "driver", "controller", "sequence", "light", "camera", "force", "group", "wheel")
ACTIONS = {"edit_undo": {}, "edit_redo": {}, "scene_new": {}, "sim_step": {"frames": 2}, "sim_stop": {}}


def state(ed):
    """The class of the scene: its world's mode and the kinds of object it holds (more than the floor for entities)."""
    kinds = {}
    for o in ed.scene_list()["objects"]:
        kinds[o["kind"]] = kinds.get(o["kind"], 0) + 1
    held = "".join(k[0] if kinds.get(k, 0) > (1 if k == "entity" else 0) else "·" for k in KINDS)
    return ed.sim_state()["mode"] + " " + held


def sequence_cover(n, seed=1, candidates=30):
    """Permutations of range(n) in which every ordered triple of distinct items appears in that order in some one -
    greedily, each the best of `candidates` random ones."""
    rng, left, out = random.Random(seed), n * (n - 1) * (n - 2), []
    covered = set()
    while len(covered) < left:
        best, gain = None, -1
        for _ in range(candidates):
            perm = list(range(n))
            rng.shuffle(perm)
            new = sum((a, b, c) not in covered for i, a in enumerate(perm) for j, b in enumerate(perm[i + 1:], i + 1) for c in perm[j + 1:])
            if new > gain:
                best, gain = perm, new
        out.append(best)
        covered |= {(a, b, c) for i, a in enumerate(best) for j, b in enumerate(best[i + 1:], i + 1) for c in best[j + 1:]}
    return out


def basis(edges):
    """The fewest actions whose transitions reach every class the walks met, greedily (set cover): what each adds."""
    reach = {}
    for frm, name, ok, to in edges:
        if ok and frm != to:
            reach.setdefault(name, set()).add((frm, to))
    classes = {to for frm, name, ok, to in edges} - {edges[0][0]} if edges else set()
    left, out = set(classes), []
    while left:
        name = max(reach, key=lambda n: len({to for _, to in reach[n]} & left))
        gain = {to for _, to in reach[name]} & left
        if not gain:
            break
        out.append([name, len(gain)])
        left -= gain
    return out


def walk(rounds=1, limit=None):
    """Every walk of the cover run from the fixture: the classes and transitions met, step by step (the coverage curve),
    and the laws - words at every step, the whole walk undone gives the fixture back, the scene at its end saved and
    opened is the same."""
    bench = Bench()
    ops = {o["name"]: o for o in bench.ops}
    names = sorted(ops) + sorted(ACTIONS)
    walks = [perm for seed in range(1, rounds + 1) for perm in sequence_cover(len(names), seed)][:limit]  # each round a cover
    laws, states, transitions, curve, skipped, below = [], set(), set(), [], 0, 0
    folder = tempfile.mkdtemp(prefix="ke-walk-")
    for w, perm in enumerate(walks):
        now, deepest = state(bench.ed), bench.depth
        states.add(now)
        for k in perm:
            name = names[k]
            try:
                args = ACTIONS[name] if name in ACTIONS else args_for(ops[name], pools(bench.ed, bench.extra))
            except (IndexError, KeyError):  # nothing of the kind it needs is in the scene now: not applicable here
                skipped += 1
                continue
            ok, err = bench.call(name, args)
            if INTERNAL.search(err) or err.startswith("TRANSPORT"):
                laws.append({"law": "words", "ops": [name], "detail": f"walk {w}: {err}"[:300]})
            if name == "edit_undo" and bench.last.get("undone"):  # how deep the history still is, as the undo says
                deepest = min(deepest, bench.last.get("undo_steps", deepest))
            after = state(bench.ed)
            transitions.add((now, name, ok, after))
            states.add(after)
            now = after
            curve.append([len(curve) + 1, len(states), len(transitions)])
        text, file = bench.text(), os.path.join(folder, f"walk-{w}.kscene")
        if bench.call("scene_save", {"file": file})[0] and bench.call("scene_open", {"file": file})[0] and bench.text() != text:
            laws.append({"law": "iso", "ops": [f"walk {w}"], "detail": "the scene at the walk's end saved and opened differs"})
        if deepest < bench.depth:  # it undid steps of the fixture's own: what an edit after them dropped no undo gives back
            below += 1
            bench.scene.close()
            bench = Bench()
        elif not bench.back():
            laws.append({"law": "inverse", "ops": [f"walk {w}"], "detail": "the whole walk undone did not give the fixture back"})
            bench = Bench()
    bench.scene.close()
    return {"actions": names, "rounds": rounds, "walks": len(walks), "steps": len(curve), "skipped": skipped, "below": below, "states": sorted(states),
            "transitions": len({t[:3] for t in transitions}), "edges": sorted(transitions), "curve": curve, "laws": laws,
            "basis": basis(sorted(transitions))}


def main(argv=None):
    a = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    a.add_argument("--sample", type=int, help="this many pairs at random instead of all")
    a.add_argument("--json", help="write the results here")
    a.add_argument("--walk", type=int, nargs="?", const=1, metavar="ROUNDS",
                   help="the automaton instead: ROUNDS covers of walks, each covering every ordered triple of actions")
    args = a.parse_args(argv)
    began = time.monotonic()
    if args.walk:
        r = walk(args.walk)
        print(f"Действий: {len(r['actions'])}; проходов: {r['walks']} (каждая упорядоченная тройка действий — в одном из них); "
              f"шагов: {r['steps']}, неприменимых: {r['skipped']}; {time.monotonic() - began:.0f} с")
        tail = r["curve"][-len(r["curve"]) // 4:] if r["curve"] else []
        print(f"Классов состояний: {len(r['states'])}; переходов «класс, действие, исход»: {r['transitions']}; "
              f"за последнюю четверть шагов новых классов: {tail[-1][1] - tail[0][1] if tail else 0}, переходов: {tail[-1][2] - tail[0][2] if tail else 0}")
        print("Наименьший набор действий, который доводит до всех встреченных классов: " + ", ".join(f"{n} (+{g})" for n, g in r["basis"]))
        print(f"Проходов, отменивших и шаги самой сцены (их отмена всего прохода не проверяет): {r['below']}")
        print(f"Законы нарушены: {len(r['laws'])}")
        for l in r["laws"][:30]:
            print(f"  {l['law']}: {' · '.join(l['ops'])} — {l['detail']}")
        if args.json:
            with open(args.json, "w", encoding="utf-8") as f:
                json.dump(r, f, ensure_ascii=False, indent=1)
        return len(r["laws"])
    r = run(args.sample)
    usable = [n for n, s in r["singles"].items() if s["ok"]]
    print(f"Операций правки: {len(r['ops'])}, выполнились на сцене: {len(usable)}; пар: {r['pairs']}; {time.monotonic() - began:.0f} с")
    refused = {n: s["error"][:110] for n, s in r["singles"].items() if not s["ok"]}
    if refused:
        print("Отказали с аргументами из схемы (нужен рецепт):")
        for n, e in refused.items():
            print(f"  {n}: {e}")
    c = r["commute"]
    print(f"Коммутируют: {sum(c.values())} пар из {len(c)}")
    print(f"Законы нарушены: {len(r['laws'])}")
    for l in r["laws"][:30]:
        print(f"  {l['law']}: {' · '.join(l['ops'])} — {l['detail']}")
    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump(r, f, ensure_ascii=False, indent=1)
    return len(r["laws"])


if __name__ == "__main__":
    sys.exit(main())
