#!/usr/bin/env python3
"""Mutation testing - the proof by contradiction of our checks (docs/constitution.md, «Честность»): a check that is
claimed to hold a law is worth something only if breaking the law makes it fail. Each mutant below breaks one claimed
law in one line; the tool puts it into a separate git worktree (the working tree is never touched), builds there, runs
the test that claims the law and says whether the test caught the break (killed) or not (survived: the claim is not
checked, whatever the constitution's mark says). The control comes first: each claiming test must pass on the unmutated
tree, or nothing is judged.

    python3 tools/mutate.py --tree /tmp/mut          # a worktree of HEAD there (made once), every mutant
    python3 tools/mutate.py --tree /tmp/mut undo     # the mutants whose name has "undo"

One line a mutant goes to <tree>/mutants.jsonl (appended, never rewritten). Standard library only; the build is the
project's (CMake, Ninja), at most one hidden editor window at a time (--self-test)."""
import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# name, the law it breaks (the constitution's article), file, the exact text, what replaces it, the test that claims the law:
# ("core", KE_TEST substring) runs build/ke_tests; ("window", KE_EDITOR_TEST group) runs build/kinetica --self-test.
MUTANTS = [
    ("undo-keeps-edit", "6: Ctrl+Z gives the scene back", "src/scene/Document.cpp",
     "    current = std::move(past_.back());\n", "", ("core", "document")),
    ("schema-takes-unknown", "4: an unknown argument is refused with its name", "src/scene/ArgsSchema.cpp",
     "    if (!properties.contains(key)) {", "    if (false) {", ("core", "args")),
    ("loop-ignored", "13/I: a looping sequence's keys repeat (samplePose)", "src/scene/Animation.cpp",
     "    if (loop && length > 0) time = time - length * std::floor(time / length);", "", ("core", "the Sequencer")),
    ("plugin-name-cyrillic", "7: a plugin's name is its folder in Latin letters (slug)", "editor/PluginHost.cpp",
     "        if (c >= QChar(0x0430) && c <= QChar(0x044F)) out += latin[c.unicode() - 0x0430];",
     "        if (c >= QChar(0x0430) && c <= QChar(0x044F)) out += c;", ("window", "plugin")),
    ("catalog-ignored", "11: the window shows a plugin's labels in its catalog's words", "editor/PluginHost.cpp",
     "it.value() = word(it.value());", "it.value() = it.value();", ("window", "plugin")),
    ("view-words-hidden", "13: what is not the plain view is said, with a way back", "editor/PlotPanel.cpp",
     "    stateBar_->setVisible(!words.isEmpty());", "    stateBar_->setVisible(false);", ("window", "plugin")),
    ("reset-view-noop", "13: «Сбросить вид» gives the plain view", "editor/PlotPanel.cpp",
     "    setView(plain);", "    (void)plain;", ("window", "plugin")),
    ("wheel-no-zoom", "plots: the wheel zooms the time about the mouse", "editor/TrackChart.cpp",
     "    const double k = std::pow(0.998, turn);", "    const double k = 1.0;", ("window", "plugin")),
    ("zoom-not-kept", "13: the reader's window stays while the data come", "editor/TrackChart.cpp",
     "    if (!userX_) t0_ = autoT0_, t1_ = autoT1_;", "    t0_ = autoT0_, t1_ = autoT1_;", ("window", "plugin")),
    ("take-never-ends", "a take ends at the play range's end", "editor/EditorOpsRecord.cpp",
     "    builder_->setTaking(on);", "    builder_->setTaking(false);", ("window", "sequencer")),
    ("exit-signals-kept", "15/I: the window's exit calls nothing of a destroyed window (UB, seen by ASan only)", "editor/MainWindow.cpp",
     "    for (QObject* child : findChildren<QObject*>()) child->disconnect(this);", "", ("window", "plugin")),
]


def sh(cmd, cwd, env=None, timeout=3600):
    """A command's exit code and its output's tail; never raises on failure."""
    import os
    full = dict(os.environ, **(env or {}))
    try:
        p = subprocess.run(cmd, cwd=cwd, env=full, capture_output=True, text=True, timeout=timeout)
        return p.returncode, (p.stdout + p.stderr)[-3000:]
    except subprocess.TimeoutExpired:
        return -1, "timed out"


