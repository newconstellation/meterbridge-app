#!/usr/bin/env bash
# Builds Meter Bridge.app from the Swift package (release, Apple Silicon)
# and signs it ad hoc so macOS will ask for audio input permission.
#
#   ./scripts/build-app.sh            -> build/Meter Bridge.app
#   open "build/Meter Bridge.app"
set -euo pipefail

cd "$(dirname "$0")/.."
APP="build/Meter Bridge.app"

swift build -c release --arch arm64
BIN="$(swift build -c release --arch arm64 --show-bin-path)/MeterBridge"

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN" "$APP/Contents/MacOS/MeterBridge"
cp Resources/Info.plist "$APP/Contents/Info.plist"
iconutil -c icns Resources/AppIcon.iconset -o "$APP/Contents/Resources/AppIcon.icns"

# Ad hoc signature (no Apple Developer ID yet). First launch: right-click › Open.
codesign --force --deep --sign - "$APP"

echo "Built $APP"
