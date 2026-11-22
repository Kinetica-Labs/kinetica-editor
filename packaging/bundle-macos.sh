#!/usr/bin/env bash
# Kinetica for macOS as one application - kinetica.app: the program and the windowless kinetica-core in Contents/MacOS,
# Qt's frameworks and plugins (macdeployqt), and in Contents/Resources the examples, the sensor models' tables, the
# console's Python package, the editor's own plugins, the agents' client and the licences (the program looks there,
# editor/Resources.h) - packed as kinetica-macos.zip. Signed ad hoc, not by Apple: the first start is right click ->
# Open. Run from the repository's root after a Release build:
#   packaging/bundle-macos.sh <build dir> <macdeployqt of the Qt the build used> <version>
set -euo pipefail
build=$(cd "${1:-build}" && pwd)
deploy=${2:-macdeployqt}
version=${3:-0.0.0}
app="$build/kinetica.app"
rm -rf "$app" "$build/kinetica.iconset"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
cp "$build/kinetica" "$build/kinetica-core" "$app/Contents/MacOS/"
cp -R examples clients data "$app/Contents/Resources/"
for p in plugins/*/plugin.py; do # the plugins that ship with the editor (plugins/urdf): its host scans the folder
    mkdir -p "$app/Contents/Resources/plugins" && cp -R "$(dirname "$p")" "$app/Contents/Resources/plugins/"
done
[ -f "$app/Contents/Resources/plugins/urdf/plugin.py" ] || { echo "the URDF plugin is not in the program" >&2; exit 1; }
mkdir -p "$app/Contents/Resources/tools" && cp tools/ke_editor.py tools/ke_editor_mcp.py "$app/Contents/Resources/tools/"
cp LICENSE THIRD_PARTY_NOTICES.md README.md "$app/Contents/Resources/" && cp extern/box3d/LICENSE "$app/Contents/Resources/LICENSE-box3d.txt"
mkdir "$build/kinetica.iconset" # the Dock's icon, from the project's own picture
for s in 16 32 128 256; do
    sips -z $s $s resources/icons/kinetica-256.png --out "$build/kinetica.iconset/icon_${s}x${s}.png" >/dev/null
    sips -z $((s * 2)) $((s * 2)) resources/icons/kinetica-256.png --out "$build/kinetica.iconset/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns "$build/kinetica.iconset" -o "$app/Contents/Resources/kinetica.icns"
cat > "$app/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleExecutable</key><string>kinetica</string>
  <key>CFBundleIdentifier</key><string>io.github.kinetica-labs.editor</string>
  <key>CFBundleName</key><string>Kinetica</string>
  <key>CFBundleDisplayName</key><string>Kinetica Editor</string>
  <key>CFBundleIconFile</key><string>kinetica</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>$version</string>
  <key>CFBundleVersion</key><string>$version</string>
  <key>LSMinimumSystemVersion</key><string>12.0</string>
  <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
PLIST
"$deploy" "$app" -executable="$app/Contents/MacOS/kinetica-core" -always-overwrite
codesign --force --deep --sign - "$app" # ad hoc: Apple Silicon runs nothing unsigned
codesign --verify --deep --strict "$app"
"$app/Contents/MacOS/kinetica-core" --version # the bundled host starts
(cd "$build" && ditto -c -k --keepParent kinetica.app kinetica-macos.zip)
echo "$build/kinetica-macos.zip"
