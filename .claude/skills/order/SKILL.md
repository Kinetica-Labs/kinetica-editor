---
name: order
description: Keep the Kinetica editor's order against entropy - run the project map (tools/project_map.py), bring its rules to zero, and propose changes that strengthen what exists (merge, move, delete) with numbers before and after. Use when asked to tidy or clean up the project, fight chaos or entropy, find junk, duplicates or dead code, check the structure, show the project map, or before committing a large batch.
---

# Order in the project

Lehman's second law: a growing system grows more complex unless work is spent to keep it simple. This is that work,
done the way Christopher Alexander describes order: each change strengthens a structure that is already there - it
merges, moves into its layer, links to the one place a fact lives - rather than adding something beside it.

## Steps

1. **Measure.** `python3 tools/project_map.py` - the rules first, then the report. Keep its numbers: they are the
   «before».
2. **Rules to zero** (CI holds them: `tools/test_project_map.py`). Fix the cause, never the measure:
   - *layers* - an include of `src/` reaching up: move the thing into the layer it belongs to (data of the scene
     lives in `scene/`), or turn the dependency around;
   - *budgets* - a file over 1600 lines: split it along a seam it already has (one topic per file, shared helpers in
     a header of their own); `CLAUDE.md` over its size: move what code says by itself out of it;
   - *readers* - a file no one reads: delete it, or name it where it is used. Add to `READ_BY` only a real reader
     that finds files by a pattern, and say which.
   Raising a budget or a threshold to silence a rule needs the user's word.
3. **The report:**
   - *repeated sentences* - keep the fact in one place and link to it: `CLAUDE.md` for what every session needs,
     `docs/contracts/` for reference, the guides for lessons (Diátaxis: tutorial, how-to, reference, explanation);
   - *operations no test names* - a line in `TRANSCRIPT` of `tools/record_window_transcript.py` (re-record, then
     `--check`; the window's answer becomes the oracle, kinetica-core is held to it), or a C++ test; an operation
     nothing calls is a candidate for removal - names only grow (`docs/contracts/names.md`), so ask first;
   - *deleted per added* - low means the project only grows: look for what the last changes made dead;
   - *hubs* (`--json`, `report.hubs`) - files many name: a change there reaches far; keep them small and stable.
4. **Propose before acting** on anything beyond the rules: a short list - strengthen / merge / move / delete - each
   with its path and its number. Deleting a file, a public name or a user's local file (`reports/`,
   `research_notes/`, anything git ignores) waits for the user's yes.
5. **Check and show.** The tests of `CLAUDE.md`, the map again, and «before → after» in numbers; after a change to
   operations also `python3 tools/op_algebra.py --json <scratchpad>/algebra.json` (every edit and pair: undo is the
   inverse, save is an iso, answers are words). For a person: `python3 tools/project_map.py --html
   <scratchpad>/kinetica-map.html --algebra <scratchpad>/algebra.json` and publish it with the Artifact tool (the same
   file path keeps the page's link).
