"""The editor's Python console, a process of its own: python3 -u -m kinetica.console

As DaVinci Resolve's console runs its scripts in a Python of the system, and Blender's console has the
whole bpy at hand: each line on stdin is run with the object model connected to the editor that started
this process (KE_EDITOR_PORT) - `scene`, `editor` (scene.editor), `ke` (this package), and `ed` that calls any operation
by name (ed.scene_list(), ed.assembly_sensor(target=166, kind="sonar")). An expression's value is printed;
a statement (an assignment, a loop on one line) is run. After each line a record separator (\\x1e) alone on
a line tells the editor the answer is complete. The calls a line makes reach the editor marked as the
console's, so its log shows them as the user's.
"""
import os
import sys
import traceback

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import kinetica as ke  # noqa: E402
from kinetica.client import Client  # noqa: E402
from kinetica.scene import Scene  # noqa: E402

DONE = "\x1e"


class Operations:
    """ed.<operation>(**args): any operation of the editor by its name, as the console's old line had it."""

    def __init__(self, scene):
        self._scene = scene

    def __getattr__(self, name):
        return lambda **args: self._scene._call(name, **args)


def main():
    scene = Scene(Client(port=int(os.environ.get("KE_EDITOR_PORT", "47801")), src="console"))
    names = {"ke": ke, "scene": scene, "editor": scene.editor, "ed": Operations(scene)}
    for line in sys.stdin:
        line = line.rstrip("\n")
        try:
            try:
                code = compile(line, "<console>", "eval")
            except SyntaxError:
                exec(compile(line, "<console>", "exec"), names)
            else:
                value = eval(code, names)
                if value is not None:
                    print(repr(value))
        except Exception as e:  # noqa: BLE001 - the console says what went wrong and goes on
            print("✗ " + (traceback.format_exception_only(type(e), e)[-1].strip()))
        print(DONE, flush=True)


if __name__ == "__main__":
    main()
