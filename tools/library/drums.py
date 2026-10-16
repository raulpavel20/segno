"""Segno's factory kits (docs/design/drum-kit.md §27, ADR 0068): the drums' sources, every kit piece
as a Library instrument, and the kits made of them.

make_recipe.py adds the pieces to tools/library/recipe.json, so the Library build fetches and makes
them; make_kits.py measures them in a built Library and writes the kits' presets.

A piece is an SFZ in Samples/Drums/<pool>/, a pool being one recorded kit, one machine or one
recordist's set: a kit draws from one pool. Its zones are the source's own velocity layers and takes
(round robins), read from the source's own maps where it has them, else from its file names,
thinned to at most `layers` x `takes`. A zone can mix the source's microphones at their gains, so a
pad sounds like a kit, not like one microphone. Every piece is levelled as one in the Library: its
loudest zone peaks at -1 dBFS, its softer layers as far below it as they were recorded.

Names follow ADR 0067: no makers' or model names in the Library's folders or the kits.
"""

import os
import re
from dataclasses import dataclass, field

# ---- the sources (ADR 0068: CC0, checked from the rights holder, pinned) ------------------------

SOURCES = {
    "virtuosity": {
        "name": "Virtuosity Drums (Versilian Studios, Karoryfer Samples)",
        "url": "https://github.com/sfzinstruments/virtuosity_drums",
        "repo": "sfzinstruments/virtuosity_drums",
        "commit": "9f04cf9a734527edfbb0a4eee1f674e45bbf71bc",  # 0.925
        "license": "CC0-1.0",
    },
    "bodyperc": {
        "name": "Body Percussion (sfzinstruments)",
        "url": "https://github.com/sfzinstruments/body_percussion",
        "repo": "sfzinstruments/body_percussion",
        "commit": "4ac9d8966679c648b62fa10a188179e186b97f24",
        "license": "CC0-1.0",
    },
    "fischer": {
        # Michael Fischer's recordings (1994), CC0 by their repackager, TidalCycles (ADR 0068).
        "name": "Michael Fischer's 808 recordings (TidalCycles' repackaging)",
        "url": "https://github.com/tidalcycles/sounds-tr808-fischer",
        "repo": "tidalcycles/sounds-tr808-fischer",
        "commit": "85fbecf1bec32553395625ea659e2a56dfd7c0e1",
        "license": "CC0-1.0",
    },
}

# ---- pieces ---------------------------------------------------------------------------------------

LEVEL = -1.0  # every piece's loudest zone, dBFS


@dataclass
class Piece:
    pool: str  # its folder under Samples/Drums
    name: str  # its file: "Kick", "Snare rimshot"
    source: str
    seconds: float
    zones: list = field(default_factory=list)

    @property
    def sfz(self):
        return f"Samples/Drums/{self.pool}/{self.name}.sfz"

    def instrument(self):
        return {"sfz": self.sfz, "source": self.source, "maxSeconds": self.seconds, "pitched": False,
                "normalize": LEVEL, "global": {}, "zones": self.zones}


PIECES = {}  # by SFZ path


def _short(name):
    """A piece's name as its samples begin: "Snare rimshot" -> "SnareRimshot"."""
    return "".join(w[:1].upper() + w[1:] for w in re.split(r"[^A-Za-z0-9]+", name) if w)


def _add(piece):
    if piece.sfz in PIECES:
        raise SystemExit(f"{piece.sfz}: made twice")
    PIECES[piece.sfz] = piece
    return piece.sfz


def _velocity(region):
    return int(region.get("lovel", 0)), int(region.get("hivel", 127))


def _layers(regions, layers, takes):
    """The regions as [(layer, take, takes in it, lovel, hivel, region)], thinned to `layers` velocity
    layers spread from softest to loudest and `takes` takes each; the kept layers share 0-127 again."""
    groups = {}
    for r in regions:
        groups.setdefault(_velocity(r), []).append(r)
    keys = sorted(groups)
    if len(keys) > layers:
        keys = [keys[i] for i in sorted({round(i * (len(keys) - 1) / (layers - 1)) for i in range(layers)})]
    out = []
    for li, k in enumerate(keys):
        these = sorted(groups[k], key=lambda r: int(r.get("seq_position", 0)))[:takes]
        lo = 0 if li == 0 else k[0]
        hi = 127 if li == len(keys) - 1 else keys[li + 1][0] - 1
        for ti, r in enumerate(these):
            out.append((li, ti, len(these), lo, hi, r))
    return out


