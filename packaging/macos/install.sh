#!/bin/bash
# Builds the game from this checkout and puts Railmaster.app in
# ~/Applications, so it starts from Launchpad, Spotlight or the Dock like
# any other app: the game and its data inside the app, signed for this Mac
# (ad hoc) and registered with Launch Services. Run it again, or update.sh,
# to replace the app with a newer build. Saves, Ctrl+S's included, are in
# ~/Library/Application Support/Railmaster.
#
#   packaging/macos/install.sh
set -euo pipefail

repo="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$repo"

# Build (configuring first if this checkout has not been built yet).
if [ ! -f build/build.ninja ]; then
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DRAILMASTER_BUILD_TESTS=OFF
fi
cmake --build build
game="$repo/build/src/client/railmaster"

app="$HOME/Applications/Railmaster.app"
rm -rf "$app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"

# The game itself, and its data in the app's Resources, where it looks
# from inside an app.
cp "$game" "$app/Contents/MacOS/Railmaster"
cp -R data "$app/Contents/Resources/data"

# The icon, from data/icon.png, at the sizes macOS asks for.
iconset="$(mktemp -d)/railmaster.iconset"
mkdir -p "$iconset"
for size in 16 32 128 256 512; do
    sips -z "$size" "$size" data/icon.png --out "$iconset/icon_${size}x${size}.png" >/dev/null
    double=$((size * 2))
    sips -z "$double" "$double" data/icon.png --out "$iconset/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$iconset" -o "$app/Contents/Resources/railmaster.icns"
rm -rf "$(dirname "$iconset")"

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
    <key>CFBundleShortVersionString</key><string>0.1.0</string>
    <key>LSApplicationCategoryType</key><string>public.app-category.simulation-games</string>
    <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
PLIST

# Signed for this Mac (ad hoc), so macOS will open it, and registered, so
# Spotlight and Launchpad know it.
codesign --force --deep --sign - "$app" || echo "Could not sign the app; it may still open."
touch "$app"
lsregister=/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister
if [ -x "$lsregister" ]; then "$lsregister" -f "$app"; fi
echo "Railmaster.app is in $HOME/Applications: open it from Launchpad or Spotlight (Cmd+Space, Railmaster)."
