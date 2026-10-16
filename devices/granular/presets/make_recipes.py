#!/usr/bin/env python3
# Granular's factory presets (ADR 0069, devices/granular/presets/BRIEF.md): each preset written as
# its designed sound, with its macros, modulation and playing, into recipes.json (the set's source),
# and the brief's catalogue. Run from the repository's root:
#
#   python3 devices/granular/presets/make_recipes.py build/rel/tools/presets/fd-presets
#
# then `fd-presets level devices/granular/presets/recipes.json out.volume` (the levels) and
# `fd-presets make devices/granular/presets/recipes.json` (the files, checked across the keyboard).
# A macro that reads both ways (Place, Tone, Grain) sits at 50 with the preset's sound there: the
# base under it is worked out here, in each parameter's own curve.

import json
import math
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
L = "library:Samples/"

# ---- the parameters, as fd-presets describes them -------------------------------------------------

def read_specs(tool):
    out = subprocess.run([tool, "describe", "granular"], capture_output=True, text=True, check=True).stdout
    specs = {}
    for line in out.splitlines():
        m = re.match(r"  (\S+) \[(\w+)\] (-?[\d.]+) \.\. (-?[\d.]+), default (.*)$", line)
        if not m:
            continue
        pid, kind, lo, hi, rest = m.groups()
        centre = re.search(r", centre (-?[\d.]+)", rest)
        specs[pid] = {"kind": kind, "min": float(lo), "max": float(hi),
                      "centre": float(centre.group(1)) if centre else None}
    return specs


SPECS = read_specs(sys.argv[1]) if len(sys.argv) > 1 else {}


def skew(pid):
    s = SPECS[pid]
    c = s["centre"]
    if s["kind"] != "continuous" or c is None or not (s["min"] < c < s["max"]):
        return 1.0
    return math.log(0.5) / math.log((c - s["min"]) / (s["max"] - s["min"]))


def to_norm(pid, v):
    s = SPECS[pid]
    p = (min(max(v, s["min"]), s["max"]) - s["min"]) / (s["max"] - s["min"])
    return p ** skew(pid)


def from_norm(pid, n):
    s = SPECS[pid]
    n = min(max(n, 0.0), 1.0)
    p = n ** (1.0 / skew(pid)) if n > 0 else 0.0
    return s["min"] + (s["max"] - s["min"]) * p


# What each matrix target moves, and how (a parameter in its own curve; the voice filters' cutoffs
# in octaves; pitch in semitones; pan and amp as they are).
TARGET_PARAM = {
    "Position": "pos", "Position Spray": "pos.spray", "Scan": "scan.rate", "Size": "grain.size",
    "Size Spray": "grain.sizespray", "Overlap": "grain.overlap", "Density": "grain.density",
    "Periods": "grain.periods", "Formant": "grain.formant", "Crossfade": "grain.xfade",
    "Jitter": "grain.jitter", "Chance": "grain.chance", "Swing": "grain.swing", "Fire": "grain.fire",
    "Skew": "grain.skew", "Flatness": "grain.flat", "Reverse": "grain.reverse",
    "Level Spray": "grain.levelspray", "Even": "grain.even", "Pitch Spray": "pitch.spray",
    "Grain Glide": "pitch.glide", "Pan Spray": "pan.spray", "Width": "out.width", "Source Mix": "src.mix",
    "Source 1 Start": "src1.start", "Source 1 End": "src1.end", "Source 1 Gain": "src1.gain",
    "Source 1 Tune": "src1.tune", "Source 2 Start": "src2.start", "Source 2 End": "src2.end",
    "Source 2 Gain": "src2.gain", "Source 2 Tune": "src2.tune", "Grain Cutoff": "gfilter.cutoff",
    "Grain Resonance": "gfilter.res", "Cutoff Spray": "gfilter.spray", "Crush": "gcolor.crush",
    "Ring": "gcolor.ring", "Ring Ratio": "gcolor.ratio", "Filter 1 Resonance": "filter1.res",
    "Filter 1 Drive": "filter1.drive", "Filter 1 Shape": "filter1.shape", "Filter 2 Resonance": "filter2.res",
    "Filter 2 Drive": "filter2.drive", "Filter 2 Shape": "filter2.shape", "Filter Routing": "filters.routing",
    "Volume": "out.volume",
}
OCTAVE_TARGETS = {"Filter 1 Cutoff": "filter1.cutoff", "Filter 2 Cutoff": "filter2.cutoff"}

# Defaults the presets lean on (the module's own, from its SPEC).
DEFAULTS = {"pos": 0.0, "pos.spray": 0.0, "scan.rate": 0.0, "grain.size": 100.0, "grain.sizespray": 0.0,
            "grain.overlap": 2.0, "grain.density": 40.0, "grain.periods": 4.0, "grain.formant": 0.0,
            "grain.xfade": 10.0, "grain.jitter": 0.0, "grain.chance": 100.0, "grain.swing": 0.0,
            "grain.fire": 0.0, "grain.skew": 0.0, "grain.flat": 0.0, "grain.reverse": 0.0,
            "grain.levelspray": 0.0, "grain.even": 0.0, "pitch.spray": 0.0, "pitch.glide": 0.0,
            "pan.spray": 30.0, "out.width": 100.0, "src.mix": 50.0, "src1.start": 0.0, "src1.end": 100.0,
            "src2.start": 0.0, "src2.end": 100.0, "src1.gain": 0.0, "src2.gain": 0.0, "src1.tune": 0.0,
            "src2.tune": 0.0, "gfilter.cutoff": 2000.0, "gfilter.res": 20.0, "gfilter.spray": 0.0,
            "gcolor.crush": 0.0, "gcolor.ring": 0.0, "gcolor.ratio": 2.0, "filter1.res": 20.0,
            "filter1.drive": 0.0, "filter1.shape": 50.0, "filter2.res": 20.0, "filter2.drive": 0.0,
            "filter2.shape": 50.0, "filters.routing": 0.0, "out.volume": -6.0, "filter1.cutoff": 20000.0,
            "filter2.cutoff": 100.0}


# ---- a preset ----------------------------------------------------------------------------------

PRESETS = []


class Preset:
    def __init__(self, name, tags, description, category, idea):
        self.name, self.tags, self.description, self.category, self.idea = name, tags, description, category, idea
        self.params, self.extra, self.routes, self.macros, self.keys = {}, {}, [], {}, None
        PRESETS.append(self)

    # The sources.
    def source(self, n, path, **params):
        self.extra[f"src{n}.path"] = L + path
        if n == 2:
            self.params["src2.on"] = True
        for k, v in params.items():
            self.params[f"src{n}.{k}"] = v
        return self

    def p(self, values):
        self.params.update(values)
        return self

    def route(self, src, dst, amt, curve=0, aux=None):
        self.routes.append((src, dst, amt, curve, aux))
        return self

    def macro(self, n, name, routes, centred=False, value=None):
        """Macro n, named, moving `routes` [(target, amount, curve)]. Centred: it sits at 50 with the
        preset's designed values there, reading both ways; otherwise it adds from 0 (or `value`)."""
        self.macros[n] = (name, routes, centred, value)
        return self

    def play(self, keys):
        self.keys = keys
        return self

    def designed(self, pid):
        return self.params.get(pid, DEFAULTS.get(pid, 0.0))

    def recipe(self):
        params = dict(self.params)
        routes = list(self.routes)
        extra = dict(self.extra)
        for n in sorted(self.macros):
            name, mroutes, centred, value = self.macros[n]
            extra[f"macro{n}.name"] = name
            at = 50.0 if centred else (value if value is not None else 0.0)
            if at:
                params[f"macro{n}"] = at
            for target, amt, curve in mroutes:
                routes.append((f"Macro {n}", target, amt, curve, None))
                if not centred and not at:
                    continue
                # The designed value sits where the macro sits: its base below by what the macro
                # adds there.
                share = at / 100.0
                if target in OCTAVE_TARGETS:
                    pid = OCTAVE_TARGETS[target]
                    params[pid] = round(self.designed(pid) / 2 ** (5.0 * amt / 100.0 * share), 1)
                elif target in TARGET_PARAM:
                    pid = TARGET_PARAM[target]
                    if curve:
                        raise ValueError(f"{self.name}: a centred macro on {target} with a curve")
                    base = from_norm(pid, to_norm(pid, self.designed(pid)) - amt / 100.0 * share)
                    params[pid] = round(base, 3 if abs(base) < 10 else 1)
                else:
                    raise ValueError(f"{self.name}: macro {n} can't be centred on {target}")
        if len(routes) > 32:
            raise ValueError(f"{self.name}: {len(routes)} routes")
        for i, (src, dst, amt, curve, aux) in enumerate(routes, start=1):
            params[f"mod{i}.src"] = src
            params[f"mod{i}.dst"] = dst
            params[f"mod{i}.amt"] = amt
            if curve:
                params[f"mod{i}.curve"] = curve
            if aux:
                params[f"mod{i}.aux"] = aux
        r = {"name": self.name, "tags": self.tags, "description": self.description}
        if self.keys:
            r["keys"] = self.keys
        r["params"] = params
        r["extra"] = extra
        return r


# The macro roles, in the same place in every preset (BRIEF.md, "Playing it"): 1 where (Place),
# 2 how scattered (Scatter), 3 the grain (Grain), 4 the tone (Tone), 5 pitch (Shimmer and the like),
# 6 movement (Motion), 7 time (Freeze, Drift, Density), 8 character.

def pad_motion(p, lfo_rate=0.06, depth=4, macro_value=40, second=0.09, second_target="Size Spray",
               second_depth=15):
    """Slow drift: LFO 1 on Position, scaled by Motion (macro 6), and LFO 2 on another grain
    target, at unrelated rates under 0.1 Hz."""
    p.p({"lfo1.rate": lfo_rate, "lfo1.mode": "Free", "lfo1.poly": False,
         "lfo2.rate": second, "lfo2.mode": "Free", "lfo2.poly": False, "lfo2.phase": 90})
    p.route("LFO 1", "Position", depth, aux="Macro 6")
    p.route("LFO 2", second_target, second_depth, aux="Macro 6")
    p.macros[6] = ("Motion", [], False, macro_value)
    return p


def detune(p, cents=7):
    """Each grain a few cents off (Grain Random on Pitch: 24 semitones a whole route)."""
    return p.route("Grain Random", "Pitch", round(cents / 2400.0 * 100.0, 2))


