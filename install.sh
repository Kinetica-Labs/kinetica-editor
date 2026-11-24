#!/usr/bin/env bash
# Kinetica on Linux and macOS in one command: Python checked (asked before anything is installed), the editor from its
# latest release unpacked into ~/Kinetica/editor, and the plugin added to Claude and Codex when their commands are there. Nothing
# is installed system-wide without a yes (or --yes); run it again to update. Windows: install.ps1.
#
#   curl -fsSL https://raw.githubusercontent.com/Kinetica-Labs/kinetica-editor/main/install.sh | bash
#   ./install.sh [--yes] [--dir DIR] [--no-claude]   # --no-claude: no plugin for Claude or Codex
set -euo pipefail

REPO="Kinetica-Labs/kinetica-editor"
DIR="${KINETICA_HOME:-$HOME/Kinetica/editor}"
YES=0
CLAUDE=1
while [ $# -gt 0 ]; do
    case "$1" in
        --yes|-y) YES=1 ;;
        --dir) DIR="$2"; shift ;;
        --no-claude) CLAUDE=0 ;;
        *) echo "usage: install.sh [--yes] [--dir DIR] [--no-claude]" >&2; exit 2 ;;
    esac
    shift
done

say() { printf '\033[1m%s\033[0m\n' "$*"; }
ask() { # a yes from the person (or --yes); no terminal to ask at: no
    [ "$YES" = 1 ] && return 0
    [ -r /dev/tty ] || return 1
    local a; read -r -p "$1 [y/N] " a </dev/tty; [[ "$a" == [yYдД]* ]]
}

os="$(uname -s)" arch="$(uname -m)"
case "$os/$arch" in
    Linux/x86_64) asset="kinetica-linux.tar.gz" ;;
    Darwin/*) asset="kinetica-macos.zip" ;;
    *) say "Kinetica выпускается для Linux x86-64, macOS и Windows; здесь $os $arch - соберите из исходников (README)."; exit 1 ;;
esac

# 1. Python 3.8+: the MCP server, the console's scripts and the plugins run on it (its standard library only).
if ! python3 -c 'import sys; sys.exit(sys.version_info < (3, 8))' 2>/dev/null; then
    say "Нужен Python 3.8 или новее: на нём MCP-сервер, скрипты и плагины."
    if [ "$os" = Darwin ] && command -v brew >/dev/null && ask "Поставить его через Homebrew (brew install python)?"; then
        brew install python
    elif command -v apt-get >/dev/null && ask "Поставить его через apt (sudo apt-get install python3)?"; then
        sudo apt-get install -y python3
    elif command -v dnf >/dev/null && ask "Поставить его через dnf (sudo dnf install python3)?"; then
        sudo dnf install -y python3
    elif command -v pacman >/dev/null && ask "Поставить его через pacman (sudo pacman -S python)?"; then
        sudo pacman -S --noconfirm python
    else
        say "Поставьте Python с python.org и запустите этот скрипт снова."; exit 1
    fi
fi
say "Python: $(python3 --version)"

# 2. The editor: the latest release for this system, the previous one kept beside it until this one is in.
url="https://github.com/$REPO/releases/latest/download/$asset"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
say "Скачиваю $url"
curl -fL --progress-bar -o "$tmp/$asset" "$url"
mkdir -p "$tmp/new"
case "$asset" in
    *.tar.gz) tar -xzf "$tmp/$asset" -C "$tmp/new" ;;
    *.zip) (cd "$tmp/new" && unzip -q "$tmp/$asset") ;;
esac
mkdir -p "$(dirname "$DIR")"
[ -d "$DIR" ] && rm -rf "$DIR.old" && mv "$DIR" "$DIR.old"
mv "$tmp/new" "$DIR"
rm -rf "$DIR.old"
if [ "$os" = Darwin ]; then
    program="$DIR/kinetica.app/Contents/MacOS/kinetica"
    # signed ad hoc, not by Apple: macOS asks once (right click -> Open), or the download mark is taken off here
    if ask "Снять с программы метку скачанного файла (xattr), чтобы macOS открыла её без вопроса?"; then
        xattr -dr com.apple.quarantine "$DIR/kinetica.app" || true
    fi
else
    program="$DIR/kinetica-linux/kinetica"
fi
say "Редактор: $program"

# 3. Claude: the plugin from this repository's marketplace - the editor's MCP server, its skills.
if [ "$CLAUDE" = 1 ] && command -v claude >/dev/null; then
    say "Подключаю плагин Kinetica к Claude"
    claude plugin marketplace add "$REPO" || true
    claude plugin install kinetica@kinetica --config "python=python3" --config "editor=$DIR" || true
elif [ "$CLAUDE" = 1 ]; then
    say "Команды claude нет: плагин поставьте в Claude сами - /plugin marketplace add $REPO, затем /plugin install kinetica@kinetica"
fi

# 4. Codex and ChatGPT: the same plugin from the same marketplace (plugin.json, the Agent Plugins schema).
if [ "$CLAUDE" = 1 ] && command -v codex >/dev/null; then
    say "Подключаю плагин Kinetica к Codex и ChatGPT"
    codex plugin marketplace add "$REPO" || true
    codex plugin add kinetica@kinetica || true
fi

say "Готово. Запуск: $program"
