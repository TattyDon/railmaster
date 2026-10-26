#!/bin/bash
# Fetches the latest version of the game into this checkout, rebuilds it
# and refreshes Railmaster.app (see install.sh).
#
#   packaging/macos/update.sh
set -euo pipefail

repo="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$repo"
git pull --ff-only
"$repo/packaging/macos/install.sh"
