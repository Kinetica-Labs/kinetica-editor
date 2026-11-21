"""The transport to the windowless host: the program ``kinetica-core`` started as a child process, one JSON line in
and one out on its pipes (``{"id": n, "op": name, "args": {...}}`` / ``{"id": n, "result": {...}}``), the same
operations as the editor's server. ``Core.call`` has the signature of ``Client.call``, so ``Scene`` takes either.

The program answers in order, one line per request, so there is no reader thread: a call writes its line and reads
the next. Its stderr is drained into a short tail that explains a death; the parent's death closes its stdin and
the program ends by itself.
"""
import collections
import json
import os
import shutil
import subprocess
import threading

from .client import check

KE_CORE = "KE_CORE"
EXE = "kinetica-core" + (".exe" if os.name == "nt" else "")


def find_core(executable=None):
    """The program's path: the argument, KE_CORE, beside the package (a bundle), build/ of the source tree, PATH."""
    given = executable or os.environ.get(KE_CORE)
    if given:
        if not (os.path.isfile(given) and os.access(given, os.X_OK)):
            raise FileNotFoundError(f"kinetica-core: no such executable file {given!r}" + (f" (from {KE_CORE})" if not executable else ""))
        return given
    here = os.path.dirname(os.path.abspath(__file__))
    beside = os.path.dirname(os.path.dirname(here))
    places = [os.path.join(beside, EXE), os.path.join(beside, "build", EXE), os.path.join(beside, "build", "Release", EXE)]
    found = next((p for p in places if os.path.isfile(p)), None) or shutil.which(EXE)
    if not found:
        raise FileNotFoundError("kinetica-core is not built: run `cmake --build build --target kinetica-core`, or set "
                                f"{KE_CORE}=/path/to/{EXE} (looked in: {', '.join(places)}, PATH)")
    return found


class Core:
    def __init__(self, executable=None):
        self._proc = subprocess.Popen([find_core(executable)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                      stderr=subprocess.PIPE, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        self._tail = collections.deque(maxlen=20)
        self._lock = threading.Lock()
        self._next = 0
        self._owed = 0
        self._closed = False
        self._drainer = threading.Thread(target=self._drain, name="kinetica-core-stderr", daemon=True)
        self._drainer.start()

    def _drain(self):
        for line in self._proc.stderr:
            self._tail.append(line.decode("utf-8", "replace").rstrip())

    def _gone(self):
        code = self._proc.wait()
        return ConnectionError(f"kinetica-core exited (code {code})" + "".join(f": {t}" for t in self._tail))

    def call(self, op, py=None, **args):
        """The answer of one operation (raises OperationError when it failed); `py` is the window console's line: not sent."""
        with self._lock:
            if self._closed:
                raise ConnectionError("kinetica-core was closed")
            while self._owed:
                self._read()
                self._owed -= 1
            self._next += 1
            line = json.dumps({"id": self._next, "op": op, "args": args}, ensure_ascii=False) + "\n"
            try:
                self._proc.stdin.write(line.encode("utf-8"))
                self._proc.stdin.flush()
            except OSError:
                raise self._gone() from None
            self._owed = 1
            envelope = self._read()
            self._owed = 0
            # a line the program could not parse is answered without an id: it is this request's answer
            if envelope.get("id", self._next) != self._next:
                raise ConnectionError("kinetica-core answered out of order")
            return check(op, envelope.get("result", {}))

    def _read(self):
        reply = self._proc.stdout.readline()
        if not reply:
            raise self._gone()
        return json.loads(reply)

    def close(self):
        with self._lock:
            if self._closed:
                return
            self._closed = True
            try:
                self._proc.stdin.close()
            except OSError:
                pass  # the child is gone; the rest of close still reaps it
            try:
                self._proc.wait(5)
            except subprocess.TimeoutExpired:
                self._proc.kill()
                self._proc.wait()
            self._drainer.join()
            self._proc.stdout.close()
            self._proc.stderr.close()