def pad_filters(p, lp=8000, hp=110, key=30):
    """A low pass above and a high pass below: no fizz, no mud. The high pass follows the note (its
    cutoff set for C3), so low notes keep their body and high ones lose their mud."""
    return p.p({"filter1.type": "LP 12", "filter1.cutoff": lp, "filter1.res": 10, "filter1.key": key,
                "filter2.on": True, "filter2.type": "HP 12", "filter2.cutoff": hp, "filter2.res": 10,
                "filter2.key": 100})


def pad_env(p, a=900, r=2800, s=100):
    return p.p({"env1.a": a, "env1.d": 2000, "env1.s": s, "env1.r": r, "env1.acurve": -20})


def common_macros(p, place=25, scatter=40, grain=None, tone=40, shimmer=None, drift=None, character=None):
    p.macro(1, "Place", [("Position", place, 0)], centred=True)
    p.macro(2, "Scatter", [("Position Spray", scatter, 0), ("Pan Spray", 30, 0), ("Size Spray", 25, 0)])
    if grain:
        p.macro(3, "Grain", [("Size", grain, 0)], centred=True)
    p.macro(4, "Tone", [("Filter 1 Cutoff", tone, 0)], centred=True)
    if shimmer:
        p.macro(5, *shimmer)
    if drift:
        p.macro(7, *drift)
    if character:
        p.macro(8, *character)
    return p


# ============================================================================================
# Pads
# ============================================================================================

p = Preset("Endless bow", ["Pad", "Strings", "Wide"],
           "A violin section's bow held still forever, gently drifting: Place moves along the note, Shimmer lifts grains an octave.",
           "Pads", "A frozen bow: long, overlapping grains just past the attack, sprayed beyond the comb range, a few cents apart, so it never goes hollow.")
p.source(1, "Strings/Violin section/Sustain.sfz")
p.p({"pos": 45, "pos.spray": 8, "pos.dist": "Near", "grain.size": 450, "grain.overlap": 8, "grain.jitter": 15,
     "grain.sizespray": 30, "pan.spray": 65, "out.width": 130, "pitch.quant": "Octaves", "pitch.dir": "Up",
     "amp.vel": 40})
pad_filters(p, 7500, 120)
pad_env(p, 1200, 3000)
detune(p, 7)
pad_motion(p)
common_macros(p, grain=30, shimmer=("Shimmer", [("Pitch Spray", 20, 0)]),
              drift=("Drift", [("Scan", 3, 0)]), character=("Reverse", [("Reverse", 60, 0)]))
p.route("Mod Wheel", "Position Spray", 30).route("Velocity", "Filter 1 Cutoff", 6)

p = Preset("Tremolo glitter", ["Pad", "Strings", "Bright"],
           "Tremolo violins scattered into a glittering cloud, a share of grains an octave up: Density thickens it, the mod wheel too.",
           "Pads", "Tremolo is granular already: a Cloud of short Gaussian grains with the octave spray quantised, so some grains sparkle above the bowing.")
p.source(1, "Strings/Violin section/Tremolo.sfz")
p.p({"grain.mode": "Cloud", "grain.density": 100, "grain.size": 80, "grain.window": "Gaussian", "pos": 40,
     "pos.spray": 25, "grain.sizespray": 30, "pitch.spray": 8, "pitch.dir": "Up", "pitch.quant": "Octaves",
     "pan.spray": 90, "out.width": 140, "amp.vel": 40})
pad_filters(p, 11000, 300, 20)
pad_env(p, 700, 2500)
detune(p, 5)
p.p({"rnd1.mode": "Smooth", "rnd1.rate": 0.3, "rnd1.poly": False}).route("Random 1", "Position", 6, aux="Macro 6")
p.macros[6] = ("Motion", [], False, 50)
common_macros(p, grain=25, shimmer=("Shimmer", [("Pitch Spray", 15, 0)]),
              drift=("Density", [("Density", 18, 0)]), character=("Air", [("Filter 2 Cutoff", 25, 0)]))
p.route("Mod Wheel", "Density", 15).route("Pressure", "Pitch Spray", 10)

p = Preset("Piano afterglow", ["Pad", "Keys", "Warm"],
           "A grand piano's ring frozen just after the hammer, warm and still, a quarter of its grains backwards.",
           "Pads", "Skip the hammer and hold the body: long Gaussian grains past the strike, reversed now and then, low-passed, the decaying ring raised by Even.")
p.source(1, "Keys/Grand piano/Grand piano.sfz", end=25)
p.p({"pos": 55, "pos.spray": 12, "pos.dist": "Near", "grain.size": 320, "grain.overlap": 9, "grain.window": "Gaussian",
     "grain.jitter": 10, "grain.sizespray": 25, "grain.reverse": 25, "grain.even": 70, "pan.spray": 60,
     "out.width": 120, "pitch.quant": "Octaves", "pitch.dir": "Up", "amp.vel": 50})
pad_filters(p, 4200, 90, 40)
pad_env(p, 800, 3200)
detune(p, 5)
pad_motion(p, 0.05, 3, 35)
common_macros(p, place=20, grain=30, shimmer=("Shimmer", [("Pitch Spray", 20, 0)]),
              drift=("Drift", [("Scan", 3, 0)]), character=("Reverse", [("Reverse", 50, 0)]))
p.route("Mod Wheel", "Reverse", 40).route("Velocity", "Filter 1 Cutoff", 8)

p = Preset("Horn cathedral", ["Pad", "Brass", "Warm"],
           "French horns as a slow cloud of swelling grains, each rising late and falling away, under a long attack.",
           "Pads", "Late-peaking windows (positive Skew) make every grain a small swell, so the cloud breathes; the long attack hides its build-up.")
p.source(1, "Brass/French horn/Sustain.sfz")
p.p({"grain.mode": "Cloud", "grain.density": 40, "grain.size": 500, "grain.skew": 45, "pos": 40, "pos.spray": 20,
     "grain.sizespray": 30, "pan.spray": 70, "out.width": 125, "amp.vel": 40})
pad_filters(p, 5000, 90)
pad_env(p, 1600, 3500)
detune(p, 6)
pad_motion(p, 0.04, 4, 40, 0.07, "Skew", 20)
common_macros(p, grain=25, shimmer=("Fifths", [("Pitch Spray", 25, 0)]), drift=("Drift", [("Scan", 3, 0)]),
              character=("Swell", [("Skew", 40, 0)]))
p.p({"pitch.quant": "Fifths", "pitch.dir": "Up"})
p.route("Mod Wheel", "Density", 15).route("Velocity", "Filter 1 Cutoff", 8)

p = Preset("Organ halo", ["Pad", "Keys", "Bright"],
           "A soft organ with a halo of octaves above it, the upper grains darker than the note.",
           "Pads", "Octave grains through a fixed grain low pass: the octave adds air without glare, as a shimmer reverb does.")
p.source(1, "Keys/Pipe organ/Soft.sfz")
p.p({"pos": 35, "pos.spray": 10, "grain.size": 260, "grain.overlap": 7, "grain.jitter": 15, "grain.sizespray": 30,
     "pitch.spray": 10, "pitch.dir": "Up", "pitch.quant": "Octaves", "gfilter.on": True, "gfilter.type": "LP",
     "gfilter.cutoff": 2600, "gfilter.res": 5, "pan.spray": 80, "out.width": 130, "amp.vel": 30})
pad_filters(p, 9000, 100)
pad_env(p, 700, 2500)
detune(p, 4)
pad_motion(p)
common_macros(p, grain=30, shimmer=("Halo", [("Pitch Spray", 25, 0)]), drift=("Drift", [("Scan", 3, 0)]),
              character=("Glow", [("Grain Cutoff", 30, 0)]))
p.route("Mod Wheel", "Pitch Spray", 20)

p = Preset("Reed ensemble", ["Pad", "Woodwinds", "Warm"],
           "An oboe and a clarinet blended grain by grain into one reed section; Blend leans it to either.",
           "Pads", "Two sources picked at random by the mix: each grain is one reed or the other, a section from two players.")
p.source(1, "Woodwinds/Oboe/Sustain.sfz").source(2, "Woodwinds/Clarinet/Sustain.sfz")
p.p({"grain.mode": "Cloud", "grain.density": 45, "grain.size": 340, "pos": 40, "pos.spray": 18, "grain.sizespray": 30,
     "pan.spray": 80, "out.width": 120, "amp.vel": 40})
pad_filters(p, 7000, 120)
pad_env(p, 600, 2200)
detune(p, 9)
pad_motion(p, 0.07, 4, 35, 0.05, "Source Mix", 15)
common_macros(p, grain=25, drift=("Drift", [("Scan", 3, 0)]))
p.macro(5, "Blend", [("Source Mix", 60, 0)], centred=True)
p.route("Mod Wheel", "Position Spray", 25).route("Velocity", "Filter 1 Cutoff", 6)

p = Preset("Bowed vibes", ["Pad", "Bright", "Clean"],
           "A vibraphone's ring frozen into a bowed, endless tone, steady as glass.",
           "Pads", "Hold the ring, not the strike: long Hann grains past the attack, eight deep, Even keeping the fading ring level.")
p.source(1, "Tuned percussion/Vibraphone/Vibraphone.sfz")
p.p({"pos": 18, "pos.spray": 6, "pos.dist": "Near", "grain.size": 260, "grain.overlap": 8, "grain.jitter": 15,
     "grain.sizespray": 30, "grain.even": 85, "pan.spray": 55, "out.width": 120, "pitch.quant": "Octaves",
     "pitch.dir": "Up", "amp.vel": 40})
pad_filters(p, 8000, 150, 40)
pad_env(p, 900, 3000)
detune(p, 4)
pad_motion(p, 0.06, 3, 35)
common_macros(p, place=20, grain=30, shimmer=("Shimmer", [("Pitch Spray", 20, 0)]),
              drift=("Drift", [("Scan", 3, 0)]), character=("Reverse", [("Reverse", 50, 0)]))
p.route("Mod Wheel", "Position Spray", 25)

p = Preset("Harp halo", ["Pad", "Pluck", "Wide"],
           "A harp's ring held and spread into a suspended second chord around each note.",
           "Pads", "The pluck's ring frozen, its grains landing on a sus 2 chord: harmony from one note, neither major nor minor.")
