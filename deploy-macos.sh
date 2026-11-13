#!/usr/bin/env bash
# Packages Tessera.app for macOS. Run ./build.sh Release first. Usage: ./deploy-macos.sh [dmg]
set -euo pipefail

cd "$(dirname "$0")"
VERSION=$(sed -n 's/^project(Tessera VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
APP=build/Release/Tessera.app
DIST=dist

rm -rf "$DIST/Tessera.app"
mkdir -p "$DIST"
cp -R "$APP" "$DIST/Tessera.app"

# Best-effort icon: SVG -> iconset -> icns. A failure only costs the icon.
make_icon() {
    local tmp
    tmp=$(mktemp -d)
    qlmanage -t -s 1024 -o "$tmp" installer/tessera.svg >/dev/null 2>&1
    local set="$tmp/tessera.iconset"
    mkdir "$set"
    for s in 16 32 128 256 512; do
        sips -z $s $s "$tmp/tessera.svg.png" --out "$set/icon_${s}x${s}.png" >/dev/null
        sips -z $((s * 2)) $((s * 2)) "$tmp/tessera.svg.png" --out "$set/icon_${s}x${s}@2x.png" >/dev/null
    done
    mkdir -p "$DIST/Tessera.app/Contents/Resources"
    iconutil -c icns "$set" -o "$DIST/Tessera.app/Contents/Resources/tessera.icns"
}
make_icon || echo "Warning: icon not created, continuing without it" >&2

"${QTDIR:+$QTDIR/bin/}macdeployqt" "$DIST/Tessera.app"
# Ad-hoc signature: arm64 refuses to run unsigned code.
codesign --force --deep --sign - "$DIST/Tessera.app"

if [ "${1:-}" = "dmg" ]; then
    STAGING=$(mktemp -d)
    cp -R "$DIST/Tessera.app" "$STAGING/"
    ln -s /Applications "$STAGING/Applications"
    hdiutil create -volname "Tessera" -srcfolder "$STAGING" -ov -format UDZO "$DIST/Tessera-$VERSION-macos.dmg"
    echo "Pronto: $DIST/Tessera-$VERSION-macos.dmg"
else
    echo "Pronto: $DIST/Tessera.app"
fi
