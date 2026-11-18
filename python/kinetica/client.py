"""The wire under the object model: one connection to a server of Kinetica's operations - the
editor started with ``--api <port>`` (KE_EDITOR_PORT, else 47801) - as JSON lines on 127.0.0.1:
``{"id": n, "op": name, "args": {...}}`` in, ``{"id": n, "result": {"ok": ..., ...}}`` out.

Every call may carry ``_py``, the line of the object model that made it (``scene.objects["Шасси"]
.add_sonar(face="+x")``): the editor's API console shows that line instead of the bare operation, so
what a script or an agent does reads as code a person would write.
"""
import json
import os
import socket

DEFAULT_PORT = int(os.environ.get("KE_EDITOR_PORT", "47801"))


class OperationError(RuntimeError):
    """An operation answered ``{"ok": false, "error": ...}``."""


class Client:
    def __init__(self, host="127.0.0.1", port=None, timeout=120.0, src=None):
        self._sock = socket.create_connection((host, port or DEFAULT_PORT), timeout=timeout)
        self._file = self._sock.makefile("rb")
        self._next = 0
        self._src = src  # "console": the editor's own Python console - its log shows the calls as the user's

    def call(self, op, py=None, **args):
        """The answer of one operation (raises OperationError when it failed); `py` is the line to show."""
        if py:
            args["_py"] = py
        self._next += 1
        envelope = {"id": self._next, "op": op, "args": args, **({"src": self._src} if self._src else {})}
        self._sock.sendall((json.dumps(envelope, ensure_ascii=False) + "\n").encode("utf-8"))
        while True:
            line = self._file.readline()
            if not line:
                raise ConnectionError("the server closed the connection")
            envelope = json.loads(line)
            if envelope.get("id") == self._next:
                break
        answer = envelope.get("result", {})
        if not answer.get("ok"):
            raise OperationError(f"{op}: {answer.get('error')}")
        return answer

    def close(self):
        self._sock.close()