p.source(1, "Strings/Harp/Harp.sfz")
p.p({"pos": 10, "pos.spray": 6, "pos.dist": "Near", "grain.size": 300, "grain.overlap": 8, "grain.jitter": 15,
     "grain.sizespray": 30, "grain.even": 70, "pitch.spray": 14, "pitch.dir": "Up", "pitch.quant": "Chord",
     "pitch.chord": "Sus 2", "pan.spray": 90, "out.width": 140, "amp.vel": 40})
pad_filters(p, 8000, 130)
pad_env(p, 800, 3000)
detune(p, 5)
pad_motion(p)
common_macros(p, place=15, grain=30, shimmer=("Chord", [("Pitch Spray", 20, 0)]), drift=("Drift", [("Scan", 3, 0)]),
              character=("Reverse", [("Reverse", 50, 0)]))
p.route("Mod Wheel", "Pitch Spray", 20)

p = Preset("Low fog", ["Pad", "Strings", "Heavy"],
           "Cellos an octave down in a slow, dark cloud that rolls in under everything.",
           "Pads", "A low cloud kept from mud by a high pass and a closed low pass, Even holding it steady.")
p.source(1, "Strings/Cello section/Sustain.sfz")
p.p({"grain.mode": "Cloud", "grain.density": 28, "grain.size": 650, "pos": 40, "pos.spray": 30, "grain.sizespray": 30,
     "grain.even": 50, "pitch.transpose": -12, "pan.spray": 55, "out.width": 120, "amp.vel": 30})
pad_filters(p, 1500, 55, 50)
pad_env(p, 2000, 4000)
detune(p, 6)
pad_motion(p, 0.04, 5, 40)
common_macros(p, grain=25, tone=50, drift=("Drift", [("Scan", 3, 0)]), character=("Reverse", [("Reverse", 50, 0)]))
p.macro(5, "Octave", [("Pitch Spray", 25, 0)])
p.p({"pitch.quant": "Octaves", "pitch.dir": "Up"})
p.play([24, 72]).route("Mod Wheel", "Filter 1 Cutoff", 15)

p = Preset("Flute air", ["Pad", "Woodwinds", "Wide"],
           "Flute breath and tone scattered wide into a soft, airy wash; Air brings up the breath.",
           "Pads", "Short grains from all over the note, the breath included, high-passed and spread across the field.")
p.source(1, "Woodwinds/Flute/Sustain.sfz")
p.p({"grain.mode": "Cloud", "grain.density": 70, "grain.size": 130, "pos": 30, "pos.spray": 55, "pos.dist": "Even",
     "grain.sizespray": 40, "pitch.spray": 0.3, "pan.spray": 100, "out.width": 150, "amp.vel": 30})
pad_filters(p, 12000, 500, 0)
pad_env(p, 900, 3000)
pad_motion(p, 0.08, 5, 40)
common_macros(p, grain=25, drift=("Density", [("Density", 18, 0)]), character=("Air", [("Filter 2 Cutoff", 30, 0)]))
p.macro(5, "Shimmer", [("Pitch Spray", 20, 0)])
p.p({"pitch.quant": "Off"})
p.route("Mod Wheel", "Filter 2 Cutoff", 20)

p = Preset("Glass choir", ["Pad", "Bright", "Wide"],
           "Wine glasses blooming into a choir, some grains an octave below for body.",
           "Pads", "A pure tone needs company: grains an octave down give it a floor, a few cents of spread give it a choir.")
p.source(1, "Tuned percussion/Wine glasses/Wine glasses.sfz")
p.p({"grain.mode": "Cloud", "grain.density": 40, "grain.size": 420, "pos": 40, "pos.spray": 20, "grain.sizespray": 30,
     "pitch.spray": 9, "pitch.dir": "Down", "pitch.quant": "Octaves", "pan.spray": 80, "out.width": 140,
     "amp.vel": 30})
pad_filters(p, 9000, 120)
pad_env(p, 1000, 3200)
detune(p, 10)
pad_motion(p)
common_macros(p, grain=25, shimmer=("Floor", [("Pitch Spray", 20, 0)]), drift=("Drift", [("Scan", 3, 0)]),
              character=("Reverse", [("Reverse", 50, 0)]))
p.route("Mod Wheel", "Position Spray", 25)

p = Preset("Viola mist", ["Pad", "Strings", "Subtle"],
           "Violas stretched to a tenth of their speed into a slowly turning mist.",
           "Pads", "Time-stretching: the playhead walks through the note at 10 % while the grains keep the pitch.")
p.source(1, "Strings/Viola section/Sustain.sfz")
p.p({"pos": 15, "scan.rate": 10, "scan.end": "Bounce", "grain.size": 380, "grain.overlap": 8, "grain.jitter": 20,
     "pos.spray": 6, "grain.sizespray": 30, "pan.spray": 60, "out.width": 125, "amp.vel": 40})
pad_filters(p, 7000, 120)
pad_env(p, 1100, 3000)
detune(p, 6)
common_macros(p, grain=30, drift=("Speed", [("Scan", 6, 0)]), character=("Reverse", [("Reverse", 50, 0)]))
p.macro(5, "Shimmer", [("Pitch Spray", 20, 0)])
p.p({"pitch.quant": "Octaves", "pitch.dir": "Up"})
p.p({"lfo1.rate": 0.07, "lfo1.mode": "Free", "lfo1.poly": False}).route("LFO 1", "Size Spray", 15, aux="Macro 6")
p.macros[6] = ("Motion", [], False, 40)
p.route("Mod Wheel", "Scan", -1.25)

# ============================================================================================
# Atmospheres and textures
# ============================================================================================

p = Preset("Rosin dust", ["FX", "Strings", "Subtle"],
           "Crackles of bowed attacks scattered in the dark, every one a different bow.",
           "Atmospheres", "Sparse, tiny grains of spiccato attacks with a sharp window, sprayed across the whole note and the field.")
p.source(1, "Strings/Violin section/Spiccato.sfz")
p.p({"grain.mode": "Cloud", "grain.density": 16, "grain.size": 35, "grain.window": "Sharp Attack", "pos": 0,
     "pos.spray": 100, "pos.dist": "Ahead", "grain.levelspray": 60, "pitch.spray": 5, "pan.spray": 100,
     "out.width": 150, "amp.vel": 30})
pad_filters(p, 9000, 400, 0)
pad_env(p, 400, 2500)
common_macros(p, place=20, drift=("Density", [("Density", 25, 0)]), character=("Crackle", [("Crush", 40, 0)]))
p.macro(3, "Grain", [("Size", 30, 0)], centred=True)
p.macro(5, "Pitch", [("Pitch Spray", 25, 0)])
p.route("Velocity", "Density", 12).route("Mod Wheel", "Density", 20)

p = Preset("Reed wind", ["FX", "Woodwinds", "Wide"],
           "A harmonica's breath turned to wind blowing through a narrow band that wanders.",
           "Atmospheres", "Short grains through a band pass whose cutoff each grain scatters over two octaves, the band drifting slowly.")
p.source(1, "Woodwinds/Harmonica/Harmonica.sfz")
p.p({"grain.mode": "Cloud", "grain.density": 120, "grain.size": 45, "pos": 30, "pos.spray": 60, "grain.sizespray": 40,
     "gfilter.on": True, "gfilter.type": "BP", "gfilter.cutoff": 1300, "gfilter.spray": 2, "gfilter.res": 25,
     "pan.spray": 100, "out.width": 150, "amp.vel": 20})
pad_filters(p, 12000, 200, 0)
pad_env(p, 1200, 3000)
p.p({"lfo1.rate": 0.09, "lfo1.mode": "Free", "lfo1.poly": False}).route("LFO 1", "Grain Cutoff", 18, aux="Macro 6")
p.macros[6] = ("Gusts", [], False, 50)
common_macros(p, drift=("Density", [("Density", 18, 0)]), character=("Whistle", [("Grain Resonance", 50, 0)]))
p.macro(3, "Band", [("Grain Cutoff", 40, 0)], centred=True)
p.route("Mod Wheel", "Cutoff Spray", 25)

p = Preset("Starfield", ["FX", "Bright", "Wide"],
           "A glockenspiel scattered into stars on a ninth chord, some of them falling backwards.",
           "Atmospheres", "Sparse long grains of the struck bar, pitched over two octaves onto a major ninth, a third reversed.")
p.source(1, "Tuned percussion/Glockenspiel/Glockenspiel.sfz")
p.p({"grain.mode": "Cloud", "grain.density": 8, "grain.size": 420, "pos": 2, "pos.spray": 12, "pos.dist": "Ahead",
     "pitch.spray": 24, "pitch.dir": "Up", "pitch.quant": "Chord", "pitch.chord": "Major 9", "grain.reverse": 30,
     "pan.spray": 100, "out.width": 150, "amp.vel": 30})
pad_filters(p, 11000, 250, 0)
pad_env(p, 300, 3500)
common_macros(p, place=10, drift=("Density", [("Density", 25, 0)]), character=("Reverse", [("Reverse", 50, 0)]))
p.macro(5, "Range", [("Pitch Spray", 20, 0)], centred=True)
p.route("Mod Wheel", "Density", 20)
p.play([48, 96])

p = Preset("Muted embers", ["FX", "Brass", "Warm"],
           "Muted trumpet glowing in fragments, sparse and ringing, smouldering low.",
           "Atmospheres", "Chance leaves gaps in a slow cloud; a little ring modulation gives the fragments a glow.")
p.source(1, "Brass/Trumpet/Muted.sfz")
p.p({"grain.mode": "Cloud", "grain.density": 30, "grain.size": 180, "grain.chance": 45, "pos": 35, "pos.spray": 30,
     "gcolor.ring": 18, "gcolor.ratio": 1.5, "gcolor.ringspray": 15, "grain.levelspray": 40, "pan.spray": 75,
     "out.width": 120, "amp.vel": 30})
pad_filters(p, 3500, 150, 30)
pad_env(p, 900, 3000)
pad_motion(p, 0.06, 5, 40)
common_macros(p, grain=25, drift=("Embers", [("Chance", 40, 0)]), character=("Glow", [("Ring", 40, 0)]))
p.macro(5, "Shimmer", [("Pitch Spray", 20, 0)])
p.p({"pitch.quant": "Octaves", "pitch.dir": "Up"})
p.route("Mod Wheel", "Chance", 35)

p = Preset("Timpani storm", ["FX", "Drums", "Heavy"],
           "A timpani roll an octave down rumbling like distant thunder, heavy and dark.",
           "Atmospheres", "Grains from all over a roll, transposed down, low-passed: a noise bed with a pitch under it.")
