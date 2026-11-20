---
name: kinetica-plugin
description: Write, run and fix a Python plugin for the Kinetica robotics editor - a new command with fields that becomes a button, a console call and an MCP tool. Use when asked to add a command, a tool or an automation to the Kinetica editor, or to turn a repeated chain of editor operations into one command.
---

# Writing a Kinetica plugin

A plugin is one file `plugin.py` in its own folder; each `@command` function becomes an operation of the editor (a
button in «Плагины → <plugin>», Ctrl+K, a console call, an MCP tool with a strict schema). A plugin gives
commands, a panel and per-frame hooks: `@on_tick def control(scene, dt, sensors)` - a robot's controller, its sensors
as the robot reads them (no truth) in, `{joint: {"target": x}}` out; `@on_tick(world=True) def f(scene, dt, bodies)` -
an extension of the physics, the bodies in, `{body: {"force": [x, y, z]}}` out; `@on_play` / `@on_stop`.
`plugin_new {name, kind: command | controller | world}` gives a working file of each. Components with orange handles
(`@component`, `field(handle=...)`), tools in the view (`@command(tool=True)`: `point`, `normal`, `object` from a
click) and drawing (`draw.line/segments/points/label/clear`, op `view_draw`) are there; importers too (`@command(opens=".urdf")`: «Файл → Импорт…» and a file dropped on the view
call it with `path`; plugins/urdf is the editor's own example) (`docs/contracts/editor-plugin.md`, «Дальше»). Guide for people: `docs/plugins.md`;
contract: `docs/contracts/editor-plugin.md`; SDK: `python/kinetica/plugin.py`; examples: the project's plugins repository kinetica-plugins (`cubes/plugin.py`); new plugins of the project go there, one folder each.

## Steps

1. **Make it**: `plugin_new {"name": "<snake_case>"}` (MCP tool or `python3 tools/ke_editor.py plugin_new '{"name": "x"}'`).
   The answer has the file's path; the template is already running with one command. A folder with a `plugin.py` put
   into a plugins' folder by hand (`plugin_list` names them in `folders`) is started by `plugin_scan`.
2. **Find the operations you need**: `ops_list` (names, help, arguments). Prefer one operation over many
   (`scene_batch`, `scene_move` with `ids`). Read the state with `scene_list`, `selection_get`, `sim_state`.
3. **Write the file** with your file tools. Shape:
   ```python
   from kinetica.plugin import command, field

   @command(label="Words on the button")
   def name(scene, arg: float = field(0.1, unit="м", range=(0.0, 1.0), label="Подпись")):
       """One sentence: what it does."""
       objects = scene.ed.scene_list()["objects"]  # any operation: scene.ed.<op>(...), as ed.<op>(...) in the console
       print("seen", len(objects))                # print() goes to the editor's API console
       return {"done": True}                      # a dict, shown to the caller
   ```
   The editor starts the file itself: nothing to call at its end. Use the object model where it has a word
   (`scene.add("box", role="rigid")`, `scene.move(...)`, `scene.objects[...]`) and `scene.ed.<op>(...)` for the rest -
   there is no other way to the scene. A `key="Ctrl+Shift+X"` is optional; a key already taken
   is not given (the console says whose it is).
   An argument that is an object of the scene: `joint=field(ref="joint", label="Сустав")` - the form lists the scene's
   joints by name, a call gives the id, the function gets the object (`joint.drive(target=0.5)`); the word is a kind of
   `scene_list {kind}` (`joint`, `body`, `encoder`, `sensor`, `camera`...). It has no default: the call must say it.
   Labels and words people see are Russian; code and comments English. Every other argument after `scene` needs a
   default (a `field` or a plain value). Return a dict.
4. **Saving restarts it** (about 0.2 s). Check `plugin_list`: its state must be `работает`; otherwise `error`
   says `plugin.py:<line>: <exception>` - fix that line.
5. **Call it**: the operation is `<plugin>_<function>`; arguments are checked against the fields before the call
   (out of range or unknown: refused with the reason). An error inside answers `plugin.py:<line>: ...`.
6. **Undo**: whatever one call changes is one Ctrl+Z (`edit_undo`) - verify by counting `scene_list` before/after.

## Rules

- Change the scene only through operations (`scene.ed.<op>(...)` or the object model), never by other means.
- A robot's controller sees sensors, not the truth (CLAUDE.md, «Контракт наблюдаемости»): write it as `@on_tick` (or
  read `sensor_read`); `sim_state` is the engine's truth, for checking only.
- No third-party packages: the standard library only.
