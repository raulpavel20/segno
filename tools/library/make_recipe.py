#!/usr/bin/env python3
"""Makes tools/library/recipe.json: every sample instrument in Segno's Library, zone by zone, each
source file with its SHA-256 (ADR 0055, docs/design/factory-library.md §6-§7); and
tools/library/kits.json, the factory kits' recipes (drum-kit.md §27), which make_kits.py turns into
presets once the Library is built.

    python3 tools/library/make_recipe.py <sources cache>

The instruments are listed below: the VSCO 2 Community Edition ones from its own SFZ maps (its 1.1.0
release), the VCSL ones mapped from their file names, and the drums' pieces from drums.py. Every
file is fetched once into the cache (<cache>/<source>/<commit>/<path>, where `segno-library` looks
too), checked against its git blob id, and hashed. Run it again after changing the list; commit the
recipe it writes.
"""

import concurrent.futures
import dataclasses
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import urllib.parse
import urllib.request

import drums

SOURCES = {
    "vsco": {
        "name": "VSCO 2 Community Edition",
        "url": "https://github.com/sgossner/VSCO-2-CE",
        "repo": "sgossner/VSCO-2-CE",
        "commit": "28092772094b2d9f1148d84cea97f4545b8c687d",  # the 1.1.0 release, with its SFZ maps
        "license": "CC0-1.0",
    },
    "vcsl": {
        "name": "Versilian Community Sample Library",
        "url": "https://github.com/sgossner/VCSL",
        "repo": "sgossner/VCSL",
        "commit": "c1ea7bcc3c7309650ab0da9d15c9cd1fbc4a4c7e",
        "license": "CC0-1.0",
    },
}
SOURCES.update(drums.SOURCES)
RAW = "https://raw.githubusercontent.com"

# The Library's paths are permanent once released (factory-library.md L13): Samples/<family>/
# <instrument>/<articulation>. Sustains are cut at 8 s, pianos at 12 s, bells at 12 s. Every
# instrument plays at the pitch it sounds: VSCO maps the piccolo, the xylophone and the glockenspiel
# at their written pitch, an octave below it, so they move up an octave ("octave": 12).
VSCO = [
    # (Library path, VSCO's map, longest seconds, options)
    ("Strings/Violin section/Sustain", "ViolinEnsSusVib.sfz", 8, {"gain": 7.5}),
    ("Strings/Violin section/Pizzicato", "ViolinEnsPizz.sfz", 8, {"gain": 6}),
    ("Strings/Violin section/Spiccato", "ViolinEnsSpic.sfz", 8, {"gain": 1.5}),
    ("Strings/Violin section/Tremolo", "ViolinEnsTrem.sfz", 8, {"gain": 6.5}),
    ("Strings/Viola section/Sustain", "ViolaEnsSusVib.sfz", 8, {"gain": -3.5}),
    ("Strings/Cello section/Sustain", "CelloEnsSusVib.sfz", 8, {"gain": -2}),
    ("Strings/Cello section/Pizzicato", "CelloEnsPizz.sfz", 8, {"gain": 2}),
    ("Strings/Cello section/Spiccato", "CelloEnsSpic.sfz", 8, {"gain": 8}),
    ("Strings/Solo violin/Sustain", "SViolinVib.sfz", 8, {"gain": -2}),
    ("Strings/Solo violin/Pizzicato", "SViolinPizz.sfz", 8, {"gain": 3.5}),
    ("Strings/Double bass/Sustain", "ContrabassSusVB.sfz", 8, {"gain": -0.5}),
    ("Strings/Double bass/Pizzicato", "ContrabassPizz.sfz", 8, {"gain": 18.5}),
    ("Strings/Harp/Harp", "Harp.sfz", 8, {"gain": 12.5}),
    ("Woodwinds/Flute/Sustain", "FluteSusVib.sfz", 8, {"gain": 12}),
    ("Woodwinds/Flute/Staccato", "FluteStac.sfz", 8, {"gain": 17}),
    ("Woodwinds/Piccolo/Sustain", "PiccoloSus.sfz", 8, {"octave": 12, "gain": 3}),
    ("Woodwinds/Oboe/Sustain", "OboeSusVib.sfz", 8, {"gain": 9.5}),
    ("Woodwinds/Clarinet/Sustain", "ClarinetSus.sfz", 8, {"gain": 4}),
    ("Woodwinds/Clarinet/Staccato", "ClarinetStac.sfz", 8, {"gain": 6}),
    ("Woodwinds/Bassoon/Sustain", "BassoonSus.sfz", 8, {"gain": -5}),
    ("Woodwinds/Bassoon/Staccato", "BassoonStac.sfz", 8, {"gain": 3}),
    ("Brass/French horn/Sustain", "FHornSus.sfz", 8, {"gain": -0.5}),
    ("Brass/French horn/Muted", "FHornMute.sfz", 8, {"gain": 1}),
    ("Brass/Trumpet/Sustain", "TrumpetSus.sfz", 8, {"gain": 0}),
    ("Brass/Trumpet/Muted", "TrumpetStraightMuteSus.sfz", 8, {"gain": 11.5}),
    ("Brass/Trumpet/Staccato", "TrumpetStac.sfz", 8, {"gain": 12}),
    ("Brass/Trombone/Sustain", "TromboneSus.sfz", 8, {"gain": 5.5}),
    ("Brass/Trombone/Staccato", "TromboneStac.sfz", 8, {"gain": 1.5}),
    ("Brass/Tuba/Sustain", "TubaSus.sfz", 8, {"gain": 9}),
    # Two of its three loudnesses: the softest and the loudest.
    ("Keys/Upright piano/Upright piano", "UprightPiano.sfz", 12, {"layers": [0, 2], "gain": 9}),
    ("Keys/Pipe organ/Loud", "OrganLoud.sfz", 8, {"gain": 1.5}),
    ("Keys/Pipe organ/Soft", "OrganQuiet.sfz", 8, {"gain": 9.5}),
    ("Tuned percussion/Glockenspiel/Glockenspiel", "Glockenspiel.sfz", 8, {"octave": 12, "gain": 16.5}),
    ("Tuned percussion/Marimba/Marimba", "Marimba.sfz", 8, {"gain": 19.5}),
    ("Tuned percussion/Xylophone/Xylophone", "Xylophone.sfz", 8, {"octave": 12, "gain": -6}),
    ("Percussion/Timpani/Hits", "Timpani.sfz", 8, {"gain": 10}),
    ("Percussion/Timpani/Rolls", "TimpaniRolls.sfz", 8, {"gain": 10.5}),
    ("Percussion/Orchestral kit/Orchestral kit", "GM-StylePerc.sfz", 8, {"pitched": False, "gain": -6}),
]

