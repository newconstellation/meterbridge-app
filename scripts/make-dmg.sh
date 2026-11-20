#!/bin/bash
# Builds Meter Bridge (Release, universal) and packages it as a drag-to-install
# disk image: dist/Meter-Bridge-<version>.dmg
#
#   scripts/make-dmg.sh [build-dir]      (default build dir: build-juce)
#
# Uses CMAKE from the environment, else .tools/bin/cmake, else cmake on PATH.
# The app is signed ad hoc (no Apple Developer ID yet), so on another Mac the
# first launch needs right-click > Open (see "First launch.txt" in the image).
set -euo pipefail

cd "$(dirname "$0")/.."
BUILD_DIR="${1:-build-juce}"
CMAKE="${CMAKE:-}"
if [[ -z "$CMAKE" ]]; then
    if [[ -x .tools/bin/cmake ]]; then CMAKE=.tools/bin/cmake; else CMAKE=cmake; fi
fi

VERSION=$(sed -n 's/^project(MeterBridge VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
APP="$BUILD_DIR/MeterBridge_artefacts/Release/Meter Bridge.app"

if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    "$CMAKE" -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
fi
"$CMAKE" --build "$BUILD_DIR" --config Release --target MeterBridge -j 8

echo "Architectures: $(lipo -archs "$APP/Contents/MacOS/Meter Bridge")"

# Ad-hoc signature over the whole bundle, so macOS accepts it after copying.
codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict "$APP"

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
ditto "$APP" "$STAGE/Meter Bridge.app"
ln -s /Applications "$STAGE/Applications"
cat > "$STAGE/First launch.txt" <<TXT
Meter Bridge $VERSION - New Constellation

Install
  Drag "Meter Bridge" onto the Applications folder.

First launch (this test build is not notarised by Apple yet)
  1. In Applications, right-click (or Control-click) Meter Bridge and choose Open.
  2. Click Open in the warning. You only do this once.
  If macOS only offers "Move to Trash" or "Done": open System Settings >
  Privacy & Security, scroll down, and click "Open Anyway" next to Meter Bridge.

  When asked, allow microphone access: Meter Bridge reads your audio inputs to
  show levels and spectra. Nothing is recorded or sent anywhere.

Then open Settings in the app, pick your input device (for example Dante
Virtual Soundcard) and add your sources.

Requires macOS 12 or later, Apple silicon or Intel.
TXT

mkdir -p dist
DMG="dist/Meter-Bridge-$VERSION.dmg"
rm -f "$DMG"
hdiutil create -volname "Meter Bridge $VERSION" -srcfolder "$STAGE" -fs HFS+ -format UDZO -ov "$DMG" >/dev/null
hdiutil verify "$DMG" >/dev/null
echo "Built $DMG ($(du -h "$DMG" | cut -f1))"