def prepare(tree):
    """A worktree of HEAD at `tree` with its submodules and its own build: made once, moved to HEAD on every run (a stale
    tree would judge old code). The repository keeps a note of it in .git/worktrees: `git worktree prune` after the tree
    is gone."""
    if not (tree / ".git").exists():
        code, out = sh(["git", "worktree", "add", "--detach", str(tree), "HEAD"], ROOT)
        if code:
            sys.exit("worktree: " + out)
    head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    code, out = sh(["git", "checkout", "--detach", "--force", head], tree)
    if code:
        sys.exit("worktree to HEAD: " + out)
    sh(["git", "submodule", "update", "--init"], tree)
    build = tree / "build"
    if not (build / "build.ninja").exists():
        qt = Path.home() / "Qt/6.7.3/gcc_64"
        code, out = sh(["cmake", "-S", ".", "-B", "build", "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_PREFIX_PATH={qt}", "-DKE_TESTS=ON"], tree)
        if code:
            sys.exit("cmake: " + out)
    code, out = sh(["cmake", "--build", "build", "-j", "8"], tree, timeout=7200)
    if code:
        sys.exit("the unmutated tree does not build: " + out)


def check(tree, kind, which):
    """The claiming test's exit code on the tree as it stands: 0 - passed."""
    if kind == "core":
        return sh(["build/ke_tests"], tree, {"KE_TEST": which} if which else None)
    return sh(["build/kinetica", "--self-test"], tree, {"KE_EDITOR_TEST": which, "KE_PLUGINS": ""})


def mutate(tree, mutant):
    name, law, file, text, into, (kind, which) = mutant
    path = tree / file
    source = path.read_text(encoding="utf-8")
    if source.count(text) != 1:
        return dict(name=name, law=law, outcome="stale", why=f"the text is in {file} {source.count(text)} times, not once")
    path.write_text(source.replace(text, into), encoding="utf-8")
    try:
        started = time.time()
        code, out = sh(["cmake", "--build", "build", "-j", "8"], tree, timeout=7200)
        if code: # a mutant that does not build says nothing of the test: not counted
            return dict(name=name, law=law, outcome="invalid", why="does not build", tail=out.strip().splitlines()[-3:])
        code, out = check(tree, kind, which)
        how = "survived" if code == 0 else "killed" if code > 0 else "crashed"  # crashed: a signal - killed, by a crash
        return dict(name=name, law=law, outcome=how, by=f"{kind}:{which or 'all'}", code=code,
                    seconds=round(time.time() - started), tail=out.strip().splitlines()[-4:])
    finally:
        path.write_text(source, encoding="utf-8") # the worktree back as HEAD has it, whatever happened


def main(argv=None):
    a = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    a.add_argument("--tree", required=True, help="where the worktree lives (made once)")
    a.add_argument("only", nargs="*", help="run the mutants whose name has one of these words")
    options = a.parse_args(argv)
    tree = Path(options.tree).resolve()
    prepare(tree)
    chosen = [m for m in MUTANTS if not options.only or any(w in m[0] for w in options.only)]
    # The control first: every claiming test passes on the tree as HEAD has it - else a «killed» says nothing (it would
    # have failed unmutated too).
    for kind, which in sorted({m[5] for m in chosen}):
        code, out = check(tree, kind, which)
        if code:
            sys.exit(f"the control failed - {kind}:{which or 'all'} does not pass unmutated, so no mutant can be judged:\n{out}")
        if out.count("[ OK ]") == 0: # a filter that matches nothing passes empty: no control at all
            sys.exit(f"the control is empty - {kind}:{which or 'all'} ran no check")
        print(f"control   {kind}:{which or 'all'} passes unmutated", flush=True)
    killed = judged = 0
    with (tree / "mutants.jsonl").open("a", encoding="utf-8") as log:
        for m in chosen:
            r = mutate(tree, m)
            r["at"] = time.strftime("%Y-%m-%dT%H:%M:%S")
            log.write(json.dumps(r, ensure_ascii=False) + "\n")
            log.flush()
            killed += r["outcome"] in ("killed", "crashed")
            judged += r["outcome"] != "invalid"
            print(f"{r['outcome']:9} {r['name']:22} {r['law']}", flush=True)
    sh(["cmake", "--build", "build", "-j", "8"], tree) # the tree's build back to HEAD's
    print(f"mutation score: {killed} of {judged} killed ({len(chosen) - judged} did not build: not counted)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