p.source(1, "Percussion/Timpani/Rolls.sfz")
p.p({"grain.mode": "Cloud", "grain.density": 70, "grain.size": 140, "pos": 30, "pos.spray": 60, "grain.sizespray": 40,
     "pitch.transpose": -12, "grain.levelspray": 30, "pan.spray": 70, "out.width": 120, "amp.vel": 50})
pad_filters(p, 900, 40, 30)
pad_env(p, 1500, 3500)
p.p({"filter1.res": 25})
p.p({"rnd1.mode": "Smooth", "rnd1.rate": 0.4, "rnd1.poly": False}).route("Random 1", "Filter 1 Cutoff", 8, aux="Macro 6")
p.macros[6] = ("Rumble", [], False, 50)
common_macros(p, grain=25, tone=40, drift=("Density", [("Density", 18, 0)]), character=("Crack", [("Crush", 30, 0)]))
p.play([24, 60]).route("Velocity", "Filter 1 Cutoff", 10).route("Mod Wheel", "Filter 1 Cutoff", 20)

p = Preset("Kalimba drops", ["Pluck", "FX", "Bright"],
           "Kalimba notes dropping slowly around you, each a note of the song's key.",
           "Atmospheres", "Very sparse long grains with a sharp attack: each grain a whole pluck, its pitch landing in the key.")
p.source(1, "Tuned percussion/Kalimba/Kalimba.sfz")
p.p({"grain.mode": "Cloud", "grain.size": 650, "grain.window": "Sharp Attack", "pos": 0,
     "pos.spray": 4, "pos.dist": "Ahead", "pitch.spray": 12, "pitch.quant": "Song Key", "grain.even": 60,
     "grain.density": 8, "pan.spray": 100, "out.width": 140, "amp.vel": 30})
pad_filters(p, 10000, 150, 0)
pad_env(p, 100, 3500)
common_macros(p, place=6, drift=("Rain", [("Density", 30, 0)]), character=("Reverse", [("Reverse", 50, 0)]))
p.macro(5, "Range", [("Pitch Spray", 20, 0)], centred=True)
p.route("Mod Wheel", "Density", 25)
p.play([48, 83])  # the kalimba's own range: its top tine is the B below C6

p = Preset("Tuned bows", ["FX", "Strings", "Bright"],
           "Cellos rung through a resonator on every grain, each tuned to a note of the song's key: a harp made of bows.",
           "Atmospheres", "Per-grain resonators with Cutoff to Key: noise and bowing become tuned, ringing notes.")
p.source(1, "Strings/Cello section/Sustain.sfz")
p.p({"grain.mode": "Cloud", "grain.density": 50, "grain.size": 170, "pos": 40, "pos.spray": 30,
     "gfilter.on": True, "gfilter.type": "Resonator", "gfilter.cutoff": 900, "gfilter.spray": 2.5, "gfilter.res": 20,
     "gfilter.snap": True, "gfilter.key": 100, "pan.spray": 90, "out.width": 140, "amp.vel": 30})
pad_filters(p, 9000, 150, 0)
pad_env(p, 700, 3000)
common_macros(p, grain=25, drift=("Density", [("Density", 18, 0)]), character=("Ring", [("Grain Resonance", 40, 0)]))
p.macro(5, "Spread", [("Cutoff Spray", 20, 0)], centred=True)
p.p({"lfo1.rate": 0.07, "lfo1.mode": "Free", "lfo1.poly": False}).route("LFO 1", "Grain Cutoff", 10, aux="Macro 6")
p.macros[6] = ("Motion", [], False, 40)
p.route("Mod Wheel", "Grain Resonance", 30)

p = Preset("Wood swarm", ["FX", "Dirty", "Wide"],
           "Xylophone attacks in a dense, buzzing swarm, like insects in a hollow log.",
           "Atmospheres", "Very short Gaussian grains, very many: below 20 ms a grain is a click, and a cloud of clicks buzzes.")
p.source(1, "Tuned percussion/Xylophone/Xylophone.sfz")
p.p({"grain.mode": "Cloud", "grain.density": 160, "grain.size": 14, "grain.window": "Gaussian", "grain.max": 128,
     "pos": 2, "pos.spray": 20, "pos.dist": "Ahead", "grain.levelspray": 40, "grain.even": 60, "pitch.spray": 3,
     "pan.spray": 100,
     "out.width": 150, "amp.vel": 30})
pad_filters(p, 6000, 250, 0)
p.p({"filter1.type": "LP 24"})
pad_env(p, 400, 1500)
common_macros(p, place=10, tone=40, drift=("Swarm", [("Density", 18, 0)]), character=("Grit", [("Crush", 35, 0)]))
p.macro(3, "Grain", [("Size", 30, 0)], centred=True)
p.route("Mod Wheel", "Density", 15)

# ============================================================================================
# Drones
# ============================================================================================
p.play([48, 96])

p = Preset("Bell clusters", ["Pad", "FX", "Heavy"],
           "Tubular bells stretched to a crawl until they become slowly turning organ clusters.",
           "Drones", "Stretching reveals a sound's inside: the playhead creeps through the bell at 2 % while long grains hold its partials.")
p.source(1, "Tuned percussion/Tubular bells/Tubular bells.sfz")
p.p({"pos": 5, "scan.rate": 2, "scan.end": "Bounce", "grain.size": 520, "grain.overlap": 8, "grain.jitter": 20,
     "pos.spray": 4, "grain.sizespray": 30, "grain.even": 55, "pan.spray": 70, "out.width": 130, "amp.vel": 30})
pad_filters(p, 6000, 80, 20)
pad_env(p, 2000, 5000)
detune(p, 5)
common_macros(p, place=10, grain=25, drift=("Crawl", [("Scan", 3, 0)]), character=("Reverse", [("Reverse", 50, 0)]))
p.macro(5, "Shimmer", [("Pitch Spray", 20, 0)])
p.p({"pitch.quant": "Octaves", "pitch.dir": "Up"})
p.p({"lfo1.rate": 0.05, "lfo1.mode": "Free", "lfo1.poly": False}).route("LFO 1", "Position Spray", 6, aux="Macro 6")
p.macros[6] = ("Motion", [], False, 40)
p.route("Mod Wheel", "Scan", 1.5)

p = Preset("Cathedral drone", ["Pad", "Keys", "Heavy"],
           "A full organ an octave down, held by Latch, its position drifting so the stops seem to breathe.",
           "Drones", "The organ is the steadiest source there is: frozen long grains, an LFO under 0.1 Hz on Position, Latch to hold it.")
p.source(1, "Keys/Pipe organ/Loud.sfz")
p.p({"pos": 40, "pos.spray": 8, "grain.size": 420, "grain.overlap": 8, "grain.jitter": 15, "grain.sizespray": 30,
     "pitch.transpose": -12, "voice.latch": True, "pan.spray": 60, "out.width": 130, "amp.vel": 20})
pad_filters(p, 4500, 45, 30)
pad_env(p, 1800, 4500)
detune(p, 4)
pad_motion(p, 0.04, 6, 50)
common_macros(p, grain=25, tone=40, drift=("Drift", [("Scan", 3, 0)]))
p.macro(5, "Fifth", [("Pitch Spray", 25, 0)])
p.p({"pitch.quant": "Fifths", "pitch.dir": "Up"})
p.play([24, 72]).route("Mod Wheel", "Filter 1 Cutoff", 15)

p = Preset("Abyss", ["Bass", "Pad", "Heavy"],
           "A double bass bowing two octaves down, smoothed into a bottomless, slowly moving floor.",
           "Drones", "Gaussian grains (the cleanest spectrum) on a low arco, a closed low pass: weight without rumble.")
p.source(1, "Strings/Double bass/Sustain.sfz")
p.p({"pos": 40, "pos.spray": 10, "grain.size": 320, "grain.overlap": 9, "grain.window": "Gaussian", "grain.jitter": 15,
     "grain.sizespray": 30, "pitch.transpose": -12, "pan.spray": 30, "amp.vel": 30})
pad_filters(p, 700, 30, 40)
pad_env(p, 2000, 4000)
detune(p, 4)
pad_motion(p, 0.05, 4, 40)
common_macros(p, grain=25, tone=40, drift=("Drift", [("Scan", 3, 0)]))
p.macro(5, "Growl", [("Filter 1 Drive", 50, 0)])
p.play([24, 60])

p = Preset("Smeared piano", ["Pad", "Keys", "Dirty"],
           "A piano an octave down smeared into a continuous, faintly worn tone.",
           "Drones", "A dense cloud from all over the note, pitched down and lightly crushed: the piano becomes a drone with tape-like grain.")
p.source(1, "Keys/Grand piano/Grand piano.sfz", end=22)
p.p({"grain.mode": "Cloud", "grain.density": 90, "grain.size": 320, "grain.max": 128, "pos": 20, "pos.spray": 70,
     "pos.dist": "Ahead", "grain.sizespray": 30, "pitch.transpose": -12, "gcolor.crush": 12, "grain.even": 80,
     "pan.spray": 70, "out.width": 120, "amp.vel": 30})
pad_filters(p, 2400, 50, 30)
pad_env(p, 1500, 3500)
pad_motion(p, 0.05, 4, 40)
common_macros(p, place=20, grain=25, drift=("Density", [("Density", 18, 0)]), character=("Wear", [("Crush", 40, 0)]))
p.macro(5, "Shimmer", [("Pitch Spray", 20, 0)])
p.p({"pitch.quant": "Octaves", "pitch.dir": "Up"})

p = Preset("Tuba earth", ["Bass", "Pad", "Warm"],
           "A tuba's voice an octave down at any note, its character slowly opening and closing.",
           "Drones", "Pitched mode keeps the tuba's formants while the grains set the note; an LFO on Formant makes it speak slowly.")
p.source(1, "Brass/Tuba/Sustain.sfz")
p.p({"grain.mode": "Pitched", "grain.periods": 2, "pos": 40, "pitch.transpose": -12, "pan.spray": 0, "amp.vel": 30})
pad_filters(p, 2500, 30, 50)
pad_env(p, 600, 2500)
p.p({"lfo1.rate": 0.12, "lfo1.mode": "Free", "lfo1.poly": False}).route("LFO 1", "Formant", 6, aux="Macro 6")
p.macros[6] = ("Speak", [], False, 50)
common_macros(p, tone=40, drift=("Drift", [("Scan", 3, 0)]))
p.macro(3, "Character", [("Formant", 25, 0)], centred=True)
p.play([24, 60])

