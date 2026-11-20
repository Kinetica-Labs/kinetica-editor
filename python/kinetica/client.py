"""The wire under the object model: one connection to a server of Kinetica's operations - the
editor started with ``--api <port>`` (KE_EDITOR_PORT, else 47801) - as JSON lines on 127.0.0.1:
``{"id": n, "op": name, "args": {...}}`` in, ``{"id": n, "result": {"ok": ..., ...}}`` out.

Every call may carry ``_py``, the line of the object model that made it (``scene.objects["Шасси"]
.add_sonar(face="+x")``): the editor's API console shows that line instead of the bare operation, so
what a script or an agent does reads as code a person would write.

The connection is shared: calls from several threads go out on it at once, a reader thread hands each
answer to the call that asked (by its id) - a plugin's loop waiting for the editor's next call and its
own worker asking the editor something do not take each other's answers, and one plugin needs one
connection of the editor's few.
"""
import json
import os
import socket
import threading

DEFAULT_PORT = int(os.environ.get("KE_EDITOR_PORT", "47801"))
WAITS = {"plugin_wait"}  # held by the editor until it has something to say: no time limit


class OperationError(RuntimeError):
    """An operation answered ``{"ok": false, "error": ...}``."""


class Client:
    def __init__(self, host="127.0.0.1", port=None, timeout=120.0, src=None):
        self._sock = socket.create_connection((host, port or DEFAULT_PORT), timeout=timeout)
        self._sock.settimeout(None)  # the reader waits as long as the connection lives; each call keeps its own limit
        self._timeout = timeout
        self._file = self._sock.makefile("rb")
        self._send = threading.Lock()
        self._next = 0
        self._waiting = {}  # call id -> [answered, envelope]
        self._gone = None   # why the connection ended, once it has
        self._src = src  # "console": the editor's own Python console - its log shows the calls as the user's
        threading.Thread(target=self._read, name="kinetica-client", daemon=True).start()

    def _read(self):
        try:
            for line in self._file:
                envelope = json.loads(line)
                slot = self._waiting.get(envelope.get("id"))
                if slot:
                    slot[1] = envelope
                    slot[0].set()
        except (OSError, ValueError) as error:
            self._gone = f"the connection to the editor broke: {error}"
        self._gone = self._gone or "the editor closed the connection"
        for slot in list(self._waiting.values()):
            slot[0].set()

    def call(self, op, py=None, **args):
        """The answer of one operation (raises OperationError when it failed); `py` is the line to show."""
        if py:
            args["_py"] = py
        slot = [threading.Event(), None]
        with self._send:
            if self._gone:
                raise ConnectionError(self._gone)
            self._next += 1
            ident = self._next
            self._waiting[ident] = slot
            envelope = {"id": ident, "op": op, "args": args, **({"src": self._src} if self._src else {})}
            self._sock.sendall((json.dumps(envelope, ensure_ascii=False) + "\n").encode("utf-8"))
        answered = slot[0].wait(None if op in WAITS else self._timeout)
        self._waiting.pop(ident, None)
        if not answered:
            raise TimeoutError(f"{op}: no answer from the editor in {self._timeout} s")
        if slot[1] is None:
            raise ConnectionError(self._gone or "the editor closed the connection")
        answer = slot[1].get("result", {})
        if not answer.get("ok"):
            raise OperationError(f"{op}: {answer.get('error')}")
        return answer

    def close(self):
        self._sock.close()
