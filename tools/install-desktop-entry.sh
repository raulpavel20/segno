#!/usr/bin/env bash
# Puts Segno in the desktop's app list for you (Linux): a launcher entry that runs a build of the
# DAW, and its icon at every size, under ~/.local/share. Rebuilding updates what it runs; nothing
# is copied but the entry and the icons. Never needs root.
#
#   tools/install-desktop-entry.sh [preset]   the build to run (default: rel)
#   tools/install-desktop-entry.sh --remove   takes them away again
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DATA="${XDG_DATA_HOME:-$HOME/.local/share}"
ID="org.segnoaudio.segno"
SIZES=(16 24 32 48 64 128 256 512)

refresh() {
    command -v update-desktop-database > /dev/null && update-desktop-database "$DATA/applications" || true
    command -v gtk-update-icon-cache > /dev/null && gtk-update-icon-cache -f -t "$DATA/icons/hicolor" > /dev/null 2>&1 || true
}

if [[ "${1:-}" == "--remove" ]]; then
    rm -f "$DATA/applications/$ID.desktop"
    for n in "${SIZES[@]}"; do
        rm -f "$DATA/icons/hicolor/${n}x${n}/apps/$ID.png"
    done
    refresh
    echo "Removed Segno from the app list."
    exit 0
fi

PRESET="${1:-rel}"
EXEC="$(find "$ROOT/build/$PRESET/app/fd-daw_artefacts" -maxdepth 2 -type f -name Segno -perm -u+x 2> /dev/null | head -1)"
if [[ -z "$EXEC" ]]; then
    echo "No Segno in build/$PRESET: build it first (cmake --build --preset $PRESET --target fd-daw)." >&2
    exit 1
fi

for n in "${SIZES[@]}"; do
    install -Dm644 "$ROOT/packaging/linux/icons/hicolor/${n}x${n}/apps/$ID.png" \
        "$DATA/icons/hicolor/${n}x${n}/apps/$ID.png"
done
mkdir -p "$DATA/applications"
sed "s|@SEGNO_EXEC@|\"$EXEC\"|" "$ROOT/packaging/linux/$ID.desktop.in" > "$DATA/applications/$ID.desktop"
command -v desktop-file-validate > /dev/null && desktop-file-validate "$DATA/applications/$ID.desktop"
refresh
echo "Segno is in the app list, running $EXEC"
