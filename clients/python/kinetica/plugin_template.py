"""PLUGIN_NAME - a plugin of the Kinetica editor (Плагины → Новый плагин… made this file).

Each @command below is a button in «Плагины → PLUGIN_NAME», a line in Ctrl+K, a call in the console
(ed.PLUGIN_NAME_lift(height=0.2)), an operation for scripts and a tool for AI agents over MCP. Save the file: the
editor restarts the plugin within a second. An error shows its line here, and the editor goes on; print() goes to the
editor's API console. `scene` is the editor's object model, as in its console: scene.objects, scene.add("box"),
scene.editor.selection, and scene.ed.<operation>(...) for any operation (the console's ed).
"""
import os

from kinetica.plugin import command, field

ENGLISH = os.environ.get("KE_UI_LANGUAGE") == "en"


@command(label="Lift Selection" if ENGLISH else "Поднять выделенное")
def lift(scene, height: float = field(0.1, unit="m" if ENGLISH else "м", range=(-1.0, 1.0),
                                    label="Height" if ENGLISH else "На сколько")):
    """Lift the selected objects by `height` - one undo step, however many they are."""
    chosen = scene.editor.selection
    if chosen:
        scene.move(*chosen, by=(0, height, 0))
    return {"moved": len(chosen)}