def mapped(ctx, pool, name, source, maps, seconds, layers=6, takes=3):
    """A piece from the source's own maps, one per microphone: [(map path, gain dB, pan)], their regions
    in the same order. Their samples are relative to the maps' program folder."""
    tree = ctx.tree(source)
    per_mic = []
    for path, gain, pan in maps:
        regions = ctx.parse_sfz(ctx.text(source, path))
        if not regions:
            raise SystemExit(f"{source}: {path} maps nothing")
        program = path.split("/mappings/")[0] if "/mappings/" in path else os.path.dirname(path)
        for r in regions:
            sample = os.path.normpath(os.path.join(program, r["sample"].replace("\\", "/")))
            if sample not in tree:
                raise SystemExit(f"{source}: {path} names {sample}, which isn't there")
            r["_file"] = sample
        per_mic.append((_layers(regions, layers, takes), gain, pan))
    first = per_mic[0][0]
    for kept, _, _ in per_mic[1:]:
        if [(l, t, lo, hi) for l, t, _, lo, hi, _ in kept] != [(l, t, lo, hi) for l, t, _, lo, hi, _ in first]:
            raise SystemExit(f"{source}: {pool}/{name}'s microphones aren't mapped alike")
    zones = []
    for i, (li, ti, count, lo, hi, region) in enumerate(first):
        zone = {"name": f"{_short(name)}_v{li + 1}_{ti + 1}", "lovel": lo, "hivel": hi}
        if count > 1:
            zone["seq"] = [count, ti + 1]
        parts = [{"file": kept[i][5]["_file"], "gain": gain, "pan": pan} for kept, gain, pan in per_mic]
        if len(parts) == 1 and parts[0]["gain"] == 0 and parts[0]["pan"] == 0:
            zone["file"] = parts[0]["file"]
        else:
            zone["mix"] = parts
        zones.append(zone)
    return _add(Piece(pool, name, source, seconds, zones))


def files(ctx, pool, name, source, paths, seconds, as_layers=False):
    """A piece from files: each a take (round robin), or with `as_layers` each a velocity layer, softest
    first."""
    tree = ctx.tree(source)
    for p in paths:
        if p not in tree:
            raise SystemExit(f"{source}: {p} isn't there")
    zones = []
    n = len(paths)
    for i, p in enumerate(paths):
        zone = {"file": p, "name": f"{_short(name)}_{i + 1}" if n > 1 else _short(name)}
        if n > 1 and as_layers:
            zone["lovel"], zone["hivel"] = 128 * i // n, 128 * (i + 1) // n - 1
        elif n > 1:
            zone["seq"] = [n, i + 1]
        zones.append(zone)
    return _add(Piece(pool, name, source, seconds, zones))


# ---- kits -----------------------------------------------------------------------------------------

# A pad's role sets its level in the kit (make_kits.py): its piece's punch at the loudest layer (the
# loudest 25 ms, dB RMS), as a mix would balance them. A pad never peaks above -3 dBFS at full
# velocity, so a very spiky piece (a snap) sits a little under its role.
ROLES = {
    "kick": -14.0, "snare": -15.0, "clap": -16.0, "rim": -20.0, "tom": -18.0, "hat": -24.0,
    "open hat": -23.0, "cymbal": -22.0, "ride": -24.0, "perc": -20.0, "shaker": -25.0, "fx": -21.0,
}


@dataclass
class Pad:
    note: int
    piece: str  # its SFZ path
    name: str
    role: str
    choke: int = 0
    pan: float = 0.0  # -1 left .. 1 right
    velocity: float = 30.0  # the Sampler's Velocity: a piece with layers needs less
    transpose: int = 0
    trim: float = 0.0  # dB on top of its role's level


@dataclass
class Kit:
    name: str
    tags: list
    description: str
    pads: list


def kits(ctx):
    """Every factory kit, its pieces added to PIECES as it goes."""
    PIECES.clear()
    out = []
    out.append(_808_classic(ctx))
    out.append(_jazz_sticks(ctx))
    out.append(_body(ctx))
    return out


# GM's drum notes, the kits' places (drum-kit.md §27.2).
KICK, SIDESTICK, SNARE, CLAP, SNARE2, LOWTOM_F, CLOSED, LOWTOM, PEDAL, MIDTOM_L, OPEN, MIDTOM, HIGHTOM_L, \
    CRASH, HIGHTOM, RIDE, CHINA, BELL, TAMB, SPLASH, COWBELL, CRASH2, VIBRASLAP, RIDE2 = range(36, 60)
