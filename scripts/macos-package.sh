#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 x3ro
# SPDX-License-Identifier: GPL-3.0-or-later
#
# macos-package.sh <version>: builds a universal (arm64 + x86_64) x3ro-deck.app,
# deploys Qt into it, signs it with the hardened runtime, notarizes and staples
# it, and wraps it in x3ro-deck-<version>.dmg.
#
# Needs a universal Qt (the official installer's) on CMAKE_PREFIX_PATH and, for
# signing and notarization, these in the environment:
#   MACOS_SIGN_IDENTITY   "Developer ID Application: … (TEAMID)"
#   NOTARY_APPLE_ID, NOTARY_TEAM_ID, NOTARY_PASSWORD (an app-specific password)
# Without MACOS_SIGN_IDENTITY it builds an ad-hoc signed DMG for testing.
set -euo pipefail
version=${1:?usage: macos-package.sh <version>}
root=$(git rev-parse --show-toplevel)
build="$root/build-release-macos"

cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
    -DX3RO_DECK_BUNDLED_VTERM=ON
cmake --build "$build"
app="$build/x3ro-deck.app"

# Qt as shared frameworks inside the bundle, with the QML the panels import.
macdeployqt "$app" -qmldir="$root/qml" -always-overwrite
# Qt's LGPL-3.0 text and where its source is; libvterm's MIT notice.
mkdir -p "$app/Contents/Resources/licenses"
cp "$root/LICENSE" "$root/TRADEMARKS.md" "$root"/data/licenses/* "$app/Contents/Resources/licenses/"
echo "Qt source: https://download.qt.io/official_releases/qt/" > "$app/Contents/Resources/licenses/Qt-SOURCE.txt"

for arch in arm64 x86_64; do
    lipo "$app/Contents/MacOS/x3ro-deck" -verify_arch "$arch"
done

identity=${MACOS_SIGN_IDENTITY:--}
# Ad-hoc signatures (test builds) cannot carry a secure timestamp.
ts=--timestamp
[[ $identity == - ]] && ts=--timestamp=none
# Inside out: helpers and frameworks first, then the bundle.
find "$app/Contents" -type f \( -name '*.dylib' -o -perm -u+x \) -print0 |
    xargs -0 codesign --force "$ts" --options runtime --sign "$identity"
codesign --force "$ts" --options runtime --entitlements "$root/packaging/macos/entitlements.plist" \
    --sign "$identity" "$app"
codesign --verify --deep --strict "$app"

dmg="$root/x3ro-deck-$version.dmg"
staging=$(mktemp -d)
cp -R "$app" "$staging/"
ln -s /Applications "$staging/Applications"
hdiutil create -volname "x3ro deck $version" -srcfolder "$staging" -ov -format UDZO "$dmg"
rm -rf "$staging"

if [[ $identity != - ]]; then
    codesign --force --timestamp --sign "$identity" "$dmg"
    xcrun notarytool submit "$dmg" --apple-id "$NOTARY_APPLE_ID" --team-id "$NOTARY_TEAM_ID" \
        --password "$NOTARY_PASSWORD" --wait
    xcrun stapler staple "$dmg"
    spctl --assess --type open --context context:primary-signature -v "$dmg"
fi
shasum -a 256 "$dmg"
