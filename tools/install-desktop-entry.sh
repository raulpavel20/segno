#!/usr/bin/env bash
# Puts Segno in the desktop's app list for you (Linux): a launcher entry that runs a build of the
# DAW, and its icon at every size, under ~/.local/share. Rebuilding updates what it runs; nothing
# is copied but the entry and the icons. Never needs root.
#
# Only a debug build looks for the Library in .cache/library (factory-library.md §6); a release build
# looks where an installer puts it. So when .cache/library holds one (the segno-library target), it's
# linked from ~/.local/share/Segno/Library, never over a Library installed there.
#
#   tools/install-desktop-entry.sh [preset]   the build to run (default: rel)
#   tools/install-desktop-entry.sh --remove   takes them away again
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DATA="${XDG_DATA_HOME:-$HOME/.local/share}"
ID="org.segnoaudio.segno"
LIBRARY="$DATA/Segno/Library"
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
    if [[ -L "$LIBRARY" && "$(readlink "$LIBRARY")" == "$ROOT/.cache/library" ]]; then
        rm "$LIBRARY"
        rmdir "$DATA/Segno" 2> /dev/null || true
    fi
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

if [[ ! -f "$ROOT/.cache/library/library.json" ]]; then
    echo "No Library in .cache/library: build it with cmake --build --preset dev --target segno-library."
elif [[ -e "$LIBRARY" && ! -L "$LIBRARY" ]]; then
    echo "A Library is installed in $LIBRARY: left as it is."
else
    mkdir -p "$DATA/Segno"
    ln -sfn "$ROOT/.cache/library" "$LIBRARY"
    echo "The Library is linked from $LIBRARY"
fi
