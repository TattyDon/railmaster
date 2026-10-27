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
"$repo/packaging/macos/make_app.sh" "$game" "$app"

# Signed for this Mac (ad hoc), so macOS will open it, and registered, so
# Spotlight and Launchpad know it.
codesign --force --deep --sign - "$app" || echo "Could not sign the app; it may still open."
touch "$app"
lsregister=/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister
if [ -x "$lsregister" ]; then "$lsregister" -f "$app"; fi
echo "Railmaster.app is in $HOME/Applications: open it from Launchpad or Spotlight (Cmd+Space, Railmaster)."
