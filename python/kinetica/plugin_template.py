"""PLUGIN_NAME - a plugin of the Kinetica editor (Плагины → Новый плагин… made this file).

Each @command below is a button in «Плагины» (with its key), a line in Ctrl+K, a call in the console
(ed.PLUGIN_NAME_lift(height=0.2)), an operation for scripts and a tool for AI agents over MCP. Save the file:
the editor restarts the plugin within a second. An error shows its line here, and the editor goes on.
`scene` is the editor's object model - as in its console: scene.objects, scene.add("box"), scene.editor.
"""
from kinetica.plugin import command, field, run


@command(label="Поднять выделенное", key="Ctrl+Shift+U")
def lift(scene, height: float = field(0.1, unit="м", range=(-1.0, 1.0), label="На сколько")):
    """Lift the selected objects by `height` - one undo step, however many they are."""
    ids = scene.client.call("selection_get")["ids"]
    if ids:
        scene.client.call("scene_move", ids=ids, by=[0, height, 0])
    return {"moved": len(ids)}


run()