# (Library path, folder in VCSL, a pattern naming note and, where it has them, loudness and round
# robin, the loudnesses kept (softest first), the octave its names count from, a short name for its
# samples, longest seconds, its level in dB)
VCSL = [
    ("Keys/Grand piano/Grand piano", "Chordophones/Zithers/Grand Piano, Kawai - Legacy/Sustains",
     r"_Sus_(?P<note>[A-G]#?\d)_v(?P<vel>\d)_rr(?P<rr>\d)\.wav$", ["2", "4"], "c3", "GrandPiano", 12, -0.5),
    ("Keys/Harpsichord/Harpsichord", "Chordophones/Zithers/Harpsichord, Flemish/Sustains/Low",
     r"_Far_(?P<note>[A-G]#?\d)_rr(?P<rr>\d)\.wav$", None, "c3", "Harpsichord", 8, 11),
    ("Tuned percussion/Vibraphone/Vibraphone", "Idiophones/Struck Idiophones/Vibraphone/Hard Mallets",
     r"_hard_(?P<note>[A-G]#?\d)_v(?P<vel>\d)_rr(?P<rr>\d)_Main\.wav$", ["2", "3"], "c3", "Vibraphone", 8, 5.5),
    ("Tuned percussion/Tubular bells/Tubular bells", "Idiophones/Struck Idiophones/Tubular Bells 1",
     r"chimes_(?P<note>[A-G]#?\d)_(?P<vel>p+|f+)_rr\d\.wav$", "soft-loud", "c3", "TubularBells", 12, 6.5),
    ("Tuned percussion/Kalimba/Kalimba", "Idiophones/Plucked Idiophones/Kalimba, Kenya",
     r"_MainSpirit_(?P<note>[A-G]#?\d)_k(?P<rr>\d+)_vl3_rr2\.wav$", None, "c3", "Kalimba", 8, 11.5),
    ("Tuned percussion/Wine glasses/Wine glasses", "Idiophones/Friction Idiophones/Wine Glasses/Sustains/Slow",
     r"glass\d_(?P<note>[A-G]#?\d)_Slow_\d_Main\.wav$", None, "c3", "WineGlass", 8, 35),
    ("Woodwinds/Recorder/Recorder", "Aerophones/Edge-blown Aerophones/Baroque Alto Recorder/Sustain",
     r"_Sus_(?P<note>[A-G]#?\d)_rr\d_Main\.wav$", None, "c3", "Recorder", 8, 28),
    ("Woodwinds/Ocarina/Ocarina", "Aerophones/Edge-blown Aerophones/Ocarina, Typical/Sustains/Sus",
     r"_Sus_(?P<note>[A-G]#?\d)\.wav$", None, "c3", "Ocarina", 8, 2),
    ("Woodwinds/Tenor sax/Tenor sax", "Aerophones/Reed Aerophones/Tenor Saxophone/Non-Vibrato",
     r"_NV_Main_(?P<note>[A-G]#?\d)_vl(?P<vel>\d)_rr(?P<rr>\d)\.wav$", ["2", "3"], "c3", "TenorSax", 8, 10),
    ("Woodwinds/Harmonica/Harmonica", "Aerophones/Free Aerophones/Harmonica-Hohner-Special20-C/Sustains/Normal",
     r"_Normal_(?P<note>[A-G]#?\d)\.wav$", None, "c3", "Harmonica", 8, 9),
]
DYNAMICS = ["ppp", "pp", "p", "mp", "mf", "f", "ff", "fff"]