p = Preset("Harmonic field", ["Pad", "Strings", "Wide"],
           "Hold one note and Latch spreads it into the song's chord on that degree, played by violas and cellos.",
           "Drones", "Pitch quantised to the Key Chord: one key down becomes the chord the song's key builds on it; two sources give the section.")
p.source(1, "Strings/Viola section/Sustain.sfz").source(2, "Strings/Cello section/Sustain.sfz", tune=-12)
p.p({"grain.mode": "Cloud", "grain.density": 40, "grain.size": 480, "pos": 40, "pos.spray": 20, "grain.sizespray": 30,
     "pitch.spray": 16, "pitch.dir": "Up", "pitch.quant": "Chord", "pitch.chord": "Key Chord", "voice.latch": True,
     "src.mix": 35, "pan.spray": 80, "pan.mode": "By Pitch", "out.width": 130, "amp.vel": 30})
pad_filters(p, 6500, 70)
pad_env(p, 1800, 4000)
detune(p, 6)
pad_motion(p)
common_macros(p, grain=25, drift=("Drift", [("Scan", 3, 0)]))
p.macro(5, "Spread", [("Pitch Spray", 20, 0)], centred=True)
p.macro(8, "Depth", [("Source Mix", 50, 0)])

# ============================================================================================
# Keys and plucks
# ============================================================================================

p = Preset("Held strike", ["Keys", "Pad", "Bright"],
           "The very moment a piano's hammer strikes, held in a loop for as long as the key is down.",
           "Keys", "Loop mode on the attack: a short loop crossfaded into itself, so the strike itself becomes a sustained, shimmering tone.")
p.source(1, "Keys/Grand piano/Grand piano.sfz", end=25)
p.p({"grain.mode": "Loop", "grain.size": 90, "grain.xfade": 45, "pos": 8, "pos.spray": 3, "grain.jitter": 10,
     "pan.spray": 25, "amp.vel": 60})
pad_filters(p, 9000, 80, 30)
p.p({"env1.a": 2, "env1.d": 5000, "env1.s": 70, "env1.r": 1500})
detune(p, 3)
common_macros(p, place=20, drift=("Drift", [("Scan", 3, 0)]), character=("Reverse", [("Reverse", 40, 0)]))
p.macro(3, "Loop", [("Size", 25, 0)], centred=True)
p.macro(5, "Shimmer", [("Pitch Spray", 20, 0)])
p.p({"pitch.quant": "Octaves", "pitch.dir": "Up"})
p.route("Velocity", "Filter 1 Cutoff", 10)

p = Preset("Tape strings", ["Pad", "Strings", "Warm"],
           "Violins on a worn tape loop that wanders back and forth through the note, a little hiss in the grain.",
           "Pads", "Loop mode wandering: long crossfaded loops, the playhead bouncing at half speed, a touch of Crush and a closed low pass for tape.")
p.source(1, "Strings/Violin section/Sustain.sfz", start=8, end=92)
p.p({"grain.mode": "Loop", "grain.size": 420, "grain.xfade": 30, "scan.rate": 45, "scan.end": "Bounce",
     "gcolor.crush": 8, "pan.spray": 35, "out.width": 120, "amp.vel": 40})
pad_filters(p, 5000, 110)
pad_env(p, 600, 2200)
p.p({"lfo1.rate": 0.35, "lfo1.mode": "Free", "lfo1.poly": False}).route("LFO 1", "Pitch", 0.4, aux="Macro 6")
p.macros[6] = ("Wow", [], False, 40)
common_macros(p, grain=25, drift=("Speed", [("Scan", 6, 0)]), character=("Wear", [("Crush", 30, 0)]))
p.macro(5, "Shimmer", [("Pitch Spray", 20, 0)])
p.p({"pitch.quant": "Octaves", "pitch.dir": "Up"})

p = Preset("Infinite piano", ["Keys", "Pad", "Warm"],
           "A piano that plays its attack as recorded and then never fades: the note simply stays.",
           "Keys", "Keep the attack, stretch the body: the playhead runs at the recording's speed through the attack, then holds at the end of a short part.")
p.source(1, "Keys/Grand piano/Grand piano.sfz", end=22)
p.p({"scan.rate": 100, "scan.end": "Stop", "grain.size": 140, "grain.overlap": 4, "grain.jitter": 10,
     "grain.sizespray": 20, "grain.even": 45, "pan.spray": 40, "amp.vel": 60})
pad_filters(p, 9000, 60, 40)
p.p({"env1.a": 2, "env1.d": 4000, "env1.s": 100, "env1.r": 1600})
detune(p, 3)
common_macros(p, place=10, grain=25, drift=("Hold", [("Source 1 End", 30, 0)]), character=("Reverse", [("Reverse", 40, 0)]))
p.macro(5, "Shimmer", [("Pitch Spray", 20, 0)])
p.p({"pitch.quant": "Octaves", "pitch.dir": "Up"})
p.route("Velocity", "Filter 1 Cutoff", 10)

p = Preset("Harpsichord shards", ["Pluck", "Keys", "Bright"],
           "Harpsichord attacks broken into shards an octave apart, glinting around each note.",
           "Keys", "Only the attacks, through a sharp window, each shard landing on the note or an octave away.")
p.source(1, "Keys/Harpsichord/Harpsichord.sfz")
p.p({"grain.mode": "Cloud", "grain.density": 18, "grain.size": 160, "grain.window": "Sharp Attack", "pos": 0,
     "pos.spray": 10, "pos.dist": "Ahead", "pitch.spray": 12, "pitch.quant": "Octaves", "grain.levelspray": 30,
     "grain.even": 40,
     "pan.spray": 80, "out.width": 130, "amp.vel": 50})
pad_filters(p, 10000, 150, 30)
p.p({"env1.a": 2, "env1.d": 1600, "env1.s": 25, "env1.r": 900})
common_macros(p, place=10, drift=("Density", [("Density", 25, 0)]), character=("Reverse", [("Reverse", 40, 0)]))
p.macro(3, "Grain", [("Size", 30, 0)], centred=True)
p.macro(5, "Octaves", [("Pitch Spray", 20, 0)], centred=True)
p.route("Velocity", "Density", 12)

p = Preset("Strummed pizzicato", ["Pluck", "Strings", "Bright"],
           "Each key strums a major seventh chord of violin plucks, the notes arriving one after another.",
           "Keys", "A sparse cloud of whole plucks quantised to a chord: the strum is the cloud's own scattered timing.")
p.source(1, "Strings/Violin section/Pizzicato.sfz")
p.p({"grain.mode": "Cloud", "grain.density": 14, "grain.size": 600, "grain.window": "Sharp Attack", "pos": 0,
     "pos.spray": 3, "pos.dist": "Ahead", "pitch.spray": 14, "pitch.dir": "Up", "pitch.quant": "Chord",
     "pitch.chord": "Major 7", "pan.spray": 80, "out.width": 130, "amp.vel": 50})
pad_filters(p, 10000, 150, 20)
p.p({"env1.a": 2, "env1.d": 2500, "env1.s": 0, "env1.r": 1200})
common_macros(p, place=6, drift=("Strum", [("Density", 25, 0)]), character=("Reverse", [("Reverse", 40, 0)]))
p.macro(5, "Spread", [("Pitch Spray", 20, 0)], centred=True)
p.route("Velocity", "Density", 15)

p = Preset("Marimba bloom", ["Keys", "Pluck", "Warm"],
           "A marimba's stroke blooming into a soft haze as its playhead slows and half its grains turn back.",
           "Keys", "Scan at half speed with reversed grains: the attack is heard, then dissolves into its own echo.")
p.source(1, "Tuned percussion/Marimba/Marimba.sfz")
p.p({"scan.rate": 50, "scan.end": "Stop", "grain.size": 200, "grain.overlap": 4, "grain.jitter": 20,
     "grain.reverse": 40, "pos.spray": 5, "grain.even": 50, "pan.spray": 60, "out.width": 120, "amp.vel": 50})
pad_filters(p, 8000, 100, 30)
p.p({"env1.a": 2, "env1.d": 3000, "env1.s": 30, "env1.r": 1500})
common_macros(p, place=10, grain=25, drift=("Slow", [("Scan", -5, 0)]), character=("Reverse", [("Reverse", 40, 0)]))
p.macro(5, "Shimmer", [("Pitch Spray", 20, 0)])
p.p({"pitch.quant": "Octaves", "pitch.dir": "Up"})
p.route("Velocity", "Filter 1 Cutoff", 10)

p = Preset("Glass mallet", ["Keys", "Bright", "Clean"],
           "Struck glass: a bright attack and a short ring, played like a mallet instrument.",
           "Keys", "A sustained glass with an amp envelope that decays, short grains on a sharp window giving each note a strike.")
p.source(1, "Tuned percussion/Wine glasses/Wine glasses.sfz")
p.p({"pos": 30, "pos.spray": 8, "grain.size": 90, "grain.overlap": 4, "grain.jitter": 15, "grain.sizespray": 25,
     "pan.spray": 40, "amp.vel": 60})
pad_filters(p, 11000, 200, 30)
p.p({"env1.a": 1, "env1.d": 1300, "env1.s": 0, "env1.r": 700, "env1.dcurve": 70})
detune(p, 6)
common_macros(p, drift=("Ring", [("Size", 20, 0)]), character=("Shimmer", [("Pitch Spray", 15, 0)]))
p.macro(3, "Grain", [("Size", 25, 0)], centred=True)
p.p({"pitch.quant": "Octaves", "pitch.dir": "Up"})
p.route("Velocity", "Filter 1 Cutoff", 12)
p.play([48, 96])

p = Preset("Kalimba echoes", ["Pluck", "Keys", "Wide"],
           "Kalimba notes that repeat on the song's dotted eighths and fade, a granular echo.",
           "Keys", "Rhythm on the grid with Repeats: each step's grain played twice, the amp's decay fading the echoes.")
p.source(1, "Tuned percussion/Kalimba/Kalimba.sfz")
p.p({"grain.mode": "Rhythm", "grain.rate": "1/8D", "grain.size": 220, "grain.repeats": 2, "grain.window": "Sharp Attack",
     "pos": 0, "pos.spray": 3, "pos.dist": "Ahead", "grain.chance": 85, "pan.spray": 70, "pan.mode": "Alternate",
     "out.width": 130, "amp.vel": 50})
