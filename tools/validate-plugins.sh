#!/usr/bin/env bash
# Runs the plugin validators (ADR 0007) on every built plugin of a preset.
#
#   tools/validate-plugins.sh [preset]        (default: dev)
#
# Validators are pinned and downloaded once into .cache/tools/. Linux only for now; CI runs the
# macOS/Windows equivalents (plus auval on macOS).
set -euo pipefail

PRESET="${1:-dev}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TOOLS="$ROOT/.cache/tools"
PLUGINVAL_VERSION="v1.0.4"
CLAPVAL_VERSION="0.4.1"
CLAPVAL_ASSET="clap-validator-0.4.1-127-g152b982-ubuntu-22.04.zip"

fetch() { # url dir
    local url="$1" dir="$2"
    if [[ ! -d "$dir" ]]; then
        mkdir -p "$dir"
        curl -fsSL "$url" -o "$dir/download.zip"
        (cd "$dir" && unzip -q download.zip && rm download.zip)
        # Some releases nest a tarball inside the zip.
        find "$dir" -maxdepth 1 -name "*.tar.gz" -exec tar -xzf {} -C "$dir" \;
    fi
}

fetch "https://github.com/Tracktion/pluginval/releases/download/$PLUGINVAL_VERSION/pluginval_Linux.zip" \
      "$TOOLS/pluginval-$PLUGINVAL_VERSION"
fetch "https://github.com/free-audio/clap-validator/releases/download/$CLAPVAL_VERSION/$CLAPVAL_ASSET" \
      "$TOOLS/clap-validator-$CLAPVAL_VERSION"

PLUGINVAL="$(find "$TOOLS/pluginval-$PLUGINVAL_VERSION" -type f -name pluginval | head -1)"
CLAPVAL="$(find "$TOOLS/clap-validator-$CLAPVAL_VERSION" -type f -name clap-validator | head -1)"
chmod +x "$PLUGINVAL" "$CLAPVAL"

failed=0
while IFS= read -r -d '' vst3; do
    echo "=== pluginval (strictness 10): $vst3"
    if ! "$PLUGINVAL" --strictness-level 10 --validate-in-process --output-dir "$ROOT/build/$PRESET/validation" --validate "$vst3"; then
        echo "FAILED: $vst3"; failed=1
    fi
done < <(find "$ROOT/build/$PRESET/plugins" -name "*.vst3" -type d -path "*/VST3/*" -print0)

while IFS= read -r -d '' clap; do
    echo "=== clap-validator: $clap"
    if ! "$CLAPVAL" validate "$clap"; then
        echo "FAILED: $clap"; failed=1
    fi
done < <(find "$ROOT/build/$PRESET/plugins" -name "*.clap" -path "*/CLAP/*" -print0)

if [[ $failed -ne 0 ]]; then
    echo "Plugin validation FAILED"; exit 1
fi
echo "All plugins validated."
