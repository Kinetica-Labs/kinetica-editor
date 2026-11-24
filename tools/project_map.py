#!/usr/bin/env python3
"""The project's map: every tracked file with who reads it, the rules of its order checked, its entropy measured.

Lehman's second law - a growing system grows more complex unless work is spent to keep it simple - is why it exists:
the rules are architecture fitness functions (Ford and Parsons, «Building Evolutionary Architectures»), machine checks
CI runs, not advice; the rest is a report of where order is wearing thin. Only the Python standard library::

    python3 tools/project_map.py          # the rules, then the report; the exit code is the number of rules broken
    python3 tools/project_map.py --json   # the same as data

Rules (test_project_map keeps them at zero):
- layers: src/ is layered bottom-up (CLAUDE.md, «Устройство»); a file includes only its own layer and those below;
- budgets: a source file stays under BUDGET_LINES lines (no God object), the always-loaded rules under their size;
- readers: every tracked file is read by another - named by its path or file name, or found by a way READ_BY says.
Report: sentences repeated across documents (a fact lives in one place - Diátaxis), operations no test names, the
week's lines added and deleted, the code's compressibility (gzip: a repeating pattern compresses), the largest files.
"""
import argparse
import bisect
import datetime
import fnmatch
import json
import os
import re
import subprocess
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LAYERS = ["math", "core", "spatial", "devices", "electro", "scene", "physics", "sim", "api", "host"]  # bottom-up
BUDGET_LINES = 1600
BUDGET_CHARS = {"CLAUDE.md": 12000}  # read at the start of every session: its size is paid each time
CODE = (".cpp", ".h", ".py")
# Files no one names, found another way - each with the reader that finds it.
READ_BY = {
    "data/assemblies/*.kscene": "src/scene/SceneFiles.cpp (assemblyFiles: the folder's scenes)",
    "clients/python/kinetica/plugin_template*.py": "editor/PluginHost.cpp (plugin_template[_<kind>].py)",
    "data/icons/*/*.svg": "editor/Icons.h (svgIcon: data/icons/<folder>/<name>.svg)",
    "examples/scenes/*.kscene": "editor/MainWindow.h (scenesDir: the example scenes' menu)",
    "tests/data/scene-format-*.kscene": "tests (scene-format-<n>.kscene)",
    "resources/icons/kinetica-*.png": "the window's icon of every size",
    "examples/models/*/*": "the scenes that load the model",
    "docs/images/*": "the documents that show them",
    "translations/*.ts": "CMakeLists.txt (qt_add_translations)",
    ".github/*": "GitHub",
    ".*": "git and its tools",
    "CLAUDE.md": "Claude Code", "README.md": "GitHub", "LICENSE": "people", "CONTRIBUTING.md": "GitHub",
    "THIRD_PARTY_NOTICES.md": "people", "CMakeLists.txt": "cmake", "extern/*": "git (a submodule)",
}


def tracked():
    out = subprocess.run(["git", "ls-files", "-z"], cwd=ROOT, capture_output=True, check=True).stdout
    return [p for p in out.decode("utf-8").split("\0") if p and os.path.isfile(os.path.join(ROOT, p))]


def text_of(path, limit=2_000_000):
    """A file's text, or None for a binary or a very large one (a model's mesh)."""
    full = os.path.join(ROOT, path)
    if os.path.getsize(full) > limit:
        return None
    try:
        with open(full, encoding="utf-8") as f:
            return f.read()
    except (UnicodeDecodeError, OSError):
        return None


# ---- the rules ------------------------------------------------------------------------------------------------------

def layer_breaks(texts):
    """Every include in src/ that reaches up a layer: (file, the included)."""
    out = []
    for path, text in texts.items():
        parts = path.split("/")
        if parts[0] != "src" or len(parts) < 3 or parts[1] not in LAYERS:
            continue
        for included in re.findall(r'#include "([a-z]+)/[^"]+"', text):
            if included in LAYERS and LAYERS.index(included) > LAYERS.index(parts[1]):
                out.append((path, re.search(r'#include "(%s/[^"]+)"' % included, text).group(1)))
    return out


def budget_breaks(texts):
    """Files over their budget: (file, its size, the budget, the unit)."""
    out = []
    for path, text in texts.items():
        if path in BUDGET_CHARS and len(text) > BUDGET_CHARS[path]:
            out.append((path, len(text), BUDGET_CHARS[path], "символов"))
        elif path.endswith(CODE) and text.count("\n") > BUDGET_LINES:
            out.append((path, text.count("\n"), BUDGET_LINES, "строк"))
    return out


def readers(paths, texts):
    """{file: one file that names it, or how it is found}; a file with neither is missing."""
    order = sorted(texts)
    corpus, starts = [], []
    at = 0
    for p in order:
        starts.append(at)
        corpus.append(texts[p])
        at += len(texts[p]) + 1
    corpus = "\0".join(corpus)
    found = {}
    for path in paths:
        way = next((why for pattern, why in READ_BY.items() if fnmatch.fnmatch(path, pattern)), None)
        if way:
            found[path] = way
            continue
        folder = os.path.dirname(path)
        names = {path, os.path.basename(path)} | ({folder + "/"} if folder.count("/") >= 1 else set())  # a folder named: its files
        for name in names:
            i = corpus.find(name)
            while i >= 0:
                owner = order[bisect.bisect_right(starts, i) - 1]
                if owner != path:
                    found[path] = owner
                    break
                i = corpus.find(name, i + 1)
            if path in found:
                break
        stem = os.path.splitext(os.path.basename(path))[0]
        if path not in found and path.endswith(".py"):  # a module: imported, or run by unittest, by its bare name
            m = re.search(r"(?:\bimport\s+|\bfrom\s+\.?|\bunittest\b[^\n]*?\s|kinetica\.)%s\b" % re.escape(stem), corpus)
            if m and order[bisect.bisect_right(starts, m.start()) - 1] != path:
                found[path] = order[bisect.bisect_right(starts, m.start()) - 1]
    return found