pad_filters(p, 9000, 150, 20)
p.p({"env1.a": 1, "env1.d": 3500, "env1.s": 0, "env1.r": 1800})
common_macros(p, place=6, drift=("Echoes", [("Chance", 15, 0)]), character=("Reverse", [("Reverse", 40, 0)]))
p.macro(5, "Range", [("Pitch Spray", 15, 0)])
p.p({"pitch.quant": "Song Key"})
p.play([48, 83])  # the kalimba's own range: its top tine is the B below C6

p = Preset("Plucked upright", ["Bass", "Pluck", "Warm"],
           "A double bass pluck played through in grains: round, woody and short.",
           "Keys", "Scan at the recording's speed, Stop at the end: the pluck plays as recorded, its grains giving a soft, woody edge.")
p.source(1, "Strings/Double bass/Pizzicato.sfz")
p.p({"scan.rate": 100, "scan.end": "Stop", "grain.size": 70, "grain.overlap": 3, "pan.spray": 10, "amp.vel": 60})
p.p({"filter1.type": "LP 24", "filter1.cutoff": 1400, "filter1.env": 35, "filter1.key": 40, "env2.d": 500,
     "env1.a": 1, "env1.d": 1600, "env1.s": 0, "env1.r": 300})
common_macros(p, place=10, tone=40)
p.macro(3, "Grain", [("Size", 25, 0)], centred=True)
p.play([24, 60]).route("Velocity", "Filter 1 Cutoff", 10)

# ============================================================================================
# Leads
# ============================================================================================

p = Preset("Vowel oboe", ["Lead", "Woodwinds", "Bright"],
           "An oboe as an oscillator: any note, its reed voice kept; the mod wheel turns its vowel.",
           "Leads", "Pitched mode at two periods, PSOLA's clean case: the note from the grains, the character from the oboe.")
p.source(1, "Woodwinds/Oboe/Sustain.sfz")
p.p({"grain.mode": "Pitched", "grain.periods": 2, "pos": 40, "pan.spray": 0, "voice.mode": "Legato", "voice.glide": 60,
     "voice.count": 1, "amp.vel": 40, "lfo1.rate": 5.2, "lfo1.fade": 600})
p.p({"filter1.type": "LP 12", "filter1.cutoff": 9000, "filter1.res": 10})
p.p({"env1.a": 15, "env1.d": 1000, "env1.s": 100, "env1.r": 300})
p.route("LFO 1", "Pitch", 0.8, aux="Macro 6").route("Mod Wheel", "Formant", 14)
p.macro(1, "Place", [("Position", 30, 0)], centred=True)
p.macro(3, "Character", [("Formant", 20, 0)], centred=True)
p.macro(4, "Tone", [("Filter 1 Cutoff", 30, 0)], centred=True)
p.macro(6, "Vibrato", [], value=50)

p = Preset("Muted wah", ["Lead", "Brass", "Bright"],
           "A muted trumpet as an oscillator whose voice opens with each note, a wah in its grains.",
           "Leads", "Env 3 moves the formant: every note starts closed and opens, the trumpet's mute speaking.")
p.source(1, "Brass/Trumpet/Muted.sfz")
p.p({"grain.mode": "Pitched", "grain.periods": 3, "grain.formant": -5, "pos": 40, "pan.spray": 0, "voice.mode": "Mono",
     "voice.glide": 40, "amp.vel": 40, "env3.a": 5, "env3.d": 450, "env3.s": 25, "env3.r": 300})
p.p({"filter1.type": "LP 12", "filter1.cutoff": 8000})
p.p({"env1.a": 8, "env1.d": 1000, "env1.s": 100, "env1.r": 250})
p.route("Env 3", "Formant", 22).route("Velocity", "Formant", 6)
p.macro(1, "Place", [("Position", 30, 0)], centred=True)
p.macro(3, "Character", [("Formant", 15, 0)], centred=True)
p.macro(4, "Tone", [("Filter 1 Cutoff", 30, 0)], centred=True)
p.macro(7, "Wah", [("Formant", 15, 0)])

p = Preset("Singing violin", ["Lead", "Strings", "Warm"],
           "A violin that sings any line legato, a vibrato coming in as each note holds.",
           "Leads", "Pitched at three periods on the solo violin, legato with glide, a delayed vibrato on Pitch.")
p.source(1, "Strings/Solo violin/Sustain.sfz")
p.p({"grain.mode": "Pitched", "grain.periods": 3, "pos": 45, "pan.spray": 0, "voice.mode": "Legato", "voice.glide": 80,
     "voice.count": 1, "amp.vel": 40, "lfo1.rate": 5.5, "lfo1.fade": 700})
p.p({"filter1.type": "LP 12", "filter1.cutoff": 9000, "filter1.res": 10})
p.p({"env1.a": 60, "env1.d": 1000, "env1.s": 100, "env1.r": 400})
p.route("LFO 1", "Pitch", 0.6, aux="Macro 6").route("Mod Wheel", "Formant", 10)
p.macro(1, "Place", [("Position", 30, 0)], centred=True)
p.macro(3, "Character", [("Formant", 15, 0)], centred=True)
p.macro(4, "Tone", [("Filter 1 Cutoff", 30, 0)], centred=True)
p.macro(6, "Vibrato", [], value=60)

p = Preset("Grit sax", ["Lead", "Woodwinds", "Dirty"],
           "A tenor sax lead with grit in every grain, each crushed a little differently.",
           "Leads", "Pitched sax through Crush with Crush Spray and a driven ladder: a growl that changes grain to grain.")
p.source(1, "Woodwinds/Tenor sax/Tenor sax.sfz")
p.p({"grain.mode": "Pitched", "grain.periods": 2, "pos": 40, "gcolor.crush": 28, "gcolor.crushspray": 50,
     "pan.spray": 0, "voice.mode": "Mono", "voice.glide": 50, "amp.vel": 40})
p.p({"filter1.type": "Ladder", "filter1.cutoff": 4000, "filter1.res": 25, "filter1.drive": 30})
p.p({"env1.a": 8, "env1.d": 1000, "env1.s": 100, "env1.r": 250})
p.route("Velocity", "Crush", 15)
p.macro(1, "Place", [("Position", 30, 0)], centred=True)
p.macro(3, "Character", [("Formant", 15, 0)], centred=True)
p.macro(4, "Tone", [("Filter 1 Cutoff", 30, 0)], centred=True)
p.macro(8, "Grit", [("Crush", 30, 0), ("Filter 1 Drive", 30, 0)])
p.route("Mod Wheel", "Crush", 25)

p = Preset("Buzz harmonica", ["Lead", "Woodwinds", "Bright"],
           "A harmonica turned into a buzzing reed lead, its filter snapping open on each note.",
           "Leads", "One period a grain: brighter and buzzier than PSOLA's two, with a filter envelope for bite.")
p.source(1, "Woodwinds/Harmonica/Harmonica.sfz")
p.p({"grain.mode": "Pitched", "grain.periods": 1, "pos": 40, "pan.spray": 0, "voice.mode": "Mono", "voice.glide": 30,
     "amp.vel": 40})
p.p({"filter1.type": "LP 24", "filter1.cutoff": 1500, "filter1.res": 30, "filter1.env": 45, "filter1.key": 60,
     "env2.a": 2, "env2.d": 350, "env2.s": 20})
p.p({"env1.a": 5, "env1.d": 800, "env1.s": 90, "env1.r": 200})
p.macro(1, "Place", [("Position", 30, 0)], centred=True)
p.macro(3, "Character", [("Formant", 15, 0)], centred=True)
p.macro(4, "Tone", [("Filter 1 Cutoff", 30, 0)], centred=True)
p.macro(5, "Body", [("Periods", 25, 0)])
p.route("Velocity", "Filter 1 Cutoff", 10).route("Mod Wheel", "Formant", 10)

p = Preset("Horn lead", ["Lead", "Brass", "Warm"],
           "A French horn as a warm, round lead that glides between notes.",
           "Leads", "Pitched horn at three periods, legato glide and a slow vibrato: the horn's voice on any line.")
p.source(1, "Brass/French horn/Sustain.sfz")
p.p({"grain.mode": "Pitched", "grain.periods": 3, "pos": 40, "pan.spray": 0, "voice.mode": "Legato", "voice.glide": 90,
     "voice.count": 1, "amp.vel": 40, "lfo1.rate": 4.8, "lfo1.fade": 900})
p.p({"filter1.type": "LP 12", "filter1.cutoff": 5000, "filter1.res": 10})
p.p({"env1.a": 40, "env1.d": 1000, "env1.s": 100, "env1.r": 400})
p.route("LFO 1", "Pitch", 0.5, aux="Macro 6")
p.macro(1, "Place", [("Position", 30, 0)], centred=True)
p.macro(3, "Character", [("Formant", 15, 0)], centred=True)
p.macro(4, "Tone", [("Filter 1 Cutoff", 30, 0)], centred=True)
p.macro(6, "Vibrato", [], value=50)
p.route("Mod Wheel", "Formant", 10).route("Velocity", "Filter 1 Cutoff", 8)

# ============================================================================================
# Basses
# ============================================================================================

p = Preset("Bassoon bass", ["Bass", "Woodwinds", "Warm"],
           "A bassoon an octave down as a mono bass, its voice lifted to stay clear in a mix.",
           "Basses", "Pitched at three periods, Formant raised so the low notes keep their reed rather than going dull.")
p.source(1, "Woodwinds/Bassoon/Sustain.sfz")
p.p({"grain.mode": "Pitched", "grain.periods": 3, "grain.formant": 4, "pos": 40, "pitch.transpose": -12, "pan.spray": 0,
     "voice.mode": "Mono", "voice.glide": 70, "amp.vel": 40})
p.p({"filter1.type": "LP 24", "filter1.cutoff": 2200, "filter1.env": 30, "filter1.key": 50, "env2.d": 400, "env2.s": 30})
p.p({"env1.a": 4, "env1.d": 800, "env1.s": 100, "env1.r": 180})
p.macro(1, "Place", [("Position", 30, 0)], centred=True)
p.macro(3, "Character", [("Formant", 15, 0)], centred=True)
p.macro(4, "Tone", [("Filter 1 Cutoff", 30, 0)], centred=True)
p.play([24, 60]).route("Velocity", "Filter 1 Cutoff", 10)

p = Preset("Trombone growl", ["Bass", "Brass", "Dirty"],
           "A trombone bass with a growl under it, a ring an octave below each grain.",
           "Basses", "Pitched trombone, ring modulated at half the grain's pitch (a sub-octave growl) through a driven filter.")