def fetch(url):
    with urllib.request.urlopen(url, timeout=120) as r:
        return r.read()


def raw_url(source, path):
    s = SOURCES[source]
    return f"{RAW}/{s['repo']}/{s['commit']}/" + urllib.parse.quote(path)


def tree(source, cache):
    """A source's files at its commit, kept in the cache once fetched (through `gh` where it's there,
    as GitHub limits what it answers without an account)."""
    s = SOURCES[source]
    kept = os.path.join(cache, source, s["commit"] + ".tree.json")
    if os.path.exists(kept):
        data = json.load(open(kept))
    else:
        endpoint = f"repos/{s['repo']}/git/trees/{s['commit']}?recursive=1"
        if shutil.which("gh"):
            data = json.loads(subprocess.run(["gh", "api", endpoint], check=True, capture_output=True).stdout)
        else:
            data = json.loads(fetch("https://api.github.com/" + endpoint))
        if data.get("truncated"):
            sys.exit(f"{source}: the tree came back truncated")
        os.makedirs(os.path.dirname(kept), exist_ok=True)
        with open(kept, "w") as f:
            json.dump(data, f)
    return {e["path"]: e for e in data["tree"] if e["type"] == "blob"}


class Context:
    """What drums.py reads its sources through: their trees, a file's text, the SFZ parser."""

    def __init__(self, trees, cache):
        self.trees, self.cache = trees, cache
        self.parse_sfz = parse_sfz

    def tree(self, source):
        return self.trees[source]

    def text(self, source, path):
        local = os.path.join(self.cache, source, SOURCES[source]["commit"], path)
        if not os.path.exists(local):
            if path not in self.trees[source]:
                sys.exit(f"{source}: {path} isn't there")
            data = fetch(raw_url(source, path))
            os.makedirs(os.path.dirname(local), exist_ok=True)
            with open(local, "wb") as f:
                f.write(data)
        return open(local, "rb").read().decode("utf-8", "replace")


def note_number(name, octave):
    """A note name as these libraries write it; `octave` says whether C3 or C4 is 60."""
    m = re.fullmatch(r"([A-Ga-g])(#|b)?(-?\d+)", name)
    pc = {"c": 0, "d": 2, "e": 4, "f": 5, "g": 7, "a": 9, "b": 11}[m.group(1).lower()]
    pc += {"#": 1, "b": -1}.get(m.group(2), 0)
    return (int(m.group(3)) + (2 if octave == "c3" else 1)) * 12 + pc


def parse_sfz(text):
    """VSCO's maps: headers and opcodes, a region taking its control, global and group's."""
    text = re.sub(r"//[^\n]*", "", text)
    ctl, glob, grp, cur, level, regions = {}, {}, {}, None, None, []
    token = re.compile(r"<\w+>|[A-Za-z0-9_]+=.*?(?=\s+[A-Za-z0-9_]+=|\s*<|\s*$)", re.M)
    for tok in token.findall(text):
        if tok.startswith("<"):
            if cur is not None:
                regions.append({**ctl, **glob, **grp, **cur})
                cur = None
            level = tok
            if tok == "<control>":
                ctl = {}
            elif tok == "<global>":
                glob, grp = {}, {}
            elif tok == "<group>":
                grp = {}
            elif tok == "<region>":
                cur = {}
            continue
        key, value = tok.split("=", 1)
        target = {"<control>": ctl, "<global>": glob, "<group>": grp, "<region>": cur}.get(level)
        if target is not None:
            target[key] = value.strip()
    if cur is not None:
        regions.append({**ctl, **glob, **grp, **cur})
    return regions