HIGHBONGO, LOWBONGO, MUTECONGA, OPENCONGA, LOWCONGA, HIGHTIMBALE, LOWTIMBALE, HIGHAGOGO, LOWAGOGO, \
    CABASA, MARACAS, SHORTWHISTLE, LONGWHISTLE, SHORTGUIRO, LONGGUIRO, CLAVES, HIGHBLOCK, LOWBLOCK, \
    MUTECUICA, OPENCUICA, MUTETRIANGLE, OPENTRIANGLE = range(60, 82)


def _808_classic(ctx):
    """The 808 as it left the factory: every knob near its middle."""
    pool, src = "808", "fischer"

    def piece(name, path, seconds):
        return files(ctx, pool, name, src, [path], seconds)

    pads = [
        Pad(KICK, piece("Kick", "bd8/BD5050.WAV", 3), "Kick", "kick", velocity=60),
        Pad(SIDESTICK, piece("Rimshot", "rs8/RS.WAV", 1), "Rimshot", "rim", velocity=60),
        Pad(SNARE, piece("Snare", "sd8/SD5050.WAV", 1.5), "Snare", "snare", velocity=60),
        Pad(CLAP, piece("Clap", "cp8/CP.WAV", 1.5), "Clap", "clap", velocity=60),
        Pad(LOWTOM_F, piece("Low tom", "lt8/LT50.WAV", 2), "Low tom", "tom", pan=-0.3, velocity=60),
        Pad(CLOSED, piece("Closed hat", "ch8/CH.WAV", 1), "Closed hat", "hat", choke=1, velocity=60),
        Pad(LOWTOM, piece("Low conga", "lc8/LC50.WAV", 1.5), "Low conga", "perc", pan=-0.2, velocity=60),
        Pad(MIDTOM_L, piece("Mid tom", "mt8/MT50.WAV", 2), "Mid tom", "tom", velocity=60),
        Pad(OPEN, piece("Open hat", "oh8/OH50.WAV", 2), "Open hat", "open hat", choke=1, velocity=60),
        Pad(MIDTOM, piece("Mid conga", "mc8/MC50.WAV", 1.5), "Mid conga", "perc", velocity=60),
        Pad(HIGHTOM_L, piece("High tom", "ht8/HT50.WAV", 2), "High tom", "tom", pan=0.3, velocity=60),
        Pad(CRASH, piece("Cymbal", "cy8/CY5050.WAV", 5), "Cymbal", "cymbal", velocity=60),
        Pad(HIGHTOM, piece("High conga", "hc8/HC50.WAV", 1.5), "High conga", "perc", pan=0.2, velocity=60),
        Pad(COWBELL, piece("Cowbell", "cb8/CB.WAV", 1.5), "Cowbell", "perc", velocity=60),
        Pad(MARACAS, piece("Maracas", "ma8/MA.WAV", 1), "Maracas", "shaker", velocity=60),
        Pad(CLAVES, piece("Claves", "cl8/CL.WAV", 1), "Claves", "perc", velocity=60),
    ]
    return Kit("808 classic", ["Drums", "Electronic", "Classic"],
               "The analog machine as it left the factory, every knob near its middle: round kick, "
               "snappy snare, ticking hats, congas and the cowbell.", pads)