p.source(1, "Brass/Trombone/Sustain.sfz")
p.p({"grain.mode": "Pitched", "grain.periods": 2, "pos": 40, "gcolor.ring": 30, "gcolor.ratio": 0.5, "pan.spray": 0,
     "voice.mode": "Mono", "voice.glide": 50, "amp.vel": 40})
p.p({"filter1.type": "Ladder", "filter1.cutoff": 1800, "filter1.res": 20, "filter1.drive": 40, "filter1.env": 25,
     "env2.d": 500, "env2.s": 30})
p.p({"env1.a": 4, "env1.d": 800, "env1.s": 100, "env1.r": 180})
p.macro(1, "Place", [("Position", 30, 0)], centred=True)
p.macro(3, "Character", [("Formant", 15, 0)], centred=True)
p.macro(4, "Tone", [("Filter 1 Cutoff", 30, 0)], centred=True)
p.macro(8, "Growl", [("Ring", 30, 0), ("Filter 1 Drive", 20, 0)])
p.play([24, 60]).route("Mod Wheel", "Ring", 30)

p = Preset("Tuba mono", ["Bass", "Brass", "Warm"],
           "A round, deep tuba bass that slides between notes.",
           "Basses", "Pitched tuba at two periods, mono with glide, low-passed: weight and a soft attack.")
p.source(1, "Brass/Tuba/Sustain.sfz")
p.p({"grain.mode": "Pitched", "grain.periods": 2, "pos": 40, "pan.spray": 0, "voice.mode": "Mono", "voice.glide": 90,
     "amp.vel": 40})
p.p({"filter1.type": "LP 24", "filter1.cutoff": 1700, "filter1.res": 15, "filter1.key": 15})
p.p({"env1.a": 10, "env1.d": 800, "env1.s": 100, "env1.r": 200})
p.macro(1, "Place", [("Position", 30, 0)], centred=True)
p.macro(3, "Character", [("Formant", 15, 0)], centred=True)
p.macro(4, "Tone", [("Filter 1 Cutoff", 30, 0)], centred=True)
p.play([36, 72])  # the tuba's own range.route("Velocity", "Filter 1 Cutoff", 10)

p = Preset("Pedal buzz", ["Bass", "Keys", "Dirty"],
           "An organ pedal turned into a buzzing, driven bass.",
           "Basses", "Pitched organ at one period a grain, the brightest and buzziest, through a driven ladder.")
p.source(1, "Keys/Pipe organ/Loud.sfz")
p.p({"grain.mode": "Pitched", "grain.periods": 1, "pos": 40, "pitch.transpose": -12, "pan.spray": 0, "voice.mode": "Mono",
     "voice.glide": 40, "amp.vel": 40})
p.p({"filter1.type": "Ladder", "filter1.cutoff": 900, "filter1.res": 30, "filter1.drive": 45, "filter1.env": 30,
     "env2.d": 300, "env2.s": 20})
p.p({"env1.a": 3, "env1.d": 800, "env1.s": 100, "env1.r": 150})
p.macro(1, "Place", [("Position", 30, 0)], centred=True)
p.macro(3, "Character", [("Formant", 15, 0)], centred=True)
p.macro(4, "Tone", [("Filter 1 Cutoff", 30, 0)], centred=True)
p.macro(5, "Body", [("Periods", 20, 0)])
p.play([24, 60]).route("Velocity", "Filter 1 Cutoff", 10)

# ============================================================================================
# Rhythm and glitch
# ============================================================================================

p = Preset("Spiccato machine", ["Pluck", "Strings", "Bright"],
           "A cello section's bounced bows sequenced in swung sixteenths, each step a different stroke.",
           "Rhythm", "Rhythm on the song's grid, Snap to transients so every grain starts on a stroke, Chance leaving gaps.")
p.source(1, "Strings/Cello section/Spiccato.sfz")
p.p({"grain.mode": "Rhythm", "grain.rate": "1/16", "grain.swing": 20, "grain.chance": 80, "grain.size": 110,
     "grain.window": "Sharp Attack", "pos": 0, "pos.spray": 100, "pos.dist": "Ahead", "pos.snap": "Transients",
     "pan.spray": 50, "amp.vel": 50})
pad_filters(p, 9000, 100, 20)
p.p({"env1.a": 1, "env1.d": 1000, "env1.s": 100, "env1.r": 300})
common_macros(p, place=10, drift=("Gaps", [("Chance", -40, 0)]), character=("Reverse", [("Reverse", 40, 0)]))
p.macro(3, "Grain", [("Size", 25, 0)], centred=True)
p.macro(5, "Swing", [("Swing", 40, 0)])
p.route("Mod Wheel", "Swing", 30)

p = Preset("Tremolo stutter", ["FX", "Strings", "Dirty"],
           "Tremolo violins chopped in thirty-seconds, stuttering four times, jumping place on every sixteenth.",
           "Rhythm", "Repeats for the stutter, a synced sample-and-hold (Random 1, Steps, 1/16) on Position for the jumps.")
p.source(1, "Strings/Violin section/Tremolo.sfz")
p.p({"grain.mode": "Rhythm", "grain.rate": "1/32", "grain.size": 45, "grain.repeats": 4, "grain.window": "Square",
     "grain.flat": 60, "pos": 40, "pan.spray": 40, "pan.mode": "Alternate", "amp.vel": 50,
     "rnd1.mode": "Steps", "rnd1.sync": "1/16", "rnd1.poly": False})
pad_filters(p, 9000, 200, 20)
p.p({"env1.a": 1, "env1.d": 1000, "env1.s": 100, "env1.r": 200})
p.route("Random 1", "Position", 25, aux="Macro 6")
p.macros[6] = ("Jumps", [], False, 60)
common_macros(p, place=20, character=("Crush", [("Crush", 40, 0)]))
p.macro(3, "Grain", [("Size", 25, 0)], centred=True)
p.macro(7, "Stutter", [("Chance", -30, 0)])
p.route("Mod Wheel", "Crush", 30)

p = Preset("Marimba shuffle", ["Pluck", "Keys", "Wide"],
           "Swung sixteenths of marimba wandering over the song's key, every note from a different stroke.",
           "Rhythm", "Grain Random on Position picks a new stroke each step; Quantize to the song's key turns the spray into a melody.")
p.source(1, "Tuned percussion/Marimba/Marimba.sfz")
p.p({"grain.mode": "Rhythm", "grain.rate": "1/16", "grain.swing": 22, "grain.size": 140, "grain.window": "Sharp Attack",
     "pos": 0, "pitch.spray": 12, "pitch.quant": "Song Key", "pan.spray": 80, "out.width": 130, "amp.vel": 50})
pad_filters(p, 9000, 120, 20)
p.p({"env1.a": 1, "env1.d": 1000, "env1.s": 100, "env1.r": 500})
p.route("Grain Random", "Position", 4)
common_macros(p, place=6, drift=("Gaps", [("Chance", -40, 0)]), character=("Reverse", [("Reverse", 40, 0)]))
p.macro(5, "Range", [("Pitch Spray", 20, 0)], centred=True)
p.route("Mod Wheel", "Swing", 30)

p = Preset("Timpani fills", ["Drums", "FX", "Heavy"],
           "Timpani strokes in eighths, each repeated and sliding down, for falling tom-like fills.",
           "Rhythm", "Rhythm with Repeats and Grain Glide downwards, Snap to transients so each repeat is a stroke.")
p.source(1, "Percussion/Timpani/Hits.sfz")
p.p({"grain.mode": "Rhythm", "grain.rate": "1/8", "grain.repeats": 3, "grain.size": 120, "pitch.glide": -7,
     "pos.snap": "Transients", "pos.spray": 30, "pos.dist": "Ahead", "grain.window": "Sharp Attack",
     "pan.spray": 40, "amp.vel": 60})
pad_filters(p, 6000, 40, 30)
p.p({"env1.a": 1, "env1.d": 1000, "env1.s": 100, "env1.r": 400})
common_macros(p, place=10, drift=("Fall", [("Grain Glide", -20, 0)]), character=("Crack", [("Crush", 30, 0)]))
p.macro(3, "Grain", [("Size", 25, 0)], centred=True)
p.play([24, 72]).route("Velocity", "Filter 1 Cutoff", 10)

p = Preset("Reverse pump", ["Pad", "Brass", "FX"],
           "Horn swells pumping on the eighths like a sidechained pad, each grain rising into the beat.",
           "Rhythm", "A Sharp End window rises to each step and stops: grains on the grid become reverse swells.")
p.source(1, "Brass/French horn/Sustain.sfz")
p.p({"grain.mode": "Rhythm", "grain.rate": "1/8", "grain.size": 240, "grain.window": "Sharp End", "pos": 40,
     "pos.spray": 10, "pan.spray": 50, "out.width": 120, "amp.vel": 40})
pad_filters(p, 6000, 100)
pad_env(p, 300, 1500)
detune(p, 6)
common_macros(p, grain=25, drift=("Gaps", [("Chance", -30, 0)]), character=("Reverse", [("Reverse", 60, 0)]))
p.macro(5, "Fifths", [("Pitch Spray", 25, 0)])
p.p({"pitch.quant": "Fifths", "pitch.dir": "Up"})

p = Preset("Glitch organ", ["FX", "Keys", "Dirty"],
           "An organ fired at random, never on a clock, square-cut and crushed: a stuttering broken machine.",
           "Rhythm", "Fire from a random source rather than the grid: grains when it jumps past half, square windows and crush for the cut.")
p.source(1, "Keys/Pipe organ/Loud.sfz")
p.p({"grain.mode": "Rhythm", "grain.rate": "4 bars", "grain.size": 70, "grain.window": "Square", "grain.repeats": 2,
     "pos": 30, "pos.spray": 40, "gcolor.crush": 35, "gcolor.crushspray": 40, "pan.spray": 60,
     "rnd1.mode": "Steps", "rnd1.rate": 11, "amp.vel": 50})
pad_filters(p, 7000, 150, 20)
p.p({"env1.a": 1, "env1.d": 1000, "env1.s": 100, "env1.r": 300})
p.route("Random 1", "Fire", 100)
common_macros(p, place=20, character=("Crush", [("Crush", 30, 0)]))
p.macro(3, "Grain", [("Size", 25, 0)], centred=True)
p.macro(7, "Rate", [("Random 1 Rate", 25, 0)])
p.macro(5, "Octaves", [("Pitch Spray", 20, 0)])
p.p({"pitch.quant": "Octaves"})