def key_of(value):
    return int(value) if re.fullmatch(r"-?\d+", value) else note_number(value, "c4")  # SFZ's own c4 = 60


def common(regions, opcode):
    """What most regions say of an opcode (the Sampler has one envelope); None if most say nothing."""
    counts = {}
    for r in regions:
        counts[r.get(opcode)] = counts.get(r.get(opcode), 0) + 1
    best = max(counts, key=counts.get)
    return best


def vsco_instrument(path, map_name, seconds, options, files):
    regions = parse_sfz(fetch(raw_url("vsco", map_name)).decode("utf-8", "replace"))
    lower = {p.lower(): p for p in files}
    zones = []
    for r in regions:
        sample = (r.get("default_path", "") + r["sample"]).replace("\\", "/")
        sample = sample if sample in files else lower[sample.lower()]
        z = {
            "file": sample,
            "lokey": key_of(r.get("lokey", r.get("key", "0"))),
            "hikey": key_of(r.get("hikey", r.get("key", "127"))),
            "root": key_of(r.get("pitch_keycenter", r.get("key", "60"))),
            "lovel": int(r.get("lovel", 0)),
            "hivel": int(r.get("hivel", 127)),
        }
        for opcode, field in (("volume", "volume"), ("tune", "tune")):
            if float(r.get(opcode, 0)) != 0.0:
                z[field] = float(r[opcode])
        if int(r.get("seq_length", 1)) > 1:
            z["seq"] = [int(r["seq_length"]), int(r.get("seq_position", 1))]
        if "lorand" in r or "hirand" in r:
            z["rand"] = [float(r.get("lorand", 0)), float(r.get("hirand", 1))]
        zones.append(z)
    for z in zones:
        for field in ("lokey", "hikey", "root"):
            z[field] = min(127, z[field] + options.get("octave", 0))
    if "layers" in options:
        # Keep some loudnesses; the kept ones share 0-127 again.
        layers = sorted({(z["lovel"], z["hivel"]) for z in zones})
        kept = [layers[i] for i in options["layers"]]
        zones = [z for z in zones if (z["lovel"], z["hivel"]) in kept]
        for z in zones:
            i = kept.index((z["lovel"], z["hivel"]))
            z["lovel"] = 0 if i == 0 else kept[i][0]
            z["hivel"] = 127 if i == len(kept) - 1 else kept[i + 1][0] - 1
    level(zones, options.get("gain", 0))
    glob = {}
    for opcode in ("ampeg_attack", "ampeg_decay", "ampeg_sustain", "ampeg_release", "amp_veltrack"):
        value = common(regions, opcode)
        if value is not None:
            glob[opcode] = float(value)
    return {"sfz": f"Samples/{path}.sfz", "source": "vsco", "maxSeconds": seconds,
            "pitched": options.get("pitched", True), "global": glob, "zones": zones}


