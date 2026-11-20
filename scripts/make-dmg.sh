#!/bin/bash
# Builds Meter Bridge (Release, universal) and packages it as a drag-to-install
# disk image: dist/Meter-Bridge-<version>.dmg
#
#   scripts/make-dmg.sh [build-dir]      (default build dir: build-juce)
#
# Uses CMAKE from the environment, else .tools/bin/cmake, else cmake on PATH.
#
# Signing (automatic):
# - If a "Developer ID Application" certificate is in the keychain (or SIGN_ID
#   names one), the app is signed with it, with the hardened runtime and
#   Resources/MeterBridge.entitlements, and the DMG is signed too.
# - If a notarytool keychain profile exists (NOTARY_PROFILE, default
#   "MeterBridge"), the DMG is sent to Apple for notarisation and the ticket
#   is stapled, so it opens on any Mac without a warning. Create the profile
#   once with:
#     xcrun notarytool store-credentials MeterBridge --apple-id <you@example.com> --team-id <TEAMID>
#   (it asks for an app-specific password from appleid.apple.com).
# - Otherwise the app is signed ad hoc and the first launch on another Mac
#   needs right-click > Open (see "First launch.txt" in the image).
# Set SIGN_ID=- to force an ad-hoc build.
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

SIGN_ID="${SIGN_ID:-$(security find-identity -v -p codesigning 2>/dev/null | sed -n 's/.*"\(Developer ID Application: [^"]*\)".*/\1/p' | head -1)}"
SIGN_ID="${SIGN_ID:--}"
NOTARY_PROFILE="${NOTARY_PROFILE:-MeterBridge}"

if [[ "$SIGN_ID" == "-" ]]; then
    echo "Signing ad hoc (no Developer ID certificate found)"
    codesign --force --deep --sign - "$APP"
    SIGNED=0
else
    echo "Signing with: $SIGN_ID"
    codesign --force --deep --options runtime --timestamp \
             --entitlements Resources/MeterBridge.entitlements --sign "$SIGN_ID" "$APP"
    SIGNED=1
fi
codesign --verify --deep --strict "$APP"

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
ditto "$APP" "$STAGE/Meter Bridge.app"
ln -s /Applications "$STAGE/Applications"
cat > "$STAGE/First launch.txt" <<TXT
Meter Bridge $VERSION - New Constellation

Install
  Drag "Meter Bridge" onto the Applications folder.

First launch
  Open Meter Bridge from Applications. If macOS says it can't check the app
  (only for test builds that are not notarised):
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

if [[ "$SIGNED" == 1 ]]; then
    codesign --force --timestamp --sign "$SIGN_ID" "$DMG"
    if xcrun notarytool history --keychain-profile "$NOTARY_PROFILE" >/dev/null 2>&1; then
        echo "Notarising with profile $NOTARY_PROFILE (a few minutes)..."
        xcrun notarytool submit "$DMG" --keychain-profile "$NOTARY_PROFILE" --wait
        xcrun stapler staple "$DMG"
        spctl --assess --type open --context context:primary-signature -v "$DMG"
    else
        echo "No notary profile \"$NOTARY_PROFILE\": signed but not notarised (see the top of this script)."
    fi
fi

echo "Built $DMG ($(du -h "$DMG" | cut -f1))"
