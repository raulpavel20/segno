#!/usr/bin/env python3
"""Writes the factory kits' presets (docs/design/drum-kit.md §27) into app/presets/drum-kit/, from
tools/library/kits.json (which make_recipe.py writes), balanced on a built Library.

    python3 tools/library/make_kits.py <Library> <fd-library> <fd-presets>

Each pad is a Sampler in One-shot mode playing its piece from the Library at the pad's own pitch. Its
volume sets its piece's loudest layer to its role's level (kits.json's roles): that layer's punch,
its loudest 25 ms in dB RMS, measured in the Library by `fd-library measure`. No pad peaks above
-3 dBFS at full velocity, so two hits together stay clear of clipping. The presets are written as
the DAW writes a kit (its keys sorted, parameters to six significant digits), so a factory kit
loaded and saved again is the same file. Run it after building the Library; commit what it writes.
"""

import json
import math
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "..", "app", "presets", "drum-kit")


def regions(sfz):
    """The Library's SFZ: each region's sample and velocity range."""
    out = []
    for line in open(sfz, encoding="utf-8"):
        if not line.startswith("<region>"):
            continue
        fields = dict(re.findall(r"(\w+)=(\S+)", line))
        out.append((fields["sample"], int(fields.get("lovel", 0)), int(fields.get("hivel", 127))))
    return out


def loudness(library, tool, sfz):
    """A piece's loudest layer: its punch (its takes' power averaged) and its peak."""
    path = os.path.join(library, sfz)
    found = regions(path)
    top = max(hi for _, _, hi in found)
    files = [os.path.join(os.path.dirname(path), "samples", s) for s, _, hi in found if hi == top]
    lines = subprocess.run([tool, "measure", *files], check=True, capture_output=True, text=True).stdout
    measured = [json.loads(line) for line in lines.splitlines() if line.strip()]
    punch = 10 * math.log10(sum(10 ** (m["punch"] / 10) for m in measured) / len(measured))
    return punch, max(m["peak"] for m in measured)


def six(value):
    """A parameter as a preset file writes it: six significant digits."""
    return float(f"{value:.6g}")


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    library, fd_library, fd_presets = sys.argv[1:]
    recipes = json.load(open(os.path.join(HERE, "kits.json"), encoding="utf-8"))
    sampler = json.loads(subprocess.run([fd_presets, "defaults", "sampler"], check=True, capture_output=True,
                                        text=True).stdout)
    levels = {}
    os.makedirs(OUT, exist_ok=True)
    for old in os.listdir(OUT):
        if old.endswith(".segnopreset"):
            os.remove(os.path.join(OUT, old))
    for kit in recipes["kits"]:
        pads = []
        for pad in sorted(kit["pads"], key=lambda p: p["note"]):
            if pad["piece"] not in levels:
                levels[pad["piece"]] = loudness(library, fd_library, pad["piece"])
            punch, peak = levels[pad["piece"]]
            volume = min(recipes["roles"][pad["role"]] + pad["trim"] - punch, -3.0 - peak)
            state = json.loads(json.dumps(sampler))
            state["params"].update({"mode": 1.0, "vel.amp": float(pad["velocity"]), "transpose": float(pad["transpose"])})
            state["params"] = {k: six(v) for k, v in state["params"].items()}
            state["extra"] = {"sample.path": "library:" + pad["piece"]}
            pads.append({"choke": pad["choke"],
                         "modules": [{"device": "sampler", "name": "Sampler", "on": True, "state": state}],
                         "name": pad["name"], "note": pad["note"], "on": True, "pan": float(pad["pan"]),
                         "plays": 60, "volumeDb": round(max(-30.0, min(6.0, volume)), 1)})
        state = {"device": "drum-kit", "extra": {"pads": pads},
                 "params": {f"macro{i}": 0.0 for i in range(1, 9)}, "stateVersion": 1}
        preset = {"segnoPreset": 1, "tags": kit["tags"], "description": kit["description"],
                  "author": recipes["author"], "state": json.loads(json.dumps(state, sort_keys=True))}
        with open(os.path.join(OUT, kit["name"] + ".segnopreset"), "w", encoding="utf-8") as f:
            f.write(json.dumps(preset, indent=2, ensure_ascii=False) + "\n")
        print(f"{kit['name']}: {len(pads)} pads")
    print(f"{len(recipes['kits'])} kits in {os.path.normpath(OUT)}")


if __name__ == "__main__":
    main()
