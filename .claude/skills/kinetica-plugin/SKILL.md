---
name: kinetica-plugin
description: Write, run and fix a Python plugin for the Kinetica robotics editor - a new command with fields that becomes a button, a console call and an MCP tool. Use when asked to add a command, a tool or an automation to the Kinetica editor, or to turn a repeated chain of editor operations into one command.
---

# Writing a Kinetica plugin

A plugin is one file `plugin.py` in its own folder; each `@command` function becomes an operation of the editor (a
button in «Плагины», Ctrl+K, a console call, an MCP tool with a strict schema). Guide for people: `docs/plugins.md`;
contract: `docs/contracts/editor-plugin.md`; SDK: `python/kinetica/plugin.py`; examples: the project's plugins repository kinetica-plugins (`cubes/plugin.py`); new plugins of the project go there, one folder each.

## Steps

1. **Make it**: `plugin_new {"name": "<snake_case>"}` (MCP tool or `python3 tools/ke_editor.py plugin_new '{"name": "x"}'`).
   The answer has the file's path; the template is already running with one command.
2. **Find the operations you need**: `ops_list` (names, help, arguments). Prefer one operation over many
   (`scene_batch`, `scene_move` with `ids`). Read the state with `scene_list`, `selection_get`, `sim_state`.
3. **Write the file** with your file tools. Shape:
   ```python
   from kinetica.plugin import command, field, run

   @command(label="Words on the button", key="Ctrl+Shift+X")
   def name(scene, arg: float = field(0.1, unit="м", range=(0.0, 1.0), label="Подпись")):
       """One sentence: what it does."""
       answer = scene.client.call("scene_list")
       return {"done": True}  # a dict, shown to the caller

   run()
   ```
   Labels and words people see are Russian; code and comments English. Every argument after `scene` needs a
   default (a `field` or a plain value). Return a dict.
4. **Saving restarts it** (about 0.2 s). Check `plugin_list`: its state must be `работает`; otherwise `error`
   says `plugin.py:<line>: <exception>` - fix that line.
5. **Call it**: the operation is `<plugin>_<function>`; arguments are checked against the fields before the call
   (out of range or unknown: refused with the reason). An error inside answers `plugin.py:<line>: ...`.
6. **Undo**: whatever one call changes is one Ctrl+Z (`edit_undo`) - verify by counting `scene_list` before/after.

## Rules

- Change the scene only through operations (`scene.client.call`), never by other means.
- A robot's controller sees sensors, not the truth (CLAUDE.md, «Контракт наблюдаемости»): a plugin that steers a robot
  reads `sensor_*` readings, not `sim_state` poses.
- No third-party packages: the standard library only.
