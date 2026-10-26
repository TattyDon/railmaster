#!/bin/bash
# Builds the game from this checkout and puts Railmaster.app in
# ~/Applications, so it starts from Launchpad, Spotlight or the Dock like
# any other app. The app runs the game built here, so after an update
# (update.sh) it opens the new version. Saves made with Ctrl+S, and the
# game's own folder, are in ~/Library/Application Support/Railmaster.
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

# The app's program: start the game from a folder of its own, so its
# quick saves have somewhere to go.
cat > "$app/Contents/MacOS/Railmaster" <<LAUNCH
#!/bin/bash
mkdir -p "\$HOME/Library/Application Support/Railmaster"
cd "\$HOME/Library/Application Support/Railmaster"
exec "$game" "\$@"
LAUNCH
chmod +x "$app/Contents/MacOS/Railmaster"

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

# Let Finder and Launchpad notice the new app (and its icon).
touch "$app"
echo "Railmaster.app is in $HOME/Applications: open it from Launchpad or Spotlight (Cmd+Space, Railmaster)."
