#!/bin/bash
# Makes a copy of Railmaster.app to give to someone else, to play together
# on another Mac without building it: the libraries the game takes from
# Homebrew (SDL2) are put inside the app, so the other Mac needs nothing
# installed. It is built for macOS 11 or later, on this Mac's kind of
# processor (Apple silicon or Intel), signed ad hoc (not notarised: the
# first time, the other Mac asks to be told it is safe to open). The zip is
# put on the Desktop.
#
#   packaging/macos/share.sh
set -euo pipefail

repo="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$repo"

# A build of its own, for older macOS than this one.
if [ ! -f build-share/build.ninja ]; then
    cmake -S . -B build-share -G Ninja -DCMAKE_BUILD_TYPE=Release -DRAILMASTER_BUILD_TESTS=OFF \
        -DRAILMASTER_BUILD_TOOLS=OFF -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
fi
cmake --build build-share

stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
app="$stage/Railmaster.app"
"$repo/packaging/macos/make_app.sh" "$repo/build-share/src/client/railmaster" "$app"

# The libraries outside macOS itself (Homebrew's), into Contents/Frameworks,
# and every reference to them pointed there. Libraries they use in turn
# are brought along the same way.
frameworks="$app/Contents/Frameworks"
mkdir -p "$frameworks"
outside() { # the non-system libraries a binary links to
    otool -L "$1" | tail -n +2 | awk '{print $1}' | grep -E '^/(opt/homebrew|usr/local|opt/local)/' || true
}
# Libraries found by a search path (@rpath) cannot be followed here: say so
# rather than make an app that will not open elsewhere.
searched="$(otool -L "$app/Contents/MacOS/Railmaster" | tail -n +2 | awk '{print $1}' | grep '^@rpath/' || true)"
if [ -n "$searched" ]; then
    echo "The game finds these by a search path, which this script cannot bundle yet:" >&2
    echo "$searched" >&2
    exit 1
fi
# (A queue of paths, one a line: macOS's bash 3.2 is unsure with arrays.)
nl=$'\n'
todo="$app/Contents/MacOS/Railmaster"
while [ -n "$todo" ]; do
    binary="${todo%%"$nl"*}"
    if [ "$binary" = "$todo" ]; then todo=""; else todo="${todo#*"$nl"}"; fi
    for lib in $(outside "$binary"); do
        name="$(basename "$lib")"
        if [ ! -f "$frameworks/$name" ]; then
            cp -L "$lib" "$frameworks/$name"
            chmod u+w "$frameworks/$name"
            install_name_tool -id "@executable_path/../Frameworks/$name" "$frameworks/$name"
            todo="${todo:+$todo$nl}$frameworks/$name"
        fi
        install_name_tool -change "$lib" "@executable_path/../Frameworks/$name" "$binary"
    done
done
left="$(outside "$app/Contents/MacOS/Railmaster"; for f in "$frameworks"/*; do [ -f "$f" ] && outside "$f"; done)"
if [ -n "$left" ]; then
    echo "Still linked outside the app (the other Mac would need these):" >&2
    echo "$left" >&2
    exit 1
fi

# Signed again: changing where it finds its libraries undoes the signature,
# and Apple silicon will not run an unsigned program.
codesign --force --sign - "$frameworks"/* 2>/dev/null || true
codesign --force --deep --sign - "$app"
codesign --verify --deep "$app"

# The oldest macOS that will run it (the libraries can ask for a newer one
# than the game).
oldest="11.0"
for f in "$app/Contents/MacOS/Railmaster" "$frameworks"/*; do
    [ -f "$f" ] || continue
    v="$(otool -l "$f" | awk '/LC_BUILD_VERSION/{b=1} b && $1=="minos"{print $2; exit}')"
    if [ -n "$v" ] && [ "$(printf '%s\n%s\n' "$oldest" "$v" | sort -V | tail -1)" = "$v" ]; then oldest="$v"; fi
done

arch="$(uname -m)"
case "$arch" in
arm64) kind="Apple silicon" ;;
x86_64) kind="Intel" ;;
*) kind="$arch" ;;
esac
version="$(sed -n 's/^ *VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt | head -1)"
build="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
zip="$HOME/Desktop/Railmaster-$version-$build-macOS-$arch.zip"
rm -f "$zip"
ditto -c -k --keepParent "$app" "$zip"

cat <<DONE
Made $zip
  for Macs with $kind, macOS $oldest or later.

On the other Mac:
  1. Open the zip (double-click) and drag Railmaster to Applications.
  2. The first time, Control-click Railmaster and choose Open, then Open
     again: macOS asks because the app is not from the App Store or a
     registered developer. After that it opens as usual.
     (Or, in Terminal: xattr -dr com.apple.quarantine /Applications/Railmaster.app)
  3. To play together, both Macs need this same version: share a new zip
     after updating.
DONE