# ============================================================================================
# Effects, risers, transitions
# ============================================================================================

p = Preset("Tremolo riser", ["FX", "Strings", "Bright"],
           "Hold a note and tremolo violins rise over eight seconds: thicker, higher, brighter, wider.",
           "FX", "Env 3 opens Density, Pitch, the high pass and the spray together: one held note builds a riser.")
p.source(1, "Strings/Violin section/Tremolo.sfz")
p.p({"grain.mode": "Cloud", "grain.density": 8, "grain.size": 90, "pos": 40, "pos.spray": 20, "pan.spray": 30,
     "out.width": 140, "amp.vel": 30, "env3.a": 8000, "env3.acurve": -40, "env3.s": 100, "env3.r": 500})
pad_filters(p, 12000, 150, 0)
pad_env(p, 300, 1500)
p.route("Env 3", "Density", 40).route("Env 3", "Pitch", 50).route("Env 3", "Filter 2 Cutoff", 30) \
 .route("Env 3", "Pan Spray", 60)
common_macros(p, grain=25)
p.macro(5, "Height", [("Pitch", 25, 0)])
p.macro(7, "Faster", [("Density", 20, 0)])

p = Preset("Falling shards", ["FX", "Bright", "Wide"],
           "Glass-bright glockenspiel grains each falling an octave as it fades.",
           "FX", "Grain Glide down an octave over each grain's life: every grain a little falling star.")
p.source(1, "Tuned percussion/Glockenspiel/Glockenspiel.sfz")
p.p({"grain.mode": "Cloud", "grain.density": 24, "grain.size": 360, "pos": 2, "pos.spray": 15, "pos.dist": "Ahead",
     "pitch.glide": -12, "pitch.spray": 12, "pitch.quant": "Octaves", "pan.spray": 100, "out.width": 150, "amp.vel": 30})
pad_filters(p, 11000, 200, 0)
pad_env(p, 100, 3000)
common_macros(p, place=10, drift=("Density", [("Density", 20, 0)]), character=("Reverse", [("Reverse", 50, 0)]))
p.macro(5, "Fall", [("Grain Glide", 25, 0)], centred=True)
p.play([48, 96])

p = Preset("Grain stop", ["FX", "Strings", "Heavy"],
           "Cellos play normally, then slow to a halt over a few seconds, dropping in pitch like a stopping tape.",
           "FX", "Env 3 brings Scan from the recording's speed to nothing and Pitch down an octave together.")
p.source(1, "Strings/Cello section/Sustain.sfz")
p.p({"pos": 10, "scan.rate": 100, "scan.end": "Stop", "grain.size": 120, "grain.overlap": 4, "pan.spray": 30,
     "amp.vel": 40, "env3.a": 2500, "env3.acurve": 40, "env3.s": 100, "env3.r": 300})
pad_filters(p, 8000, 60, 30)
pad_env(p, 50, 1200)
p.route("Env 3", "Scan", -12.5).route("Env 3", "Pitch", -50)
common_macros(p, place=10, grain=25)
p.macro(7, "Stop", [("Scan", -12.5, 0)])

p = Preset("Hold the moment", ["FX", "Strings", "Warm"],
           "Violins that play as recorded until the mod wheel freezes them where they are.",
           "FX", "Scan at the recording's speed; the mod wheel takes it to zero: a freeze you play.")
p.source(1, "Strings/Violin section/Sustain.sfz")
p.p({"pos": 5, "scan.rate": 100, "scan.end": "Bounce", "grain.size": 180, "grain.overlap": 4, "grain.jitter": 10,
     "pos.spray": 3, "pan.spray": 40, "amp.vel": 40})
pad_filters(p, 8000, 100)
pad_env(p, 300, 2000)
detune(p, 5)
p.route("Mod Wheel", "Scan", -12.5)
common_macros(p, place=10, grain=25)
p.macro(7, "Freeze", [("Scan", -12.5, 0)])
p.macro(5, "Shimmer", [("Pitch Spray", 20, 0)])
p.p({"pitch.quant": "Octaves", "pitch.dir": "Up"})

p = Preset("Impact tail", ["FX", "Drums", "Heavy"],
           "A timpani stroke whose tail becomes stretched, ringing bells.",
           "FX", "Both sources on every grain: the timpani played through, the bells frozen beside it, blending into each other.")
p.source(1, "Percussion/Timpani/Hits.sfz").source(2, "Tuned percussion/Tubular bells/Tubular bells.sfz", start=5, end=40, gain=-6)
p.p({"src.pick": "Both", "scan.rate": 60, "scan.end": "Stop", "grain.size": 300, "grain.overlap": 4, "pos.spray": 5,
     "grain.even": 40, "pan.spray": 60, "out.width": 130, "amp.vel": 60})
pad_filters(p, 6000, 40, 20)
p.p({"env1.a": 1, "env1.d": 6000, "env1.s": 0, "env1.r": 3000})
common_macros(p, place=10, grain=25)
p.macro(5, "Bells", [("Source 2 Gain", 15, 0)], centred=True)
p.play([24, 72])

# ============================================================================================
# Experiments
# ============================================================================================

p = Preset("Piano into organ", ["Keys", "Pad", "Warm"],
           "A piano that turns into an organ as you push the mod wheel, grain by grain.",
           "Experiments", "Two sources and the mix on the mod wheel: Random pick turns the blend into a morph.")
p.source(1, "Keys/Upright piano/Upright piano.sfz").source(2, "Keys/Pipe organ/Soft.sfz")
p.p({"src.mix": 0, "pos": 12, "pos.spray": 8, "grain.size": 260, "grain.overlap": 5, "grain.jitter": 15,
     "grain.even": 40, "pan.spray": 50, "out.width": 120, "amp.vel": 50})
pad_filters(p, 7000, 80)
pad_env(p, 200, 2500)
detune(p, 5)
p.route("Mod Wheel", "Source Mix", 100)
common_macros(p, place=15, grain=25)
p.macro(5, "Organ", [("Source Mix", 100, 0)])

p = Preset("Gamelan vibes", ["Keys", "Bright", "FX"],
           "Vibraphone rung into gamelan-like metal, each grain at its own clangorous ratio.",
           "Experiments", "Ring modulation at an inharmonic ratio, sprayed per grain: struck metal from a vibraphone.")
p.source(1, "Tuned percussion/Vibraphone/Vibraphone.sfz")
p.p({"pos": 5, "pos.spray": 10, "grain.size": 220, "grain.overlap": 4, "gcolor.ring": 45, "gcolor.ratio": 3.5,
     "gcolor.ringspray": 20, "pan.spray": 60, "out.width": 120, "amp.vel": 50})
pad_filters(p, 9000, 150, 20)
p.p({"env1.a": 2, "env1.d": 3500, "env1.s": 20, "env1.r": 2000})
common_macros(p, place=10, grain=25, character=("Clang", [("Ring", 30, 0)]))
p.macro(5, "Ratio", [("Ring Ratio", 20, 0)], centred=True)
p.route("Velocity", "Ring", 15)

p = Preset("Song scan", ["Pad", "Strings", "Subtle"],
           "Violins read once through every four bars, following the song wherever it plays.",
           "Experiments", "The Song playhead: the position follows the song, so a part of the arrangement always sounds the same part of the note.")
p.source(1, "Strings/Violin section/Sustain.sfz", start=10, end=90)
p.p({"scan.from": "Song", "scan.sync": "4 bars", "scan.end": "Bounce", "grain.size": 320, "grain.overlap": 6,
     "grain.jitter": 15, "pos.spray": 5, "grain.sizespray": 30, "pan.spray": 60, "out.width": 125, "amp.vel": 40})
pad_filters(p, 8000, 110)
pad_env(p, 600, 2500)
detune(p, 6)
common_macros(p, grain=25)
p.macro(5, "Shimmer", [("Pitch Spray", 20, 0)])
p.p({"pitch.quant": "Octaves", "pitch.dir": "Up"})

p = Preset("Breath drone", ["Pad", "Woodwinds", "Subtle"],
           "A harmonica held by Latch, each note you add joining a slow, breathing drone.",
           "Experiments", "Latch holds notes until the next; a slow cloud with Even keeps the breath steady.")
p.source(1, "Woodwinds/Harmonica/Harmonica.sfz")
p.p({"grain.mode": "Cloud", "grain.density": 30, "grain.size": 600, "pos": 40, "pos.spray": 25, "grain.even": 60,
     "grain.sizespray": 30, "voice.latch": True, "pan.spray": 70, "out.width": 130, "amp.vel": 30})
pad_filters(p, 5000, 100)
pad_env(p, 2000, 4000)
detune(p, 6)
pad_motion(p, 0.05, 5, 40)
common_macros(p, grain=25, drift=("Drift", [("Scan", 3, 0)]))
p.macro(5, "Fifths", [("Pitch Spray", 25, 0)])
p.p({"pitch.quant": "Fifths", "pitch.dir": "Up"})

# ---- out ------------------------------------------------------------------------------------------

if __name__ == "__main__":
    names = [x.name for x in PRESETS]
    assert len(names) == len(set(names)), "duplicate names"
    target = os.path.join(HERE, "recipes.json")
    old = {}
    if os.path.exists(target):
        # Keep the levels `fd-presets level` set (out.volume, src gains), unless the recipe
        # sets its own.
        for r in json.load(open(target)).get("presets", []):
            old[r["name"]] = {k: v for k, v in r["params"].items() if k in ("out.volume",)}
    recipes = []
    for x in PRESETS:
        r = x.recipe()
        for k, v in old.get(x.name, {}).items():
            r["params"].setdefault(k, v)
        recipes.append(r)
    with open(target, "w") as f:
        json.dump({"device": "granular", "author": "Segno", "presets": recipes}, f, indent=4, ensure_ascii=False)
        f.write("\n")
    # The catalogue for the brief.
    with open(os.path.join(HERE, "catalogue.md"), "w") as f:
        current = None
        for x in PRESETS:
            if x.category != current:
                current = x.category
                f.write(f"\n### {current}\n\n| Preset | The idea | Its macros |\n|---|---|---|\n")
            macros = ", ".join(f"{n} {x.macros[n][0]}" for n in sorted(x.macros))
            f.write(f"| **{x.name}** | {x.idea} | {macros} |\n")
    print(len(PRESETS), "presets")
