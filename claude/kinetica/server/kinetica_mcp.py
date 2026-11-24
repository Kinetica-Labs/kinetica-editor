"""The plugin's door to Kinetica, in Claude (.claude-plugin/plugin.json) and in Codex and ChatGPT (plugin.json and
mcp.json, the Agent Plugins schema): finds the installed editor and runs its own MCP server (tools/ke_editor_mcp.py,
the operations, the agent's guide) on this process's stdin and stdout. The plugin folder is copied into the client's
cache and may hold nothing of the editor's: the editor is found in KINETICA_HOME (Claude's «Kinetica folder»), else
where install.sh / install.ps1 put it - ~/Kinetica/editor, on Windows %LOCALAPPDATA%\\Kinetica\\editor (a source
checkout linked there serves too). The editor's plugins: KE_PLUGINS (Claude's «Kinetica plugins folder»), else the
folder `plugins` beside the editor's, if there is one. Not found: a small server of its own says how to install it,
so the model can tell the person. Only the standard library."""
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


def kinetica():
    """Where install.sh / install.ps1 keep Kinetica: ~/Kinetica, on Windows %LOCALAPPDATA%\\Kinetica."""
    local = os.environ.get("LOCALAPPDATA")
    return os.path.join(local, "Kinetica") if local else os.path.join(os.path.expanduser("~"), "Kinetica")


def homes():
    if os.environ.get("KINETICA_HOME"):
        yield os.environ["KINETICA_HOME"]
    yield os.path.join(kinetica(), "editor")


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
    env = dict(os.environ)
    if not env.get("KE_PLUGINS") and os.path.isdir(os.path.join(kinetica(), "plugins")):
        env["KE_PLUGINS"] = os.path.join(kinetica(), "plugins")
    sys.exit(subprocess.call([sys.executable, path], env=env))  # the same stdin and stdout: the editor's server speaks to the client


if __name__ == "__main__":
    main()