def vcsl_instrument(path, folder, pattern, velocities, octave, short, seconds, gain, files):
    found = []
    for p in sorted(files):
        if not p.startswith(folder + "/") or "/" in p[len(folder) + 1:]:
            continue
        m = re.search(pattern, p)
        if m:
            found.append((p, m.groupdict()))
    if not found:
        sys.exit(f"{path}: nothing in {folder} matches")
    # The loudnesses kept, softest first; with "soft-loud", each note's softer and louder.
    zones = []
    for p, g in found:
        note = note_number(g["note"], octave)
        if velocities == "soft-loud":
            layer = None
        elif velocities is None:
            layer = 0
        elif g["vel"] in velocities:
            layer = velocities.index(g["vel"])
        else:
            continue
        zones.append({"file": p, "root": note, "layer": layer, "vel": g.get("vel"), "rr": int(g.get("rr") or 1)})
    if velocities == "soft-loud":
        for note in {z["root"] for z in zones}:
            these = sorted((z for z in zones if z["root"] == note), key=lambda z: DYNAMICS.index(z["vel"]))
            for i, z in enumerate(these):
                z["layer"] = 0 if i == 0 else 1
        layers = 2
    else:
        layers = 1 if velocities is None else len(velocities)
    # Each loudness its share of 0-127; each note up to halfway to the next, the ends six beyond.
    out = []
    for layer in range(layers):
        these = [z for z in zones if z["layer"] == layer]
        roots = sorted({z["root"] for z in these})
        for z in these:
            i = roots.index(z["root"])
            lo = max(0, roots[0] - 6) if i == 0 else roots[i - 1] + (roots[i] - roots[i - 1] - 1) // 2 + 1
            hi = min(127, roots[-1] + 6) if i == len(roots) - 1 else roots[i] + (roots[i + 1] - roots[i] - 1) // 2
            turns = sorted(x["rr"] for x in these if x["root"] == z["root"])
            name = f"{short}_{segno_note(z['root'])}"
            if layers > 1:
                name += f"_{'soft' if layer == 0 and layers == 2 else 'loud' if layers == 2 else f'v{layer + 1}'}"
            zone = {"file": z["file"], "name": name, "lokey": lo, "hikey": hi, "root": z["root"],
                    "lovel": 128 * layer // layers, "hivel": 128 * (layer + 1) // layers - 1}
            if len(turns) > 1:
                zone["seq"] = [len(turns), turns.index(z["rr"]) + 1]
                zone["name"] += f"_{turns.index(z['rr']) + 1}"
            out.append(zone)
    level(out, gain)
    return {"sfz": f"Samples/{path}.sfz", "source": "vcsl", "maxSeconds": seconds, "pitched": True,
            "global": {}, "zones": out}


def level(zones, gain):
    """An instrument's level: `gain` dB on every zone's volume, so its C3 (or the note it plays an
    octave from it; a kit's kick) comes out near -22 dB RMS, peaking below -3 dBFS, as instrument
    presets do. SFZ's <global> volume can't: a region's own volume replaces it."""
    for z in zones:
        v = round(z.get("volume", 0.0) + gain, 2)
        if v:
            z["volume"] = v
        else:
            z.pop("volume", None)


def segno_note(key):
    """A note as Segno names it (C3 = 60), for a file name: "Cs3"."""
    return ["C", "Cs", "D", "Ds", "E", "F", "Fs", "G", "Gs", "A", "As", "B"][key % 12] + str(key // 12 - 2)


def git_blob_id(data):
    return hashlib.sha1(b"blob %d\0" % len(data) + data).hexdigest()


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    cache = sys.argv[1]
    trees = {s: tree(s, cache) for s in SOURCES}
    instruments = [vsco_instrument(*i, trees["vsco"]) for i in VSCO]
    instruments += [vcsl_instrument(*i, trees["vcsl"]) for i in VCSL]
    kits = drums.kits(Context(trees, cache))
    instruments += [p.instrument() for p in drums.PIECES.values()]

    # Every file once (a drum's every microphone): fetched into the cache unless it's there, checked,
    # hashed.
    wanted = {(i["source"], part["file"]) for i in instruments for z in i["zones"] for part in z.get("mix", [z])}
    hashes = {}

    def get(item):
        source, path = item
        local = os.path.join(cache, source, SOURCES[source]["commit"], path)
        if os.path.exists(local):
            data = open(local, "rb").read()
        else:
            data = fetch(raw_url(source, path))
        if git_blob_id(data) != trees[source][path]["sha"]:
            sys.exit(f"{source}: {path} isn't the file its commit has")
        if not os.path.exists(local):
            os.makedirs(os.path.dirname(local), exist_ok=True)
            with open(local + ".part", "wb") as f:
                f.write(data)
            os.replace(local + ".part", local)
        return item, hashlib.sha256(data).hexdigest(), len(data)

    total = 0
    with concurrent.futures.ThreadPoolExecutor(8) as pool:
        for n, (item, digest, size) in enumerate(pool.map(get, sorted(wanted)), 1):
            hashes[item] = digest
            total += size
            if n % 100 == 0 or n == len(wanted):
                print(f"{n}/{len(wanted)} files, {total / 1e6:.0f} MB", flush=True)
    for i in instruments:
        for z in i["zones"]:
            for part in z.get("mix", [z]):
                part["sha256"] = hashes[(i["source"], part["file"])]

    sources = {k: {"name": v["name"], "url": v["url"], "raw": f"{RAW}/{v['repo']}", "commit": v["commit"],
                   "license": v["license"]} for k, v in SOURCES.items()}
    recipe = {"segnoLibraryRecipe": 1, "version": "1.0.0", "sources": sources, "instruments": instruments}
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "recipe.json")
    with open(out, "w") as f:
        json.dump(recipe, f, indent=1)
        f.write("\n")
    print(f"{len(instruments)} instruments, {sum(len(i['zones']) for i in instruments)} zones, "
          f"{len(wanted)} files ({total / 1e6:.0f} MB) in {out}")
    out = os.path.join(os.path.dirname(out), "kits.json")
    with open(out, "w") as f:
        json.dump({"segnoKitRecipes": 1, "author": "Segno", "roles": drums.ROLES,
                   "kits": [dataclasses.asdict(k) for k in kits]}, f, indent=1, ensure_ascii=False)
        f.write("\n")
    print(f"{len(kits)} kits in {out}")


if __name__ == "__main__":
    main()