def _jazz_sticks(ctx):
    """Virtuosity's jazz kit with sticks: close microphones on kick and snare, the overheads, the
    ribbon and the room together."""
    pool, src = "Jazz kit", "virtuosity"
    m = "Programs/mappings"

    def piece(name, articulation, close, seconds, layers=6, takes=3):
        mics = [(f"{m}/{close}/{articulation}_map.sfz", 0.0, 0.0)] if close else []
        mics += [(f"{m}/oh/{articulation}_map.sfz", -4.0 if close else 0.0, 0.0),
                 (f"{m}/mid/{articulation}_map.sfz", -8.0 if close else -4.0, 0.0),
                 (f"{m}/room/{articulation}_map.sfz", -10.0 if close else -6.0, 0.0)]
        return mapped(ctx, pool, name, src, mics, seconds, layers, takes)

    pads = [
        Pad(KICK, piece("Kick", "kick_snon", "kickmic", 2.5), "Kick", "kick"),
        Pad(SIDESTICK, piece("Cross-stick", "snare_crossstick", "snaremic", 1.5), "Cross-stick", "rim"),
        Pad(SNARE, piece("Snare", "snare_center", "snaremic", 2.5, layers=8), "Snare", "snare"),
        Pad(CLAP, piece("Rimshot", "snare_rimshot", "snaremic", 2.5), "Rimshot", "snare", trim=-2),
        Pad(SNARE2, piece("Snare off-centre", "snare_offcenter", "snaremic", 2.5), "Off-centre", "snare", trim=-2),
        Pad(LOWTOM_F, piece("Low tom", "ltom_center", None, 3.5), "Low tom", "tom", pan=-0.25),
        Pad(CLOSED, piece("Closed hat", "hh_closed", None, 1.5), "Closed hat", "hat", choke=1, pan=0.2),
        Pad(LOWTOM, piece("High tom", "htom_center", None, 3.0), "High tom", "tom", pan=0.15),
        Pad(PEDAL, piece("Pedal hat", "hh_pedal", None, 1.0), "Pedal hat", "hat", choke=1, pan=0.2),
        Pad(OPEN, piece("Open hat", "hh_open", None, 3.0), "Open hat", "open hat", choke=1, pan=0.2),
        Pad(MIDTOM_L, piece("Half-open hat", "hh_half", None, 2.0), "Half hat", "hat", choke=1, pan=0.2),
        Pad(CRASH, piece("Crash", "crash_crash", None, 6.0), "Crash", "cymbal", pan=-0.3),
        Pad(RIDE, piece("Ride", "ride_ride", None, 6.0), "Ride", "ride", pan=0.3),
        Pad(CHINA, piece("Flat ride", "flatride_ride", None, 6.0), "Flat ride", "ride", pan=-0.2),
        Pad(BELL, piece("Ride bell", "ride_bell", None, 5.0), "Ride bell", "ride", pan=0.3),
        Pad(SPLASH, piece("Sizzle crash", "crash_sizzle", None, 6.0), "Sizzle", "cymbal", pan=-0.3),
    ]
    return Kit("Jazz sticks", ["Drums", "Acoustic", "Jazz"],
               "A small jazz kit played with sticks in a club room: a deep snare from whisper to crack, "
               "a breathing ride, a flat ride and a sizzle.", pads)


def _body(ctx):
    """Claps, snaps, stomps and the body's own drums, two strengths and several takes each."""
    pool, src = "Body", "bodyperc"

    def piece(name, mapping, seconds=1.5):
        return mapped(ctx, pool, name, src, [(f"Programs/mappings/body_{mapping}.sfz", 0.0, 0.0)], seconds,
                      layers=2, takes=6)

    pads = [
        Pad(KICK, piece("Stomp", "stomp_l"), "Stomp", "kick", velocity=40),
        Pad(SIDESTICK, piece("Finger snap", "snap_r"), "Snap", "rim", velocity=40, pan=0.2),
        Pad(SNARE, piece("Handclap", "handclap"), "Clap", "clap", velocity=40),
        Pad(CLAP, piece("Finger clap", "fingerclap"), "Finger clap", "clap", velocity=40, trim=-3),
        Pad(SNARE2, piece("Chest", "chestthump_l"), "Chest", "snare", velocity=40, trim=-2),
        Pad(LOWTOM_F, piece("Belly", "bellycenter_l"), "Belly", "tom", velocity=40),
        Pad(CLOSED, piece("Pinky snap", "pinkysnap_r"), "Pinky snap", "hat", velocity=40, pan=0.3),
        Pad(LOWTOM, piece("Belly side", "bellyside_r"), "Belly side", "tom", velocity=40, pan=0.2),
        Pad(PEDAL, piece("Heel", "heel_l"), "Heel", "perc", velocity=40, pan=-0.2),
        Pad(MIDTOM_L, piece("Chest side", "chestside_r"), "Chest side", "tom", velocity=40, pan=0.2),
        Pad(OPEN, piece("Snap left", "snap_l"), "Snap left", "rim", velocity=40, pan=-0.3),
        Pad(MIDTOM, piece("Half stomp", "halfstomp_r"), "Half stomp", "kick", velocity=40, trim=-4),
        Pad(HIGHTOM_L, piece("Cheek", "cheek_rout"), "Cheek", "perc", velocity=40, pan=0.3),
        Pad(CRASH, piece("Head", "head"), "Head", "perc", velocity=40),
        Pad(HIGHTOM, piece("Shoulder", "shoulderout_r"), "Shoulder", "perc", velocity=40, pan=0.3),
        Pad(RIDE, piece("Cheek pop", "cheek_lin"), "Cheek pop", "perc", velocity=40, pan=-0.3),
    ]
    return Kit("Body", ["Drums", "Percussion", "Organic"],
               "Everything a body plays: stomps for a kick, claps for a snare, snaps, chest and belly "
               "drums, a heel and a cheek pop.", pads)
