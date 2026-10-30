#!/bin/bash
# Makes Railmaster.app at APP_PATH from the built game GAME and this
# checkout's data: the game in Contents/MacOS, its data in
# Contents/Resources (where it looks from inside an app), the icon and
# Info.plist. Not signed: install.sh and share.sh sign it.
#
#   packaging/macos/make_app.sh GAME APP_PATH
set -euo pipefail

game="$1"
app="$2"
repo="$(cd "$(dirname "$0")/../.." && pwd)"

rm -rf "$app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
cp "$game" "$app/Contents/MacOS/Railmaster"
cp -R "$repo/data" "$app/Contents/Resources/data"

# The icon, from data/icon.png, at the sizes macOS asks for.
iconset="$(mktemp -d)/railmaster.iconset"
mkdir -p "$iconset"
for size in 16 32 128 256 512; do
    sips -z "$size" "$size" "$repo/data/icon.png" --out "$iconset/icon_${size}x${size}.png" >/dev/null
    double=$((size * 2))
    sips -z "$double" "$double" "$repo/data/icon.png" --out "$iconset/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$iconset" -o "$app/Contents/Resources/railmaster.icns"
rm -rf "$(dirname "$iconset")"

version="$(sed -n 's/^ *VERSION \([0-9.]*\).*/\1/p' "$repo/CMakeLists.txt" | head -1)"
cat > "$app/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>Railmaster</string>
    <key>CFBundleDisplayName</key><string>Railmaster</string>
    <key>CFBundleIdentifier</key><string>io.github.tattydon.railmaster</string>
    <key>CFBundleExecutable</key><string>Railmaster</string>
    <key>CFBundleIconFile</key><string>railmaster</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>${version:-0.1.0}</string>
    <key>LSApplicationCategoryType</key><string>public.app-category.simulation-games</string>
    <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
PLIST