# ---- the report -----------------------------------------------------------------------------------------------------

def repeated_sentences(texts, shortest=80):
    """Sentences of the documents (.md) said word for word in two files or more: {sentence: [files]}."""
    seen = {}
    for path, text in texts.items():
        if not path.endswith(".md"):
            continue
        for s in re.split(r"(?<=[.!?:])\s+|\n\s*\n", text):
            s = " ".join(s.split())
            if len(s) >= shortest and not s.startswith(("|", "```", "#")):
                seen.setdefault(s, set()).add(path)
    return {s: sorted(f) for s, f in seen.items() if len(f) > 1}


def untested_ops(texts):
    """The window's operations (tests/data/ops-list.json) that no test names."""
    ops = [op["name"] for op in json.loads(texts.get("tests/data/ops-list.json", '{"ops": []}')).get("ops", [])]
    tests = "\n".join(t for p, t in texts.items() if p.startswith("tests/") and p != "tests/data/ops-list.json"
                      or fnmatch.fnmatch(p, "tools/test_*.py") or fnmatch.fnmatch(p, "editor/SelfTest*.cpp"))
    return [op for op in ops if not re.search(r"\b%s\b" % re.escape(op), tests)]


def week(days=7):
    """Lines added and deleted in code and documents over the last days."""
    since = (datetime.date.today() - datetime.timedelta(days=days)).isoformat()
    out = subprocess.run(["git", "log", f"--since={since}", "--format=", "--numstat", "--", "*.cpp", "*.h", "*.py", "*.md"],
                         cwd=ROOT, capture_output=True, text=True, check=True).stdout
    added = deleted = 0
    for line in out.splitlines():
        a, d, *_ = line.split("\t") + ["", ""]
        if a.isdigit() and d.isdigit():
            added, deleted = added + int(a), deleted + int(d)
    return {"days": days, "added": added, "deleted": deleted, "deleted_per_added": round(deleted / added, 3) if added else 0.0}


def build():
    paths = tracked()
    texts = {p: t for p in paths if (t := text_of(p)) is not None}
    code = "".join(t for p, t in texts.items() if p.endswith(CODE)).encode("utf-8")
    read = readers(paths, texts)
    lines = lambda suffixes: sum(t.count("\n") for p, t in texts.items() if p.endswith(suffixes))  # noqa: E731
    return {
        "rules": {"layers": layer_breaks(texts), "budgets": budget_breaks(texts), "readers": sorted(set(paths) - set(read))},
        "report": {
            "files": len(paths), "code_lines": lines(CODE), "doc_lines": lines((".md",)),
            "code_gzip_ratio": round(len(zlib.compress(code, 9)) / max(1, len(code)), 3),
            "week": week(), "repeated": repeated_sentences(texts), "untested_ops": untested_ops(texts),
            "largest": sorted(((t.count("\n"), p) for p, t in texts.items() if p.endswith(CODE)), reverse=True)[:5],
        },
    }


def show(m):
    r, rep = m["rules"], m["report"]
    print("Правила порядка")
    print(f"  слои: {len(r['layers'])} связей вверх" + "".join(f"\n    {a} → {b}" for a, b in r["layers"]))
    print(f"  бюджеты: {len(r['budgets'])} сверх" + "".join(f"\n    {p}: {n} {u} при {b}" for p, n, b, u in r["budgets"]))
    print(f"  читатели: {len(r['readers'])} файлов без читателя" + "".join(f"\n    {p}" for p in r["readers"]))
    w = rep["week"]
    print(f"\nЭнтропия: файлов {rep['files']}, строк кода {rep['code_lines']}, документов {rep['doc_lines']} "
          f"(на строку кода {rep['doc_lines'] / max(1, rep['code_lines']):.2f}); код сжимается до {rep['code_gzip_ratio']}")
    print(f"  за {w['days']} дней: +{w['added']} −{w['deleted']} строк, удалено на добавленную {w['deleted_per_added']}")
    print("  крупнейшие: " + ", ".join(f"{p} {n}" for n, p in rep["largest"]))
    print(f"\nПовторы в документах: {len(rep['repeated'])} фраз дословно в двух файлах и больше")
    for s, files in list(rep["repeated"].items())[:10]:
        print(f"  «{s[:90]}…» — {', '.join(files)}")
    print(f"\nОперации, которых не называет ни один тест: {len(rep['untested_ops'])}")
    if rep["untested_ops"]:
        print("  " + ", ".join(rep["untested_ops"]))


def broken(m):
    return sum(len(v) for v in m["rules"].values())


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("--json", action="store_true", help="the map as data")
    m = build()
    if p.parse_args(argv).json:
        print(json.dumps(m, ensure_ascii=False, indent=1))
    else:
        show(m)
    return broken(m)


if __name__ == "__main__":
    sys.exit(main())
