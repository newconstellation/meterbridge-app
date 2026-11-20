#!/bin/bash
# Builds Meter Bridge (Release, universal) and packages it as a drag-to-install
# disk image: dist/Meter-Bridge-<version>.dmg
#
#   scripts/make-dmg.sh [build-dir]      (default build dir: build-juce)
#
# Signing:
#   - With a "Developer ID Application" certificate in the keychain, the app is
#     signed with it (hardened runtime, Resources/entitlements.plist) and so is
#     the disk image. Pick a certificate with MB_SIGN_ID if there are several.
#   - Without one (or with MB_SIGN_ID=-), the app is signed ad hoc and the first
#     launch on another Mac needs right-click > Open (see "First launch.txt").
#
# Notarizing (Developer ID only): the image is sent to Apple and the ticket is
# stapled when a notarytool keychain profile exists: MB_NOTARY_PROFILE if set,
# else "MeterBridge" or "meterbridge". Make it once per Mac with:
#   xcrun notarytool store-credentials MeterBridge --apple-id <apple id> --team-id <team id>
# (it asks for an app-specific password from account.apple.com).
# Skip notarizing with MB_NOTARIZE=0.
#
# Uses CMAKE from the environment, else .tools/bin/cmake, else cmake on PATH.
set -euo pipefail

cd "$(dirname "$0")/.."
BUILD_DIR="${1:-build-juce}"
CMAKE="${CMAKE:-}"
if [[ -z "$CMAKE" ]]; then
    if [[ -x .tools/bin/cmake ]]; then CMAKE=.tools/bin/cmake; else CMAKE=cmake; fi
fi

VERSION=$(sed -n 's/^project(MeterBridge VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
APP="$BUILD_DIR/MeterBridge_artefacts/Release/Meter Bridge.app"
ENTITLEMENTS=Resources/entitlements.plist

SIGN_ID="${MB_SIGN_ID:-}"
if [[ -z "$SIGN_ID" ]]; then
    SIGN_ID=$(security find-identity -v -p codesigning 2>/dev/null |
              awk -F'"' '/Developer ID Application/ {print $2; exit}')
    SIGN_ID="${SIGN_ID:--}"
fi

NOTARIZE=0
PROFILE=""
if [[ "$SIGN_ID" != "-" && "${MB_NOTARIZE:-1}" != "0" ]]; then
    for candidate in ${MB_NOTARY_PROFILE:-MeterBridge meterbridge}; do
        if xcrun notarytool history --keychain-profile "$candidate" >/dev/null 2>&1; then
            PROFILE="$candidate"
            NOTARIZE=1
            break
        fi
    done
    if [[ "$NOTARIZE" == "0" ]]; then
        echo "No notarytool profile found: the image will be signed but not notarized."
        echo "Create one with"
        echo "  xcrun notarytool store-credentials MeterBridge --apple-id <apple id> --team-id <team id>"
        echo "or set MB_NOTARY_PROFILE to an existing one."
    fi
fi

if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    "$CMAKE" -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
fi
"$CMAKE" --build "$BUILD_DIR" --config Release --target MeterBridge -j 8

echo "Architectures: $(lipo -archs "$APP/Contents/MacOS/Meter Bridge")"

if [[ "$SIGN_ID" == "-" ]]; then
    echo "Signing ad hoc (no Developer ID certificate found)"
    codesign --force --deep --sign - "$APP"
else
    echo "Signing with: $SIGN_ID"
    codesign --force --deep --options runtime --timestamp \
        --entitlements "$ENTITLEMENTS" --sign "$SIGN_ID" "$APP"
fi
codesign --verify --deep --strict "$APP"

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
ditto "$APP" "$STAGE/Meter Bridge.app"
ln -s /Applications "$STAGE/Applications"

if [[ "$NOTARIZE" == "1" ]]; then
    FIRST_LAUNCH='First launch
  Open Meter Bridge from Applications.'
else
    FIRST_LAUNCH='First launch (this test build is not notarised by Apple)
  1. In Applications, right-click (or Control-click) Meter Bridge and choose Open.
  2. Click Open in the warning. You only do this once.
  If macOS only offers "Move to Trash" or "Done": open System Settings >
  Privacy & Security, scroll down, and click "Open Anyway" next to Meter Bridge.'
fi

cat > "$STAGE/First launch.txt" <<TXT
Meter Bridge $VERSION - New Constellation

Install
  Drag "Meter Bridge" onto the Applications folder.

$FIRST_LAUNCH

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

if [[ "$SIGN_ID" != "-" ]]; then
    codesign --force --timestamp --sign "$SIGN_ID" "$DMG"
fi
if [[ "$NOTARIZE" == "1" ]]; then
    echo "Notarizing with profile \"$PROFILE\" (usually a few minutes)..."
    xcrun notarytool submit "$DMG" --keychain-profile "$PROFILE" --wait
    xcrun stapler staple "$DMG"
    spctl -a -t open --context context:primary-signature -vv "$DMG"
fi

echo "Built $DMG ($(du -h "$DMG" | cut -f1))"
