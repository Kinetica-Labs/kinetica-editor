#!/usr/bin/env python3
"""A client of the Kinetica editor's API (only the Python standard library).

The editor started with ``--api <port>`` serves its operations (editor/EditorOps.h) on 127.0.0.1 as
JSON lines. Every button of the editor, its magnet and this client call the same operations::

    from ke_editor import Editor, launch
    launch()                                  # build/kinetica --api 47801, if it is not running yet
    ed = Editor()                             # 127.0.0.1, port from KE_EDITOR_PORT or 47801
    ed.scene_new()
    base = ed.scene_put(block='entity "База"\\n  shape box size 0.3 0.1 0.3 position 0 0.5 0'
                              ' rotation 0 0 0 color 0.6 0.6 0.6\\n  rigid density 1000 fixed 1\\nend\\n')["id"]
    arm = ed.assembly_attach(target=base, face="+y", joint="hinge", axis=0)
    ed.sim_step(frames=60)["joints"]          # the hinge's angle after a second
    ed.view_screenshot(file="/tmp/arm.png")

From the shell: ``python3 tools/ke_editor.py [--port N] <op> ['{"json": "args"}']`` prints the answer;
``python3 tools/ke_editor.py ops_list`` names every operation.
"""
import json
import os
import socket
import subprocess
import sys
import time

DEFAULT_PORT = int(os.environ.get("KE_EDITOR_PORT", "47801"))
EDITOR_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


class EditorError(RuntimeError):
    """An operation answered {"ok": false, "error": ...}."""


class Editor:
    """One connection to a running editor; any operation is a method: ``ed.scene_list()``."""

    def __init__(self, host="127.0.0.1", port=DEFAULT_PORT, timeout=120.0):
        self._sock = socket.create_connection((host, port), timeout=timeout)
        self._file = self._sock.makefile("rb")
        self._next = 0

    def call(self, op, **args):
        """The answer of one operation; raises EditorError when it says it failed."""
        self._next += 1
        request = {"id": self._next, "op": op, "args": args}
        self._sock.sendall((json.dumps(request, ensure_ascii=False) + "\n").encode("utf-8"))
        while True:
            line = self._file.readline()
            if not line:
                raise ConnectionError("the editor closed the connection")
            envelope = json.loads(line)
            if envelope.get("id") == self._next:
                break
        answer = envelope.get("result", {})
        if not answer.get("ok"):
            raise EditorError(f"{op}: {answer.get('error')}")
        return answer

    def __getattr__(self, name):
        if name.startswith("_"):
            raise AttributeError(name)
        return lambda **args: self.call(name, **args)

    def close(self):
        self._sock.close()


def reachable(port=DEFAULT_PORT):
    """Whether an editor answers on the port."""
    try:
        socket.create_connection(("127.0.0.1", port), timeout=0.5).close()
        return True
    except OSError:
        return False


def launch(port=DEFAULT_PORT, scene=None, display=None, wait=60.0):
    """Starts build/kinetica with --api (unless one already answers on the port) and waits for it."""
    if reachable(port):
        return None
    cmd = [os.path.join(EDITOR_DIR, "build", "kinetica"), "--api", str(port)]
    if scene:
        cmd += ["--scene", scene]
    env = dict(os.environ)
    env.setdefault("DISPLAY", display or ":0")
    proc = subprocess.Popen(cmd, cwd=EDITOR_DIR, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                            start_new_session=True)
    deadline = time.time() + wait
    while time.time() < deadline:
        if reachable(port):
            return proc
        if proc.poll() is not None:
            raise RuntimeError(f"the editor exited with code {proc.returncode}")
        time.sleep(0.2)
    raise TimeoutError(f"the editor did not open port {port} within {wait} s")


def main(argv):
    port = DEFAULT_PORT
    if len(argv) > 2 and argv[1] == "--port":
        port, argv = int(argv[2]), argv[:1] + argv[3:]
    if len(argv) < 2:
        print(__doc__)
        return 2
    args = json.loads(argv[2]) if len(argv) > 2 else {}
    try:
        print(json.dumps(Editor(port=port).call(argv[1], **args), ensure_ascii=False, indent=1))
    except (EditorError, OSError) as e:
        print(e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
