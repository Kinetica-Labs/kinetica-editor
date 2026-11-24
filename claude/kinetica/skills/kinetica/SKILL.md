---
name: kinetica
description: Build, drive and test robots in the Kinetica physics editor through its MCP tools (ke-editor) - a robot arm, a car, a drone; measure them, explain them with formulas and film it. Use when asked to build or simulate a robot, a mechanism or a scene in Kinetica, to teach with one, or to make a video of a model building it.
---

# Kinetica Editor

Kinetica is a desktop robotics editor on rigid-body physics (Box3D). Its MCP server `ke-editor` gives every operation of
the editor as a tool; its instructions (sent when it connects) are the short guide - read them first.

1. **The editor running.** If a tool says the editor is not reachable, call `editor_launch` (it starts the program and
   waits). The tools then act in the person's window: they see each call.
2. **The biggest step first.** A ready robot is one call (`assembly_list`, `assembly_add`); ground is one call
   (`terrain_make`); a new robot is one `scene_batch`. Before building a kind of robot read its topic: `guide {topic}`
   - `arm`, `car`, `power`, `film`.
3. **Check by running.** `sim_play`, `sim_step`, `sim_measure`, `sim_stop` - numbers with units, not looks.
4. **Explain and film** (topic `film`): `narration_say` puts a step's card in the view and flies the camera to the part,
   `formula_callout` labels a part with a formula as a drawing does, `narration_stage` lights the scene.

Plugins of the editor (the formulas, the narration, chess, the depz sensors) come from the kinetica-plugins repository;
an operation a plugin gives is `<plugin>_<command>` (`formula_callout`, `chess_move`).
