"""The Claude plugin's door to Kinetica: finds the installed editor and runs its own MCP server (tools/ke_editor_mcp.py,
the operations, the agent's guide) on this process's stdin and stdout. The plugin folder is copied into Claude's cache
and may hold nothing of the editor's: the editor is found where install.sh / install.ps1 put it (~/Kinetica/editor,
on Windows %LOCALAPPDATA%\\Kinetica\\editor), or in KINETICA_HOME (the plugin's «Kinetica folder»), or a source
checkout given there. Not found: a small server of its own says how to install it, so the model can tell the person.
Only the standard library."""
import json
import os
import subprocess
import sys

# Where a release or a checkout keeps tools/: Windows (beside kinetica.exe), Linux (the bundle), macOS (the app), a checkout.
LAYOUTS = (("tools",), ("kinetica-windows", "tools"), ("kinetica-linux", "usr", "bin", "tools"),
           ("kinetica.app", "Contents", "Resources", "tools"))
INSTALL = ("Kinetica is not installed here. Linux, macOS: curl -fsSL "
           "https://raw.githubusercontent.com/Kinetica-Labs/kinetica-editor/main/install.sh | bash; Windows (PowerShell): "
           "irm https://raw.githubusercontent.com/Kinetica-Labs/kinetica-editor/main/install.ps1 | iex - or set the plugin's "
           "«Kinetica folder» to where it is. Then restart Claude.")


def homes():
    if os.environ.get("KINETICA_HOME"):
        yield os.environ["KINETICA_HOME"]
    local = os.environ.get("LOCALAPPDATA")
    yield os.path.join(local, "Kinetica", "editor") if local else os.path.join(os.path.expanduser("~"), "Kinetica", "editor")


def server():
    """The editor's tools/ke_editor_mcp.py, or None."""
    for home in homes():
        for layout in LAYOUTS:
            path = os.path.join(home, *layout, "ke_editor_mcp.py")
            if os.path.isfile(path):
                return path
    return None


def explain():
    """No editor: a server that answers MCP's start with how to install it, and has no tools."""
    for line in sys.stdin:
        try:
            msg = json.loads(line)
        except ValueError:
            continue
        if "id" not in msg:
            continue
        result = {"tools": []} if msg.get("method") == "tools/list" else {
            "protocolVersion": msg.get("params", {}).get("protocolVersion", "2025-06-18"), "capabilities": {"tools": {}},
            "instructions": INSTALL, "serverInfo": {"name": "ke-editor", "version": "0"}} if msg.get("method") == "initialize" else {}
        sys.stdout.write(json.dumps({"jsonrpc": "2.0", "id": msg["id"], "result": result}) + "\n")
        sys.stdout.flush()


def main():
    path = server()
    if not path:
        print(f"kinetica plugin: {INSTALL}", file=sys.stderr)
        return explain()
    sys.exit(subprocess.call([sys.executable, path]))  # the same stdin and stdout: the editor's server speaks to Claude


if __name__ == "__main__":
    main()
