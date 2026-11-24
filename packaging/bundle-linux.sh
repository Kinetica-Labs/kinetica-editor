#!/usr/bin/env bash
# Kinetica for Linux in one folder - the program, the windowless kinetica-core, Qt's libraries and plugins (linuxdeploy and its Qt plugin), the
# examples, the sensor models' tables, the console's Python package, the editor's own plugins, the agents' client and the licences - packed as kinetica-linux.tar.gz (tar
# keeps the right to run the program; a zip would lose it). Run from the repository's root after a Release build:
#   packaging/bundle-linux.sh <build dir> <qmake of the Qt the build used>
# A program built on an older Ubuntu runs on newer ones, not the other way round (glibc): CI builds it on 22.04.
set -euo pipefail
build=$(realpath "${1:-build}")
qmake=${2:-qmake}
tools="$build/deploy-tools" out="$build/kinetica-linux"
mkdir -p "$tools"
fetch() { [ -x "$tools/$1" ] || { curl -fsSL -o "$tools/$1" "$2"; chmod +x "$tools/$1"; }; }
fetch linuxdeploy-x86_64.AppImage https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
fetch linuxdeploy-plugin-qt-x86_64.AppImage https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
export APPIMAGE_EXTRACT_AND_RUN=1 QMAKE="$qmake" PATH="$tools:$PATH" # no FUSE needed
rm -rf "$out"
linuxdeploy-x86_64.AppImage --appdir "$out" --executable "$build/kinetica" --executable "$build/kinetica-core" --desktop-file packaging/kinetica.desktop \
  --icon-file resources/icons/kinetica-256.png --icon-filename kinetica --plugin qt
cp -r examples clients data "$out/usr/bin/" # the program looks next to itself for them
for p in plugins/*/plugin.py; do # the plugins that ship with the editor (plugins/urdf): its host scans the folder next to it
    mkdir -p "$out/usr/bin/plugins" && cp -r "$(dirname "$p")" "$out/usr/bin/plugins/"
done
mkdir -p "$out/usr/bin/tools" && cp tools/ke_editor.py tools/ke_editor_mcp.py "$out/usr/bin/tools/"
mkdir -p "$out/usr/bin/docs" && cp -r docs/agent-guide.md docs/agent "$out/usr/bin/docs/" # what the MCP server gives a model
cp LICENSE THIRD_PARTY_NOTICES.md README.md "$out/" && cp extern/box3d/LICENSE "$out/LICENSE-box3d.txt"
printf '#!/bin/sh\nexec "$(dirname "$(readlink -f "$0")")/usr/bin/kinetica" "$@"\n' > "$out/kinetica"
chmod +x "$out/kinetica"
tar -czf "$build/kinetica-linux.tar.gz" -C "$build" kinetica-linux
echo "$build/kinetica-linux.tar.gz"
