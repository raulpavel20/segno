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
    "bigrusty": {
        "name": "Big Rusty Drums (Karoryfer Samples)",
        "url": "https://github.com/sfzinstruments/karoryfer.big-rusty-drums",
        "repo": "sfzinstruments/karoryfer.big-rusty-drums",
        "commit": "f07ce00df34a46b6b08375be56fe116cf15782bc",  # 1.100
        "license": "CC0-1.0",
    },
    "swirly": {
        "name": "Swirly Drums (Karoryfer Samples)",
        "url": "https://github.com/sfzinstruments/karoryfer.swirly-drums",
        "repo": "sfzinstruments/karoryfer.swirly-drums",
        "commit": "c40dafe0011cb2e54c0c220ff0fa308a11fc60f5",  # 1.104
        "license": "CC0-1.0",
    },
    "unruly": {
        "name": "Unruly Drums (Karoryfer Samples)",
        "url": "https://github.com/sfzinstruments/karoryfer.unruly-drums",
        "repo": "sfzinstruments/karoryfer.unruly-drums",
        "commit": "9bf75c2a1392f190cd1c264645653629f0b3a097",  # 1.100
        "license": "CC0-1.0",
    },
    "gogodze2": {
        "name": "Gogodze Phu Vol. II (Karoryfer Samples)",
        "url": "https://github.com/sfzinstruments/karoryfer.gogodze-phu-vol-ii",
        "repo": "sfzinstruments/karoryfer.gogodze-phu-vol-ii",
        "commit": "69a0274cdc39c99c3098cde1bc3789690de86d62",  # 1.001
        "license": "CC0-1.0",
    },
    "machines": {
        # Recordings of drum machines by their owner (Mck): an analog 606-style machine, a digital
        # machine's classic voices, an 80s digital machine and a 90s groove box (ADR 0068).
        "name": "MckSamplePacks (MckAudio)",
        "url": "https://github.com/MckAudio/MckSamplePacks",
        "repo": "MckAudio/MckSamplePacks",
        "commit": "5db40e8fe26785c256845a5bb38921b2654a887f",
        "license": "CC0-1.0",
    },
    "fpsynth": {
        "name": "FreePats Synthesizer Percussion",
        "url": "https://github.com/freepats/synthesizer-percussion",
        "repo": "freepats/synthesizer-percussion",
        "commit": "39bbabce8e0e10aa259d5d94fca329f668733185",
        "license": "CC0-1.0",
    },
    "lmms": {
        "name": "LMMS assets",
        "url": "https://github.com/LMMS/assets",
        "repo": "LMMS/assets",
        "commit": "bc09e0745c69c73bd6c3112122031e4963ce3876",
        "license": "CC0-1.0",
    },
    "sonicpi": {
        # Its samples from Freesound, "placed in the public domain via the Creative Commons 0 License".
        "name": "Sonic Pi's samples",
        "url": "https://github.com/sonic-pi-net/sonic-pi",
        "repo": "sonic-pi-net/sonic-pi",
        "commit": "7b39b6639705f801dd9d907d6c1715b8c795a376",
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
    """A piece once, however many kits of its pool play it: the second time it's asked for, the same
    piece (made alike, or it's a mistake)."""
    if piece.sfz in PIECES:
        if PIECES[piece.sfz] != piece:
            raise SystemExit(f"{piece.sfz}: made twice, two ways")
        return piece.sfz
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
    """A piece from the source's own maps, one per microphone: [(map path, gain dB, pan)] or
    [(map path, gain dB, pan, pattern)], the pattern picking the map's regions by their samples' names
    where one map holds several articulations or microphones. Their regions come in the same order;
    their samples are relative to the maps' program folder."""
    tree = ctx.tree(source)
    per_mic = []
    for path, gain, pan, *pattern in maps:
        regions = ctx.parse_sfz(ctx.text(source, path))
        if pattern:
            regions = [r for r in regions if re.search(pattern[0], os.path.basename(r["sample"]))]
        if not regions:
            raise SystemExit(f"{source}: {path} maps nothing{' for ' + pattern[0] if pattern else ''}")
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
    out.append(_funk_studio(ctx))
    out.append(_dusty(ctx))
    out.append(_rock(ctx))
    out.append(_mallets(ctx))
    out.append(_brushes(ctx))
    out.append(_indie(ctx))
    out.append(_seventies(ctx))
    out.append(_808_boom(ctx))
    out.append(_808_trap(ctx))
    out.append(_909_house(ctx))
    out.append(_909_techno(ctx))
    out.append(_707_dance(ctx))
    out.append(_machine_percussion(ctx))
    out.append(_analog_606(ctx))
    out.append(_80s_digital(ctx))
    out.append(_80s_electronic(ctx))
    out.append(_90s_club(ctx))
    out.append(_90s_studio(ctx))
    out.append(_synth_percussion(ctx))
    out.append(_drum_and_bass(ctx))
    out.append(_hardstyle(ctx))
    out.append(_hip_hop(ctx))
    out.append(_future(ctx))
    out.append(_electro_lab(ctx))
    out.append(_minimal(ctx))
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


def _virtuosity_mono(ctx, pool, name, articulation, mic, seconds, layers=6, takes=3, room=None):
    """A Virtuosity piece through one microphone, a little of the room beside it where asked."""
    m = "Programs/mappings"
    mics = [(f"{m}/{mic}/{articulation}_map.sfz", 0.0, 0.0)]
    if room is not None:
        mics.append((f"{m}/room/{articulation}_map.sfz", room, 0.0))
    return mapped(ctx, pool, name, "virtuosity", mics, seconds, layers, takes)


def _funk_studio(ctx):
    """The jazz kit dried out: close microphones up front, the overheads under them, no room; the
    snare half-muted for tight backbeats and ghost notes."""
    pool, src = "Studio kit", "virtuosity"
    m = "Programs/mappings"

    def piece(name, articulation, close, seconds, layers=6, takes=3):
        mics = [(f"{m}/{close}/{articulation}_map.sfz", 0.0, 0.0)] if close else []
        mics += [(f"{m}/oh/{articulation}_map.sfz", -9.0 if close else 0.0, 0.0)]
        return mapped(ctx, pool, name, src, mics, seconds, layers, takes)

    pads = [
        Pad(KICK, piece("Kick", "kick_snoff", "kickmic", 1.5), "Kick", "kick"),
        Pad(SIDESTICK, piece("Cross-stick", "snare_crossstick", "snaremic", 1.0), "Cross-stick", "rim"),
        Pad(SNARE, piece("Snare", "snare_halfopen", "snaremic", 1.5, layers=8), "Snare", "snare"),
        Pad(CLAP, piece("Snare muted", "snare_muted", "snaremic", 1.0, layers=8), "Muted", "snare", trim=-2),
        Pad(SNARE2, piece("Snare open", "snare_center", "snaremic", 2.0, layers=8), "Open snare", "snare"),
        Pad(LOWTOM_F, piece("Low tom", "ltom_muted", None, 2.0), "Low tom", "tom", pan=-0.25),
        Pad(CLOSED, piece("Closed hat", "hh_closed", None, 1.0), "Closed hat", "hat", choke=1, pan=0.2),
        Pad(LOWTOM, piece("High tom", "htom_center", None, 2.0), "High tom", "tom", pan=0.15),
        Pad(PEDAL, piece("Pedal hat", "hh_pedal", None, 1.0), "Pedal hat", "hat", choke=1, pan=0.2),
        Pad(OPEN, piece("Open hat", "hh_34", None, 2.0), "Open hat", "open hat", choke=1, pan=0.2),
        Pad(MIDTOM_L, piece("Half-open hat", "hh_half", None, 1.5), "Half hat", "hat", choke=1, pan=0.2),
        Pad(CRASH, piece("Crash", "crash_crash", None, 4.0), "Crash", "cymbal", pan=-0.3),
        Pad(RIDE, piece("Ride", "ride_ride", None, 4.0), "Ride", "ride", pan=0.3),
        Pad(BELL, piece("Ride bell", "ride_bell", None, 3.0), "Ride bell", "ride", pan=0.3),
    ]
    return Kit("Funk studio", ["Drums", "Acoustic", "Funk"],
               "A dry studio kit for funk and soul: a tight half-muted snare that loves ghost notes, a "
               "short kick, crisp hats, no room.", pads)


def _dusty(ctx):
    """The jazz kit through its 1950s ribbon-era vintage microphone and a touch of room: worn, mid-
    heavy, ready for boom bap and lo-fi."""
    pool = "Dusty kit"
    pads = [
        Pad(KICK, _virtuosity_mono(ctx, pool, "Kick", "kick_snon", "lofi", 1.5, room=-12), "Kick", "kick"),
        Pad(SIDESTICK, _virtuosity_mono(ctx, pool, "Cross-stick", "snare_crossstick", "lofi", 1.0, room=-12),
            "Cross-stick", "rim"),
        Pad(SNARE, _virtuosity_mono(ctx, pool, "Snare", "snare_center", "lofi", 1.5, 8, room=-12), "Snare", "snare"),
        Pad(CLAP, _virtuosity_mono(ctx, pool, "Rimshot", "snare_rimshot", "lofi", 1.5, room=-12), "Rimshot", "snare",
            trim=-2),
        Pad(LOWTOM_F, _virtuosity_mono(ctx, pool, "Low tom", "ltom_center", "lofi", 2.0, room=-12), "Low tom", "tom"),
        Pad(CLOSED, _virtuosity_mono(ctx, pool, "Closed hat", "hh_closed", "lofi", 1.0, room=-12), "Closed hat",
            "hat", choke=1),
        Pad(LOWTOM, _virtuosity_mono(ctx, pool, "High tom", "htom_center", "lofi", 2.0, room=-12), "High tom", "tom"),
        Pad(PEDAL, _virtuosity_mono(ctx, pool, "Pedal hat", "hh_pedal", "lofi", 1.0, room=-12), "Pedal hat", "hat",
            choke=1),
        Pad(OPEN, _virtuosity_mono(ctx, pool, "Open hat", "hh_open", "lofi", 2.5, room=-12), "Open hat", "open hat",
            choke=1),
        Pad(CRASH, _virtuosity_mono(ctx, pool, "Crash", "crash_crash", "lofi", 4.0, room=-12), "Crash", "cymbal"),
        Pad(RIDE, _virtuosity_mono(ctx, pool, "Ride", "ride_ride", "lofi", 4.0, room=-12), "Ride", "ride"),
    ]
    return Kit("Dusty", ["Drums", "Acoustic", "Lo-fi"],
               "The jazz kit heard through a 1950s microphone: warm, worn and mid-heavy, made for boom bap and "
               "lo-fi beats.", pads)


def _rock(ctx):
    """An early-1980s kit hit hard with sticks: a damped 24-inch kick, a deep snare, four toms, and
    a full set of cymbals, each close microphone with the overheads."""
    pool, src = "Vintage rock kit", "bigrusty"
    m = "Programs/mappings"

    def drum(name, folder, art, seconds, mics, layers=6, takes=3):
        return mapped(ctx, pool, name, src, [(f"{m}/{folder}/{art}_{mic}.sfz", g, 0.0) for mic, g in mics],
                      seconds, layers, takes)

    snare = [("top", 0.0), ("btm", -8.0), ("oh", -5.0)]
    pads = [
        Pad(KICK, drum("Kick", "kick_24", "k", 2.0, [("kick", 0.0), ("oh", -6.0), ("sn", -14.0)]), "Kick", "kick"),
        Pad(SIDESTICK, drum("Side-stick", "snare_14", "sn_ss", 1.0, snare), "Side-stick", "rim"),
        Pad(SNARE, drum("Snare", "snare_14", "sn_center", 2.0, snare, layers=8), "Snare", "snare"),
        Pad(CLAP, drum("Rimshot", "snare_14", "sn_rims", 2.0, snare), "Rimshot", "snare"),
        Pad(SNARE2, drum("Snare edge", "snare_14", "sn_edge", 2.0, snare), "Edge", "snare", trim=-2),
        Pad(LOWTOM_F, drum("Floor tom", "tom_22", "tom_22", 3.0, [("cl", 0.0), ("oh", -5.0), ("sn", -14.0)]),
            "Floor tom", "tom", pan=-0.35),
        Pad(CLOSED, drum("Closed hat", "hihat_14", "ht_cl", 1.0, [("cl", 0.0), ("oh", -3.0)]), "Closed hat",
            "hat", choke=1, pan=0.25),
        Pad(LOWTOM, drum("Low tom", "tom_18", "tom_18", 3.0, [("cl", 0.0), ("oh", -5.0), ("sn", -14.0)]),
            "Low tom", "tom", pan=-0.2),
        Pad(PEDAL, drum("Pedal hat", "hihat_14", "ht_chik", 1.0, [("cl", 0.0), ("oh", -3.0)]), "Pedal hat", "hat",
            choke=1, pan=0.25),
        Pad(MIDTOM_L, drum("Mid tom", "tom_15", "tom_15", 2.5, [("cl", 0.0), ("oh", -5.0), ("sn", -14.0)]),
            "Mid tom", "tom", pan=0.0),
        Pad(OPEN, drum("Open hat", "hihat_14", "ht_open", 3.0, [("cl", 0.0), ("oh", -3.0)]), "Open hat",
            "open hat", choke=1, pan=0.25),
        Pad(MIDTOM, drum("High tom", "tom_14", "tom_14", 2.5, [("cl", 0.0), ("oh", -5.0), ("sn", -14.0)]),
            "High tom", "tom", pan=0.2),
        Pad(CRASH, drum("Crash", "crash_17", "cr", 6.0, [("cl", -4.0), ("oh", 0.0)]), "Crash", "cymbal", pan=-0.35),
        Pad(RIDE, drum("Ride", "ride_22", "rd", 6.0, [("cl", -4.0), ("oh", 0.0)]), "Ride", "ride", pan=0.35),
        Pad(CHINA, drum("China", "china_18", "cn", 6.0, [("cl", -4.0), ("oh", 0.0)]), "China", "cymbal", pan=0.45),
        Pad(BELL, drum("Ride bell", "ride_22", "rd_bl", 5.0, [("cl", -4.0), ("oh", 0.0)]), "Ride bell", "ride",
            pan=0.35),
        Pad(CRASH2, drum("Sizzle crash", "crash_sizzle_17", "crs_cr", 6.0, [("cl", -4.0), ("oh", 0.0)]),
            "Sizzle crash", "cymbal", pan=-0.45),
    ]
    return Kit("Rock", ["Drums", "Acoustic", "Rock"],
               "An early-80s kit hit hard: a damped 24-inch kick, a deep snare with rimshots, four toms and a "
               "full set of cymbals, a china among them.", pads)


def _mallets(ctx):
    """The same kit played with soft mallets: toms and cymbals that swell and rumble, for film and
    ambient music."""
    pool, src = "Vintage rock kit", "bigrusty"
    m = "Programs/mappings"

    def drum(name, folder, art, seconds, mics, layers=6, takes=3):
        return mapped(ctx, pool, name, src, [(f"{m}/{folder}/{art}_{mic}.sfz", g, 0.0) for mic, g in mics],
                      seconds, layers, takes)

    toms = [("cl", 0.0), ("oh", -4.0), ("sn", -14.0)]
    cym = [("cl", -4.0), ("oh", 0.0)]
    pads = [
        Pad(KICK, drum("Kick", "kick_24", "k", 2.0, [("kick", 0.0), ("oh", -6.0), ("sn", -14.0)]), "Kick", "kick"),
        Pad(SNARE, drum("Snare mallet", "snare_14", "sn_mallet", 2.5, [("top", 0.0), ("btm", -8.0), ("oh", -4.0)]),
            "Snare", "snare"),
        Pad(LOWTOM_F, drum("Floor tom mallet", "tom_22", "tom_22_mallet", 4.0, toms), "Floor tom", "tom",
            pan=-0.35),
        Pad(LOWTOM, drum("Low tom mallet", "tom_18", "tom_18_mallet", 3.5, toms), "Low tom", "tom", pan=-0.2),
        Pad(MIDTOM_L, drum("Mid tom mallet", "tom_15", "tom_15_mallet", 3.0, toms), "Mid tom", "tom"),
        Pad(MIDTOM, drum("High tom mallet", "tom_14", "tom_14_mallet", 3.0, toms), "High tom", "tom", pan=0.2),
        Pad(CRASH, drum("Crash mallet", "crash_17", "cr_mallet", 8.0, cym), "Crash", "cymbal", pan=-0.35),
        Pad(RIDE, drum("Ride mallet", "ride_22", "rd_mallet", 8.0, cym), "Ride", "ride", pan=0.35, trim=4),
        Pad(CHINA, drum("China mallet", "china_18", "cn_mallet", 8.0, cym), "China", "cymbal", pan=0.45),
        Pad(SPLASH, drum("Sizzle mallet", "ride_sizzle_19", "rds_mallet", 8.0, cym), "Sizzle", "cymbal", pan=-0.45),
    ]
    return Kit("Mallets", ["Drums", "Acoustic", "Cinematic"],
               "The rock kit played with soft mallets: booming toms and cymbals that swell and wash, for film "
               "and ambient music.", pads)


def _brushes(ctx):
    """A kit made for brushes, heard in a small room: the snare's hits, digs and stirs, a soft
    marching kick, and cymbals that hiss."""
    pool, src = "Brushed kit", "swirly"
    m = "Programs/mappings/wet"

    def piece(name, map_name, seconds, layers=6, takes=3, more=()):
        return mapped(ctx, pool, name, src, [(f"{m}/{map_name}.sfz", 0.0, 0.0), *more], seconds, layers, takes)

    pads = [
        Pad(KICK, piece("Kick", "kick", 2.0), "Kick", "kick"),
        Pad(SIDESTICK, piece("Snare edge", "snare_edge_top", 1.5, more=[(f"{m}/snare_edge_btm.sfz", -6.0, 0.0)]),
            "Edge", "snare", trim=-3),
        Pad(SNARE, piece("Snare", "snare_main_top", 1.5, layers=8, more=[(f"{m}/snare_main_btm.sfz", -6.0, 0.0)]),
            "Snare", "snare"),
        Pad(CLAP, piece("Snare dig", "snare_dig_top", 1.5, more=[(f"{m}/snare_dig_btm.sfz", -6.0, 0.0)]), "Dig",
            "snare", trim=-2),
        Pad(SNARE2, piece("Stir", "snare_stir_on_notes", 2.0), "Stir", "snare", trim=-4),
        Pad(LOWTOM_F, piece("Floor tom", "tom_floor", 3.0), "Floor tom", "tom", pan=-0.3),
        Pad(CLOSED, piece("Closed hat", "hat_closed", 1.0), "Closed hat", "hat", choke=1, pan=0.25),
        Pad(LOWTOM, piece("Low tom", "tom_mlow", 2.5), "Low tom", "tom", pan=-0.15),
        Pad(PEDAL, piece("Pedal hat", "hat_foot", 1.0), "Pedal hat", "hat", choke=1, pan=0.25),
        Pad(MIDTOM_L, piece("Mid tom", "tom_xmed", 2.5), "Mid tom", "tom"),
        Pad(OPEN, piece("Open hat", "hat_open", 2.5), "Open hat", "open hat", choke=1, pan=0.25),
        Pad(MIDTOM, piece("High tom", "tom_mhi", 2.5), "High tom", "tom", pan=0.15),
        Pad(CRASH, piece("Crash", "crash", 5.0), "Crash", "cymbal", pan=-0.35),
        Pad(RIDE, piece("Ride", "ride", 5.0), "Ride", "ride", pan=0.35),
        Pad(CHINA, piece("China", "china", 5.0), "China", "cymbal", pan=0.4),
        Pad(SPLASH, piece("Splash", "splash", 4.0), "Splash", "cymbal", pan=-0.25),
    ]
    return Kit("Brushes", ["Drums", "Acoustic", "Jazz"],
               "A kit played with brushes in a small room: snare hits, digs and stirs, a soft kick, and "
               "cymbals that hiss and shimmer.", pads)


def _indie(ctx):
    """A kit with snare wires on every drum: a buzzing kick, three snares with the wires on or off
    as toms, a loose hat and two cymbals; close microphones and two overheads panned."""
    pool, src = "Wired kit", "unruly"
    m = "Programs/mappings"

    def piece(name, folder, art, mics, seconds, layers=6, takes=3):
        return mapped(ctx, pool, name, src, [(f"{m}/{folder}/{art}_{mic}_map.sfz", g, pan) for mic, g, pan in mics],
                      seconds, layers, takes)

    snare = [("top", 0.0, 0.0), ("btm", -8.0, 0.0), ("ohl", -5.0, -1.0), ("ohr", -5.0, 1.0)]
    tom = [("top", 0.0, 0.0), ("ohl", -5.0, -1.0), ("ohr", -5.0, 1.0)]  # no bottom microphone on its toms
    cym = [("cl", -4.0, 0.0), ("oh", 0.0, 0.0)]
    hat = [("cl", 0.0, 0.0), ("ohl", -4.0, -1.0), ("ohr", -4.0, 1.0)]
    pads = [
        Pad(KICK, piece("Kick", "k20", "k20_dirty", [("in", 0.0, 0.0), ("out", -4.0, 0.0), ("oh", -8.0, 0.0)], 2.0),
            "Kick", "kick"),
        Pad(SIDESTICK, piece("Side-stick", "s14", "s14_sstick", snare, 1.0), "Side-stick", "rim"),
        Pad(SNARE, piece("Snare", "s14", "s14_center", snare, 2.0, layers=8), "Snare", "snare"),
        Pad(CLAP, piece("Rimshot", "s14", "s14_rimshot", snare, 2.0), "Rimshot", "snare"),
        Pad(SNARE2, piece("Small snare", "s13", "s13_center", snare, 2.0), "Small snare", "snare"),
        Pad(LOWTOM_F, piece("Floor tom", "s22", "s22_tom_dirty", tom, 3.0), "Floor tom", "tom", pan=-0.3),
        Pad(CLOSED, piece("Closed hat", "h14", "h14_closed_tip", hat, 1.0), "Closed hat", "hat", choke=1, pan=0.25),
        Pad(LOWTOM, piece("Low tom", "s14", "s14_tom_dirty", tom, 2.5), "Low tom", "tom", pan=-0.1),
        Pad(PEDAL, piece("Pedal hat", "h14", "h14_footchik", hat, 1.0), "Pedal hat", "hat", choke=1, pan=0.25),
        Pad(MIDTOM_L, piece("High tom", "s13", "s13_tom_dirty", tom, 2.5), "High tom", "tom", pan=0.15),
        Pad(OPEN, piece("Open hat", "h14", "h14_open_tip", hat, 3.0), "Open hat", "open hat", choke=1, pan=0.25),
        Pad(MIDTOM, piece("Loose hat", "h14", "h14_loose_tip", hat, 2.0), "Loose hat", "hat", choke=1, pan=0.25),
        Pad(CRASH, piece("Crash", "c16", "c16_bow", cym, 5.0), "Crash", "cymbal", pan=-0.35),
        Pad(RIDE, piece("Ride", "r20", "r20_bow", cym, 5.0), "Ride", "ride", pan=0.35),
        Pad(BELL, piece("Ride bell", "r20", "r20_bell", cym, 4.0), "Ride bell", "ride", pan=0.35),
        Pad(SPLASH, piece("Crash edge", "c16", "c16_edge", cym, 5.0), "Crash edge", "cymbal", pan=-0.35),
        Pad(HIGHBLOCK, piece("Woodblock", "wb", "wb_hi", [("cl", 0.0, 0.0), ("oh", -6.0, 0.0)], 1.0), "Woodblock",
            "perc", pan=0.2),
    ]
    return Kit("Indie", ["Drums", "Acoustic", "Indie"],
               "A kit with snare wires on every drum: a buzzing kick, snares turned into toms, a loose hat and "
               "trashy cymbals. Garage and indie rock.", pads)


def _seventies(ctx):
    """A kit recorded for a 1973 sound: a deep kick, a fat snare, three toms and a hi-hat, its
    close microphones with the room's front, side and overhead."""
    pool, src = "Seventies kit", "gogodze2"
    m = "Programs/mappings"

    def piece(name, prefix, mics, seconds, pattern=None, layers=6, takes=3):
        maps = [(f"{m}/{prefix}_{mic}_mic.sfz", g, 0.0, *([pattern] if pattern else [])) for mic, g in mics]
        return mapped(ctx, pool, name, src, maps, seconds, layers, takes)

    drum = [("front", -3.0), ("oh", -6.0), ("side", -8.0)]
    pads = [
        Pad(KICK, piece("Kick", "kick", [("kick", 0.0), *drum], 2.0), "Kick", "kick"),
        Pad(SIDESTICK, piece("Side-stick", "snare", [("snare", 0.0), *drum], 1.0, r"^ss_"), "Side-stick", "rim"),
        Pad(SNARE, piece("Snare", "snare", [("snare", 0.0), *drum], 2.0, r"^sc_"), "Snare", "snare"),
        Pad(SNARE2, piece("Snare edge", "snare", [("snare", 0.0), *drum], 2.0, r"^se_"), "Edge", "snare", trim=-2),
        Pad(LOWTOM_F, piece("Low tom", "toml", [("front", 0.0), ("oh", -3.0), ("side", -6.0)], 3.0), "Low tom",
            "tom", pan=-0.3),
        Pad(CLOSED, piece("Closed hat", "hc", [("oh", 0.0), ("front", -4.0)], 1.0), "Closed hat", "hat", choke=1,
            pan=0.25),
        Pad(LOWTOM, piece("Mid tom", "tomm", [("front", 0.0), ("oh", -3.0), ("side", -6.0)], 2.5), "Mid tom", "tom"),
        Pad(PEDAL, piece("Pedal hat", "hf", [("oh", 0.0), ("front", -4.0)], 1.0), "Pedal hat", "hat", choke=1,
            pan=0.25),
        Pad(MIDTOM_L, piece("High tom", "tomh", [("front", 0.0), ("oh", -3.0), ("side", -6.0)], 2.5), "High tom",
            "tom", pan=0.25),
        Pad(OPEN, piece("Open hat", "ho", [("oh", 0.0), ("front", -4.0)], 3.0), "Open hat", "open hat", choke=1,
            pan=0.25),
        Pad(MIDTOM, piece("Tight hat", "ht", [("oh", 0.0), ("front", -4.0)], 1.0), "Tight hat", "hat", choke=1,
            pan=0.25),
        Pad(HIGHTOM_L, piece("Loose hat", "hl", [("oh", 0.0), ("front", -4.0)], 2.0), "Loose hat", "hat", choke=1,
            pan=0.25),
    ]
    return Kit("70s soul", ["Drums", "Acoustic", "Soul"],
               "A kit recorded for a 1973 sound: a deep thud of a kick, a fat snare, three toms and a hi-hat "
               "that opens wide. Soul, funk and breaks.", pads)


# ---- electronic kits --------------------------------------------------------------------------

def _one(ctx, pool, source, seconds=2.0, velocity=60):
    """A pad maker for a pool of single samples: (note, file, name, role, more Pad options)."""

    def pad(note, path, name, role, piece=None, seconds_=None, **options):
        sfz = files(ctx, pool, piece or name, source, [path] if isinstance(path, str) else path,
                    seconds_ or seconds)
        return Pad(note, sfz, name, role, velocity=options.pop("velocity", velocity), **options)

    return pad


def _808_boom(ctx):
    """The 808 with its kick's decay all the way up and its tone down: a long, booming kick under a
    darker snare, for hip-hop and electro."""
    pad = _one(ctx, "808", "fischer")
    pads = [
        pad(KICK, "bd8/BD0010.WAV", "Kick", "kick", piece="Kick boom", seconds_=3.5),
        pad(SIDESTICK, "rs8/RS.WAV", "Rimshot", "rim", seconds_=1),
        pad(SNARE, "sd8/SD0025.WAV", "Snare", "snare", piece="Snare dark", seconds_=1.5),
        pad(CLAP, "cp8/CP.WAV", "Clap", "clap", seconds_=1.5),
        pad(LOWTOM_F, "lt8/LT10.WAV", "Low tom", "tom", piece="Low tom long", seconds_=3, pan=-0.3),
        pad(CLOSED, "ch8/CH.WAV", "Closed hat", "hat", piece="Closed hat", seconds_=1, choke=1),
        pad(MIDTOM_L, "mt8/MT10.WAV", "Mid tom", "tom", piece="Mid tom long", seconds_=3),
        pad(OPEN, "oh8/OH75.WAV", "Open hat", "open hat", piece="Open hat long", seconds_=2, choke=1),
        pad(HIGHTOM_L, "ht8/HT10.WAV", "High tom", "tom", piece="High tom long", seconds_=3, pan=0.3),
        pad(CRASH, "cy8/CY0075.WAV", "Cymbal", "cymbal", piece="Cymbal dark", seconds_=5),
        pad(COWBELL, "cb8/CB.WAV", "Cowbell", "perc", seconds_=1.5),
        pad(CLAVES, "cl8/CL.WAV", "Claves", "perc", seconds_=1),
    ]
    return Kit("808 boom", ["Drums", "Electronic", "Hip-hop"],
               "The 808 with the kick's decay all the way up and its tone down: a long boom under a darker "
               "snare and long toms. Hip-hop, electro, Miami bass.", pads)


def _808_trap(ctx):
    """Trap's 808: a short kick and a long sub-kick, a bright snappy snare, the clap, tight hats
    and the rimshot."""
    pad = _one(ctx, "808", "fischer")
    pads = [
        pad(35, "bd8/BD2510.WAV", "808", "kick", piece="Kick long", seconds_=4, trim=-1),
        pad(KICK, "bd8/BD7500.WAV", "Kick", "kick", piece="Kick short", seconds_=1.5),
        pad(SIDESTICK, "rs8/RS.WAV", "Rimshot", "rim", seconds_=1),
        pad(SNARE, "sd8/SD7575.WAV", "Snare", "snare", piece="Snare bright", seconds_=1.5),
        pad(CLAP, "cp8/CP.WAV", "Clap", "clap", seconds_=1.5),
        pad(SNARE2, "sd8/SD5010.WAV", "Snare snappy", "snare", piece="Snare snappy", seconds_=1.5, trim=-2),
        pad(CLOSED, "ch8/CH.WAV", "Closed hat", "hat", piece="Closed hat", seconds_=1, choke=1),
        pad(OPEN, "oh8/OH25.WAV", "Open hat", "open hat", piece="Open hat short", seconds_=1.5, choke=1),
        pad(LOWTOM, "lt8/LT50.WAV", "Low tom", "tom", piece="Low tom", seconds_=2, pan=-0.3),
        pad(MIDTOM, "mt8/MT50.WAV", "Mid tom", "tom", piece="Mid tom", seconds_=2),
        pad(HIGHTOM, "ht8/HT50.WAV", "High tom", "tom", piece="High tom", seconds_=2, pan=0.3),
        pad(COWBELL, "cb8/CB.WAV", "Cowbell", "perc", seconds_=1.5),
    ]
    return Kit("808 trap", ["Drums", "Electronic", "Trap"],
               "Trap's 808: a long sub-kick to play as a bass and a short kick for the punch, a bright snappy "
               "snare, the clap and tight hats for rolls.", pads)


def _machine_file(ctx, folder, name):
    """A file of the machines' recordings by its name, without its number: "TR8/BD", "909_Bass_Drum_1"."""
    tree = ctx.tree("machines")
    found = [p for p in tree if p.startswith(folder + "/") and re.fullmatch(r"\d{3}_" + re.escape(name) + r"\.wav",
                                                                               os.path.basename(p))]
    if len(found) != 1:
        raise SystemExit(f"machines: {folder}/{name}: {len(found)} files")
    return found[0]


def _modern(ctx, pool):
    """A pad maker for the digital machine's voices: (note, folder, name in the machine, our name, role)."""
    one = _one(ctx, pool, "machines")

    def pad(note, folder, name, ours, role, **options):
        return one(note, _machine_file(ctx, "TR8/" + folder, name), ours, role, **options)

    return pad


def _909_house(ctx):
    pad = _modern(ctx, "909")
    pads = [
        pad(35, "BD", "909_Bass_Drum_2", "Kick 2", "kick", piece="Kick 2"),
        pad(KICK, "BD", "909_Bass_Drum_1", "Kick", "kick"),
        pad(SIDESTICK, "PERC", "909_Rim_Shot", "Rimshot", "rim", seconds_=1),
        pad(SNARE, "SD", "909_Snare_Drum_1", "Snare", "snare"),
        pad(CLAP, "PERC", "909_Hand_Clap", "Clap", "clap"),
        pad(SNARE2, "SD", "909_Snare_Drum_2", "Snare 2", "snare", piece="Snare 2"),
        pad(LOWTOM_F, "TOMS", "909_Low_Tom", "Low tom", "tom", pan=-0.3),
        pad(CLOSED, "HATS", "909_Closed_HiHat", "Closed hat", "hat", choke=1, seconds_=1),
        pad(MIDTOM_L, "TOMS", "909_Mid_Tom", "Mid tom", "tom"),
        pad(OPEN, "HATS", "909_Open_HiHat", "Open hat", "open hat", choke=1),
        pad(HIGHTOM_L, "TOMS", "909_High_Tom", "High tom", "tom", pan=0.3),
        pad(CRASH, "HATS", "909_Crash_Cymbal", "Crash", "cymbal", seconds_=4),
        pad(RIDE, "HATS", "909_Ride_Cymbal", "Ride", "ride", seconds_=4),
        pad(CHINA, "PERC", "909_Hand_Clap_Long", "Clap long", "clap", piece="Clap long"),
    ]
    return Kit("909 house", ["Drums", "Electronic", "House"],
               "The 909 as house knows it: the round kick, the snare and clap on two and four, open hats on "
               "the off-beat and the ride.", pads)


def _909_techno(ctx):
    pad = _modern(ctx, "909")
    pads = [
        pad(35, "BD", "909_Bass_Drum_3", "Kick 3", "kick", piece="Kick 3"),
        pad(KICK, "BD", "909_AttackBD_1", "Kick", "kick", piece="Kick attack"),
        pad(SIDESTICK, "PERC", "909_Rim_Shot", "Rimshot", "rim", seconds_=1),
        pad(SNARE, "SD", "909_AttackSD_1", "Snare", "snare", piece="Snare attack"),
        pad(CLAP, "PERC", "909_Hand_Clap", "Clap", "clap"),
        pad(SNARE2, "SD", "909_Snare_Drum_3", "Snare 3", "snare", piece="Snare 3"),
        pad(LOWTOM_F, "TOMS", "909_Low_Tom_Short", "Low tom", "tom", piece="Low tom short", pan=-0.3),
        pad(CLOSED, "HATS", "909_Closed_HiHat_Short", "Closed hat", "hat", piece="Closed hat short", choke=1,
            seconds_=1),
        pad(MIDTOM_L, "TOMS", "909_Mid_Tom_Short", "Mid tom", "tom", piece="Mid tom short"),
        pad(OPEN, "HATS", "909_Open_HiHat_Short", "Open hat", "open hat", piece="Open hat short", choke=1),
        pad(HIGHTOM_L, "TOMS", "909_High_Tom_Short", "High tom", "tom", piece="High tom short", pan=0.3),
        pad(CRASH, "HATS", "909_Crash_Cymbal_Short", "Crash", "cymbal", piece="Crash short", seconds_=3),
        pad(RIDE, "HATS", "909_Ride_Cymbal_Short", "Ride", "ride", piece="Ride short", seconds_=3),
        pad(CHINA, "BD", "909_AttackBD_3", "Kick hard", "kick", piece="Kick attack hard", trim=-1),
    ]
    return Kit("909 techno", ["Drums", "Electronic", "Techno"],
               "The 909 tightened for techno: its attack kick and snare, short hats and toms, a short ride "
               "for driving sixteenths.", pads)


def _707_dance(ctx):
    pad = _modern(ctx, "707")
    pads = [
        pad(35, "BD", "707_Bass_Drum_2", "Kick 2", "kick", piece="Kick 2"),
        pad(KICK, "BD", "707_Bass_Drum_1", "Kick", "kick"),
        pad(SIDESTICK, "PERC", "707_Rim_Shot", "Rimshot", "rim", seconds_=1),
        pad(SNARE, "SD", "707_Snare_Dum_1", "Snare", "snare"),
        pad(CLAP, "PERC", "707_Hand_Clap", "Clap", "clap"),
        pad(SNARE2, "SD", "707_Snare_Dum_2", "Snare 2", "snare", piece="Snare 2"),
        pad(LOWTOM_F, "TOMS", "707_Low_Tom", "Low tom", "tom", pan=-0.3),
        pad(CLOSED, "HATS", "707_Closed_HiHat", "Closed hat", "hat", choke=1, seconds_=1),
        pad(MIDTOM_L, "TOMS", "707_Mid_Tom", "Mid tom", "tom"),
        pad(OPEN, "HATS", "707_Open_HiHat", "Open hat", "open hat", choke=1),
        pad(HIGHTOM_L, "TOMS", "707_High_Tom", "High tom", "tom", pan=0.3),
        pad(CRASH, "HATS", "707_Crash_Cymbal", "Crash", "cymbal", seconds_=4),
        pad(RIDE, "HATS", "707_Ride_Cymbal", "Ride", "ride", seconds_=4),
        pad(TAMB, "PERC", "707_Tambourine", "Tambourine", "shaker"),
        pad(CHINA, "SD", "707_Snare_Dum_3", "Snare 3", "snare", piece="Snare 3"),
    ]
    return Kit("707 dance", ["Drums", "Electronic", "Dance"],
               "The 707's bright digital drums: punchy kick, crisp snare, a tambourine, the sound of 80s dance "
               "and early house.", pads)


def _machine_percussion(ctx):
    pad = _modern(ctx, "Machine percussion")
    pads = [
        pad(HIGHBONGO, "PERC", "727_High_Bongo", "High bongo", "perc", pan=0.2),
        pad(LOWBONGO, "PERC", "727_Low_Bongo", "Low bongo", "perc", pan=0.1),
        pad(MUTECONGA, "PERC", "727_Mute_High_Conga", "Mute conga", "perc", pan=-0.1),
        pad(OPENCONGA, "PERC", "727_Open_High_Conga", "High conga", "perc", pan=-0.15),
        pad(LOWCONGA, "PERC", "727_Low_Conga", "Low conga", "perc", pan=-0.25),
        pad(HIGHTIMBALE, "PERC", "727_High_Timbale", "High timbale", "perc", pan=0.3),
        pad(LOWTIMBALE, "PERC", "727_Low_Timbale", "Low timbale", "perc", pan=0.25),
        pad(HIGHAGOGO, "PERC", "727_Agogo_High", "High agogo", "perc", pan=0.35),
        pad(LOWAGOGO, "PERC", "727_Agogo_Low", "Low agogo", "perc", pan=0.35),
        pad(CABASA, "PERC", "727_Cabasa", "Cabasa", "shaker", pan=-0.3),
        pad(MARACAS, "PERC", "808_Maracas", "Maracas", "shaker", pan=0.3),
        pad(SHORTWHISTLE, "PERC", "727_Short_Whistle", "Short whistle", "fx"),
        pad(LONGWHISTLE, "PERC", "727_Long_Whistle", "Long whistle", "fx"),
        pad(CLAVES, "PERC", "808_Claves_1", "Claves", "perc", pan=-0.2),
        pad(MUTECUICA, "PERC", "727_Quijada", "Quijada", "perc"),
        pad(COWBELL, "PERC", "808_Cowbell", "Cowbell", "perc"),
        pad(OPENTRIANGLE, "PERC", "727_Star_Chime", "Star chime", "fx", seconds_=3),
        pad(TAMB, "PERC", "707_Tambourine", "Tambourine", "shaker"),
    ]
    return Kit("Machine percussion", ["Drums", "Percussion", "Electronic"],
               "A drum machine's Latin percussion: congas, bongos, timbales, agogos, a quijada and whistles, "
               "in the bright digital colour of the 80s.", pads)


def _analog_606(ctx):
    """An analog 606-style machine recorded by its owner: knobs at several settings each (chosen
    by decay; the toms are its two, low and high)."""
    pad = _one(ctx, "606", "machines")
    d = "RD6"
    pads = [
        pad(35, f"{d}/BD/004.wav", "Kick long", "kick", piece="Kick long", seconds_=2),
        pad(KICK, f"{d}/BD/001.wav", "Kick", "kick", seconds_=1),
        pad(SNARE, f"{d}/SD/001.wav", "Snare", "snare", seconds_=1),
        pad(CLAP, f"{d}/CLAP/002.wav", "Clap", "clap", seconds_=1.5),
        pad(SNARE2, f"{d}/SD/003.wav", "Snare long", "snare", piece="Snare long", seconds_=2),
        pad(LOWTOM_F, f"{d}/TOMS/001.wav", "Low tom", "tom", seconds_=1, pan=-0.3),
        pad(CLOSED, f"{d}/HATS/002.wav", "Closed hat", "hat", choke=1, seconds_=1),
        pad(LOWTOM, f"{d}/TOMS/004.wav", "Low tom long", "tom", piece="Low tom long", seconds_=2, pan=-0.3),
        pad(PEDAL, f"{d}/HATS/006.wav", "Half hat", "hat", piece="Half hat", choke=1, seconds_=1),
        pad(MIDTOM_L, f"{d}/TOMS/006.wav", "High tom", "tom", seconds_=1, pan=0.3),
        pad(OPEN, f"{d}/HATS/007.wav", "Open hat", "open hat", choke=1, seconds_=1.5),
        pad(MIDTOM, f"{d}/TOMS/008.wav", "High tom long", "tom", piece="High tom long", seconds_=2, pan=0.3),
        pad(CRASH, f"{d}/HATS/015.wav", "Cymbal", "cymbal", seconds_=4),
        pad(RIDE, f"{d}/HATS/012.wav", "Cymbal short", "cymbal", piece="Cymbal short", seconds_=3),
    ]
    return Kit("606 analog", ["Drums", "Electronic", "Classic"],
               "A small analog machine in the 606's mould, recorded by its owner: a thumpy kick, a noisy "
               "snare, sizzling hats and two toms. Acid and minimal.", pads)


def _rx(ctx, pool):
    one = _one(ctx, pool, "machines")

    def pad(note, folder, name, ours, role, **options):
        return one(note, _machine_file(ctx, "RX5/" + folder, name), ours, role, **options)

    return pad


def _80s_digital(ctx):
    pad = _rx(ctx, "80s digital")
    pads = [
        pad(35, "BD", "Heavy_Bass_Drum_1", "Kick heavy", "kick", piece="Kick heavy"),
        pad(KICK, "BD", "Bass_Drum_1", "Kick", "kick"),
        pad(SIDESTICK, "SD", "Rim_Shot_1", "Rimshot", "rim", seconds_=1),
        pad(SNARE, "SD", "Snare_Drum_1", "Snare", "snare"),
        pad(CLAP, "PERC", "Claps", "Clap", "clap"),
        pad(SNARE2, "SD", "Heavy_Snare_Drum_1", "Snare heavy", "snare", piece="Snare heavy"),
        pad(LOWTOM_F, "TOMS", "Tom_4", "Floor tom", "tom", pan=-0.35),
        pad(CLOSED, "HATS", "HiHat_Closed", "Closed hat", "hat", choke=1, seconds_=1),
        pad(LOWTOM, "TOMS", "Tom_3", "Low tom", "tom", pan=-0.15),
        pad(OPEN, "HATS", "HiHat_Open", "Open hat", "open hat", choke=1),
        pad(MIDTOM_L, "TOMS", "Tom_2", "Mid tom", "tom", pan=0.1),
        pad(MIDTOM, "TOMS", "Tom_1", "High tom", "tom", pan=0.3),
        pad(CRASH, "HATS", "Crash_Cymbal", "Crash", "cymbal", seconds_=4),
        pad(RIDE, "HATS", "Ride_Edge", "Ride", "ride", seconds_=4),
        pad(BELL, "HATS", "Ride_Cup", "Ride bell", "ride", seconds_=3),
        pad(TAMB, "PERC", "Tambourine", "Tambourine", "shaker"),
        pad(COWBELL, "PERC", "Cowbell", "Cowbell", "perc"),
        pad(MARACAS, "PERC", "Shaker", "Shaker", "shaker"),
    ]
    return Kit("80s digital", ["Drums", "Electronic", "80s"],
               "An 80s digital drum machine's sampled kit: tight kick, big snare, clap and four toms. Pop, "
               "synth-pop and new wave.", pads)


def _80s_electronic(ctx):
    pad = _rx(ctx, "80s digital")
    pads = [
        pad(35, "BD", "Electric_Bass_Drum", "Kick electric", "kick", piece="Kick electric"),
        pad(KICK, "BD", "Process_Bass_Drum_1", "Kick", "kick", piece="Kick processed"),
        pad(SIDESTICK, "SD", "Rim_Shot_2", "Rimshot", "rim", piece="Rimshot 2", seconds_=1),
        pad(SNARE, "SD", "Process_Snare_Drum_1", "Snare", "snare", piece="Snare processed"),
        pad(CLAP, "PERC", "Claps", "Clap", "clap"),
        pad(SNARE2, "SD", "Electric_Snare_Drum", "Snare electric", "snare", piece="Snare electric"),
        pad(LOWTOM_F, "TOMS", "Process_Tom_3", "Low tom", "tom", piece="Tom processed low", pan=-0.3),
        pad(CLOSED, "HATS", "HiHat_Closed", "Closed hat", "hat", choke=1, seconds_=1),
        pad(MIDTOM_L, "TOMS", "Process_Tom_2", "Mid tom", "tom", piece="Tom processed mid"),
        pad(OPEN, "HATS", "HiHat_Open", "Open hat", "open hat", choke=1),
        pad(HIGHTOM_L, "TOMS", "Process_Tom_1", "High tom", "tom", piece="Tom processed high", pan=0.3),
        pad(CRASH, "HATS", "Crash_Cymbal", "Crash", "cymbal", seconds_=4),
        pad(CHINA, "TOMS", "FM_Tom", "FM tom", "tom", piece="FM tom"),
        pad(RIDE, "SD", "FM_Snare_Drum", "FM snare", "snare", piece="FM snare"),
        pad(SPLASH, "TOMS", "Hybrid_Tom", "Hybrid tom", "tom", piece="Hybrid tom"),
        pad(COWBELL, "MISC", "FM_Percussion_1", "FM perc", "perc", piece="FM perc 1"),
        pad(CRASH2, "MISC", "FM_Percussion_2", "FM perc 2", "perc", piece="FM perc 2"),
        pad(VIBRASLAP, "TOMS", "Process_Roto", "Roto", "tom", piece="Roto"),
    ]
    return Kit("80s electronic", ["Drums", "Electronic", "80s"],
               "The same 80s machine's electronic side: processed kick and snare, synth toms and FM "
               "percussion. Electro-funk and synth-pop.", pads)


def _dr(ctx, pool):
    one = _one(ctx, pool, "machines")

    def pad(note, folder, name, ours, role, **options):
        return one(note, _machine_file(ctx, "DR5/" + folder, name), ours, role, **options)

    return pad


def _90s_club(ctx):
    pad = _dr(ctx, "90s groove box")
    pads = [
        pad(35, "BD", "Dance_Kick", "Kick 2", "kick", piece="Kick dance"),
        pad(KICK, "BD", "House_Kick_1", "Kick", "kick", piece="Kick house"),
        pad(SIDESTICK, "PERC", "Claves", "Claves", "perc", seconds_=1),
        pad(SNARE, "SD", "House_Snare", "Snare", "snare", piece="Snare house"),
        pad(CLAP, "PERC", "TR-808_Clap", "Clap", "clap", piece="Clap"),
        pad(SNARE2, "SD", "Dance_Snare", "Snare 2", "snare", piece="Snare dance"),
        pad(LOWTOM_F, "PERC", "Conga_Low_Open", "Low conga", "perc", piece="Conga low", pan=-0.3),
        pad(CLOSED, "HATS", "Pop_Closed_Hi-hat", "Closed hat", "hat", piece="Closed hat pop", choke=1, seconds_=1),
        pad(LOWTOM, "PERC", "Conga_High_Open", "High conga", "perc", piece="Conga high", pan=-0.15),
        pad(PEDAL, "HATS", "Pop_Pedal_Hi-hat", "Pedal hat", "hat", piece="Pedal hat pop", choke=1, seconds_=1),
        pad(MIDTOM_L, "PERC", "Conga_High_Slap", "Conga slap", "perc", piece="Conga slap"),
        pad(OPEN, "HATS", "Pop_Open_Hi-hat", "Open hat", "open hat", piece="Open hat pop", choke=1),
        pad(MIDTOM, "PERC", "Tambourine", "Tambourine", "shaker"),
        pad(HIGHTOM_L, "PERC", "Shaker", "Shaker", "shaker"),
        pad(CRASH, "HATS", "Crash_Cymbal_1", "Crash", "cymbal", piece="Crash", seconds_=4),
        pad(RIDE, "HATS", "Ride_Cymbal", "Ride", "ride", piece="Ride", seconds_=4),
        pad(CHINA, "FX", "Reverse_Cymbal", "Reverse cymbal", "fx", seconds_=3),
        pad(BELL, "FX", "Scratch_Push", "Scratch", "fx"),
        pad(TAMB, "SD", "Noise_Snare", "Noise snare", "snare", piece="Snare noise"),
        pad(SPLASH, "FX", "High_Q", "High Q", "fx"),
        pad(COWBELL, "PERC", "Cowbell", "Cowbell", "perc"),
    ]
    return Kit("90s club", ["Drums", "Electronic", "House"],
               "A 90s groove box at the club: house kicks and snares, a fat clap, pop hats, congas, a "
               "reverse cymbal and a scratch.", pads)


def _90s_studio(ctx):
    pad = _dr(ctx, "90s groove box")
    pads = [
        pad(35, "BD", "Tight_Kick", "Kick tight", "kick", piece="Kick tight"),
        pad(KICK, "BD", "Real_Kick", "Kick", "kick", piece="Kick real"),
        pad(SIDESTICK, "SD", "Ambient_Side_Stick", "Side-stick", "rim", piece="Side-stick", seconds_=1),
        pad(SNARE, "SD", "Real_Snare_1", "Snare", "snare", piece="Snare real"),
        pad(CLAP, "SD", "Room_Gated_Snare", "Gated snare", "snare", piece="Snare gated"),
        pad(SNARE2, "SD", "Rock_Snare", "Rock snare", "snare", piece="Snare rock"),
        pad(LOWTOM_F, "TOMS", "Rock_Tom_4", "Floor tom", "tom", piece="Tom rock 4", pan=-0.35),
        pad(CLOSED, "HATS", "Acoustic_Closed_Hi-hat", "Closed hat", "hat", piece="Closed hat", choke=1, seconds_=1),
        pad(LOWTOM, "TOMS", "Rock_Tom_3", "Low tom", "tom", piece="Tom rock 3", pan=-0.15),
        pad(PEDAL, "HATS", "Acoustic_Pedal_Hi-hat", "Pedal hat", "hat", piece="Pedal hat", choke=1, seconds_=1),
        pad(MIDTOM_L, "TOMS", "Rock_Tom_2", "Mid tom", "tom", piece="Tom rock 2", pan=0.1),
        pad(OPEN, "HATS", "Acoustic_Open_Hi-hat", "Open hat", "open hat", piece="Open hat", choke=1),
        pad(MIDTOM, "TOMS", "Rock_Tom_1", "High tom", "tom", piece="Tom rock 1", pan=0.3),
        pad(CRASH, "HATS", "Crash_Cymbal_2", "Crash", "cymbal", piece="Crash 2", seconds_=4),
        pad(RIDE, "HATS", "Rock_Ride_Cymbal", "Ride", "ride", piece="Ride rock", seconds_=4),
        pad(BELL, "HATS", "Rock_Ride_Bell_Cymbal", "Ride bell", "ride", piece="Ride bell rock", seconds_=3),
        pad(SPLASH, "HATS", "Splash_Cymbal", "Splash", "cymbal", seconds_=3),
    ]
    return Kit("90s studio", ["Drums", "Electronic", "Pop"],
               "A 90s groove box's studio kit: a real kick, a big gated snare, rock toms and acoustic hats. "
               "Pop, R&B and rock demos.", pads)


def _synth_percussion(ctx):
    pad = _one(ctx, "Synth percussion", "fpsynth")
    pads = [
        pad(35, "samples/Kick06.flac", "Kick 2", "kick", piece="Kick 2"),
        pad(KICK, "samples/Kick04.flac", "Kick", "kick"),
        pad(SNARE, "samples/Snare09.flac", "Snare", "snare"),
        pad(CLAP, "samples/Clap01.flac", "Clap", "clap"),
        pad(SNARE2, "samples/Snare14.flac", "Snare 2", "snare", piece="Snare 2"),
        pad(LOWTOM_F, ["samples/LowTom02-01.flac", "samples/LowTom02-02.flac"], "Low tom", "tom", pan=-0.3),
        pad(CLOSED, "samples/ClosedHiHat01-01.flac", "Closed hat", "hat", choke=1, seconds_=1),
        pad(LOWTOM, ["samples/MidTom02-01.flac", "samples/MidTom02-02.flac"], "Mid tom", "tom"),
        pad(PEDAL, "samples/ClosedHiHat02-01.flac", "Closed hat 2", "hat", piece="Closed hat 2", choke=1, seconds_=1),
        pad(MIDTOM_L, ["samples/HighTom02-01.flac", "samples/HighTom02-02.flac"], "High tom", "tom", pan=0.3),
        pad(OPEN, "samples/OpenHiHat02-01.flac", "Open hat", "open hat", choke=1),
        pad(CRASH, ["samples/Cymbal01-01.flac", "samples/Cymbal01-02.flac", "samples/Cymbal01-03.flac"], "Cymbal",
            "cymbal", seconds_=4),
        pad(RIDE, "samples/Cymbal02.flac", "Cymbal 2", "cymbal", piece="Cymbal 2", seconds_=4),
        pad(MARACAS, "samples/Shaker04.flac", "Shaker", "shaker"),
        pad(CABASA, "samples/ShakerLong01.flac", "Long shaker", "shaker", piece="Shaker long"),
        pad(CLAVES, "samples/Claves01.flac", "Claves", "perc"),
    ]
    return Kit("Synth percussion", ["Drums", "Electronic", "Synth"],
               "Drums made on synthesizers, nothing recorded: sine kicks, noise snares and claps, metallic "
               "hats and pitched toms.", pads)


def _lmms(ctx, pool):
    one = _one(ctx, pool, "lmms")
    base = "Samples/Sample library/Drums"

    def pad(note, path, name, role, **options):
        return one(note, os.path.normpath(f"{base}/{path}.flac"), name, role, **options)

    return pad


def _drum_and_bass(ctx):
    pad = _lmms(ctx, "Drum and bass")
    pads = [
        pad(KICK, "Kicks/DnB Kick", "Kick", "kick"),
        pad(SIDESTICK, "Misc/Rim Shot 1", "Rimshot", "rim"),
        pad(SNARE, "Snares/DnB Snare 1", "Snare", "snare"),
        pad(CLAP, "Snares/DnB Snare 2", "Snare 2", "snare", piece="Snare 2"),
        pad(SNARE2, "Snares/DnB Snare 3", "Snare 3", "snare", piece="Snare 3"),
        pad(LOWTOM_F, "Toms/Tomsynth 1 Floor", "Floor tom", "tom", pan=-0.3),
        pad(CLOSED, "Hi-hats/CHH 3", "Closed hat", "hat", choke=1, seconds_=1),
        pad(LOWTOM, "Toms/Tomsynth 1 Low", "Low tom", "tom", pan=-0.15),
        pad(PEDAL, "Hi-hats/CHH 5", "Closed hat 2", "hat", piece="Closed hat 2", choke=1, seconds_=1),
        pad(MIDTOM_L, "Toms/Tomsynth 1 Mid", "Mid tom", "tom", pan=0.1),
        pad(OPEN, "Hi-hats/OHH 2", "Open hat", "open hat", choke=1),
        pad(MIDTOM, "Toms/Tomsynth 1 High", "High tom", "tom", pan=0.3),
        pad(CRASH, "Crashes/Crash 3", "Crash", "cymbal", seconds_=4),
        pad(RIDE, "Rides/Cymbal Ride 2", "Ride", "ride", seconds_=4),
        pad(MARACAS, "Shakers & Rattles/Shaker 1", "Shaker", "shaker"),
        pad(CHINA, "Snares/DnB Snare 4", "Snare 4", "snare", piece="Snare 4"),
    ]
    return Kit("Drum and bass", ["Drums", "Electronic", "Drum and bass"],
               "Snares that crack at 170 bpm, a tight kick, fast hats and a ride for rolling breaks.", pads)


def _hardstyle(ctx):
    pad = _lmms(ctx, "Hardstyle")
    pads = [
        pad(35, "Kicks/Hardstyle Kick 2 - G1", "Kick 2", "kick", piece="Kick 2", trim=-1),
        pad(KICK, "Kicks/Hardstyle Kick 1 - G1", "Kick", "kick", trim=-1),
        pad(SIDESTICK, "Kicks/Hardcore Kick 1 - G#1", "Hardcore kick", "kick", piece="Kick hardcore", trim=-1),
        pad(SNARE, "Snares/Punch Snare 1", "Snare", "snare"),
        pad(CLAP, "Claps/Noise Clap", "Clap", "clap"),
        pad(SNARE2, "Snares/Punch Snare 2", "Snare 2", "snare", piece="Snare 2"),
        pad(LOWTOM_F, "Kicks/Hardstyle Kick 3 - G1", "Kick 3", "kick", piece="Kick 3", trim=-1),
        pad(CLOSED, "Hi-hats/CHH 7", "Closed hat", "hat", choke=1, seconds_=1),
        pad(LOWTOM, "Kicks/Hardstyle Kick 5 - F1", "Kick F", "kick", piece="Kick F", trim=-1),
        pad(OPEN, "Hi-hats/OHH 5", "Open hat", "open hat", choke=1),
        pad(CRASH, "Crashes/Crash 5", "Crash", "cymbal", seconds_=4),
        pad(RIDE, "Rides/Cymbal Ride 4", "Ride", "ride", seconds_=4),
        pad(CHINA, "Claps/Layered Clap 2", "Layered clap", "clap", piece="Clap layered"),
        pad(SPLASH, "Snares/Punch Snare 3", "Snare 3", "snare", piece="Snare 3"),
    ]
    return Kit("Hardstyle", ["Drums", "Electronic", "Hardstyle"],
               "Distorted, tuned kicks in G and F, a hardcore kick, punchy snares and a noise clap. Hardstyle "
               "and hardcore.", pads)


def _hip_hop(ctx):
    pad = _lmms(ctx, "Hip-hop")
    pads = [
        pad(35, "Kicks/Bass Kick Long", "Long kick", "kick", piece="Kick long"),
        pad(KICK, "Kicks/Bass Kick Short", "Kick", "kick", piece="Kick short"),
        pad(SIDESTICK, "Misc/Rim Shot 2", "Rimshot", "rim"),
        pad(SNARE, "Snares/Hiphop Snare 1", "Snare", "snare"),
        pad(CLAP, "Claps/Trap Clap", "Clap", "clap"),
        pad(SNARE2, "Snares/Hiphop Snare 2", "Snare 2", "snare", piece="Snare 2"),
        pad(LOWTOM_F, "Snares/Hiphop Snare 3", "Snare 3", "snare", piece="Snare 3"),
        pad(CLOSED, "Hi-hats/CHH 5", "Closed hat", "hat", choke=1, seconds_=1),
        pad(LOWTOM, "Misc/Snap 1", "Snap", "rim"),
        pad(OPEN, "Hi-hats/OHH 4", "Open hat", "open hat", choke=1),
        pad(MIDTOM_L, "Snares/RnB Snare 1", "RnB snare", "snare", piece="Snare RnB"),
        pad(CRASH, "Crashes/Crash 2", "Crash", "cymbal", seconds_=4),
        pad(MARACAS, "Shakers & Rattles/Shaker 3", "Shaker", "shaker"),
        pad(CHINA, "../SFXs/Vinyl Backspin", "Backspin", "fx", piece="Backspin"),
    ]
    return Kit("Hip-hop", ["Drums", "Electronic", "Hip-hop"],
               "A long and a short kick, hip-hop and R&B snares, a trap clap, a snap and a vinyl backspin.",
               pads)


def _future(ctx):
    pad = _lmms(ctx, "Future")
    pads = [
        pad(35, "Kicks/Future Kick 2", "Kick 2", "kick", piece="Kick 2"),
        pad(KICK, "Kicks/Future Kick 1", "Kick", "kick"),
        pad(SNARE, "Snares/Future Snare 1", "Snare", "snare"),
        pad(CLAP, "Claps/Synthetic Clap 1", "Clap", "clap"),
        pad(SNARE2, "Snares/Future Snare 2", "Snare 2", "snare", piece="Snare 2"),
        pad(LOWTOM_F, "Toms/Tomsynth 2 Floor", "Floor tom", "tom", pan=-0.3),
        pad(CLOSED, "Hi-hats/CHH 9", "Closed hat", "hat", choke=1, seconds_=1),
        pad(LOWTOM, "Toms/Tomsynth 2 Low", "Low tom", "tom", pan=-0.15),
        pad(MIDTOM_L, "Toms/Tomsynth 2 Mid", "Mid tom", "tom", pan=0.1),
        pad(OPEN, "Hi-hats/OHH 7", "Open hat", "open hat", choke=1),
        pad(MIDTOM, "Toms/Tomsynth 2 High", "High tom", "tom", pan=0.3),
        pad(CRASH, "Crashes/Crash 6", "Crash", "cymbal", seconds_=4),
        pad(CHINA, "Claps/Synthetic Clap 2", "Clap 2", "clap", piece="Clap 2"),
        pad(SPLASH, "Kicks/Future Kick 3", "Kick 3", "kick", piece="Kick 3"),
    ]
    return Kit("Future bass", ["Drums", "Electronic", "Future bass"],
               "Bright, compressed kicks, wide synthetic claps and snares, and synth toms for future bass "
               "and pop drops.", pads)


def _sonic(ctx, pool):
    one = _one(ctx, pool, "sonicpi")

    def pad(note, name, ours, role, **options):
        return one(note, f"etc/samples/{name}.flac", ours, role, **options)

    return pad


def _electro_lab(ctx):
    pad = _sonic(ctx, "Electro lab")
    pads = [
        pad(35, "elec_hollow_kick", "Hollow kick", "kick", piece="Kick hollow"),
        pad(KICK, "elec_soft_kick", "Kick", "kick", piece="Kick soft"),
        pad(SIDESTICK, "elec_tick", "Tick", "rim"),
        pad(SNARE, "elec_snare", "Snare", "snare"),
        pad(CLAP, "elec_filt_snare", "Filter snare", "snare", piece="Snare filtered"),
        pad(SNARE2, "elec_hi_snare", "High snare", "snare", piece="Snare high"),
        pad(LOWTOM_F, "elec_fuzz_tom", "Fuzz tom", "tom"),
        pad(CLOSED, "elec_cymbal", "Cymbal", "hat", piece="Cymbal", seconds_=1.5),
        pad(LOWTOM, "elec_mid_snare", "Mid snare", "snare", piece="Snare mid"),
        pad(OPEN, "elec_triangle", "Triangle", "perc"),
        pad(MIDTOM_L, "elec_lo_snare", "Low snare", "snare", piece="Snare low"),
        pad(MIDTOM, "elec_wood", "Wood", "perc"),
        pad(HIGHTOM_L, "elec_pop", "Pop", "perc"),
        pad(CRASH, "elec_bong", "Bong", "perc"),
        pad(HIGHTOM, "elec_blip", "Blip", "fx"),
        pad(RIDE, "elec_ping", "Ping", "fx"),
        pad(CHINA, "elec_plip", "Plip", "fx"),
        pad(BELL, "elec_chime", "Chime", "fx", seconds_=3),
    ]
    return Kit("Electro lab", ["Drums", "Electronic", "Electro"],
               "Electronic drums and blips from a lab bench: a hollow kick, filtered snares, a fuzz tom, a "
               "wood block, pops, pings and chimes.", pads)


def _minimal(ctx):
    pad = _sonic(ctx, "Minimal")
    pads = [
        pad(35, "bd_tek", "Kick 2", "kick", piece="Kick tek"),
        pad(KICK, "bd_haus", "Kick", "kick", piece="Kick haus"),
        pad(SIDESTICK, "perc_snap", "Snap", "rim"),
        pad(SNARE, "sn_dolf", "Snare", "snare"),
        pad(CLAP, "sn_zome", "Snare 2", "snare", piece="Snare zome"),
        pad(SNARE2, "sn_generic", "Snare 3", "snare", piece="Snare generic"),
        pad(LOWTOM_F, "bd_klub", "Kick 3", "kick", piece="Kick klub", trim=-2),
        pad(CLOSED, "hat_tap", "Closed hat", "hat", choke=1, piece="Hat tap"),
        pad(LOWTOM, "bd_zome", "Kick 4", "kick", piece="Kick zome", trim=-2),
        pad(PEDAL, "hat_snap", "Snap hat", "hat", choke=1, piece="Hat snap"),
        pad(OPEN, "hat_psych", "Open hat", "open hat", choke=1, piece="Hat psych"),
        pad(MIDTOM_L, "hat_metal", "Metal hat", "hat", choke=1, piece="Hat metal"),
        pad(CRASH, "hat_zild", "Long hat", "open hat", choke=1, piece="Hat zild"),
        pad(RIDE, "ride_tri", "Ride", "ride", piece="Ride tri", seconds_=3),
        pad(CHINA, "perc_swash", "Swash", "fx"),
        pad(BELL, "perc_bell", "Bell", "perc", seconds_=3),
    ]
    return Kit("Minimal", ["Drums", "Electronic", "Techno"],
               "Small, dry club sounds: two kicks, clicky snares and snaps, a family of thin hats and a bell. "
               "Minimal techno and microhouse.", pads)
