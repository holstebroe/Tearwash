"""Tearwash factory presets (docs/03_PRESETS.md).

Each preset starts from a program as stored (what selecting the program loads) and sets the listed
parameters, in units through the helpers below. Names start with a category code: HL halls, RM
rooms and chambers, PL plates, SP space (long tails and long predelays), EC echo and delay, FX
ensemble and effects. The PG presets (every program as stored) come from the program list itself.

Names may evoke famous records, films and artists, but are invented look-alikes: never a real
title, name or trademark. Run tools/gen_presets.py after editing.
"""
import math

# Units -> stored 0..1 values (the inverse of law:: in src/tearwash/engine/Reverb.hpp).
def sec(s):                     # at most 58 s: the top of DECAY is the hold (input muted),
    s = min(max(s, 0.2), 58.0)  # which belongs to live use, not to a preset that must take input
    return math.log(s / 0.2) / math.log(300.0)
def xo(hz):
    return math.log(max(hz, 80.0) / 80.0) / math.log(125.0)
def hz(f):                     # damping / high-cut corner; OPEN for none
    return math.log(max(f, 1000.0) / 1000.0) / math.log(20.0)
OPEN = 1.0
def ms(t):
    return math.sqrt(max(t, 0.0) / 1000.0)
def size(x):
    return math.log(max(x, 0.5) / 0.5) / math.log(4.0)
def rate(f):
    return math.log(max(f, 0.1) / 0.1) / math.log(50.0)
def pct(p):
    return p / 100.0
SHIMMER_STEPS = [-12, -7, 5, 7, 12, 19, 24]   # the shimmer interval set (law::kShimmerSteps)
def st(semitones):             # SHIMMER INTERVAL, stored
    return SHIMMER_STEPS.index(semitones) / 6.0

# (name, program, {key: value}); values 0..1 from the helpers; mix as a fraction; toggles 0/1.
# ENSEMBLE: decay is the echo feedback (0..1), not a time.
PRESETS = [
    # ---- Halls ------------------------------------------------------------------------------
    ('HL Grand Rotunda', 'GRAND HALL',
     dict(decay=sec(3.2), lowdecay=sec(4.2), crossover=xo(600), damping=hz(7000), predelay=ms(28), size=size(1.2), distance=pct(45), mix=0.30)),
    ('HL Golden Hall Of Music', 'GRAND HALL',
     dict(decay=sec(2.6), lowdecay=sec(3.4), damping=hz(6000), predelay=ms(20), diffusion=pct(38), mix=0.28)),
    ('HL Eighties Snare Hall', 'BRIGHT HALL',
     dict(decay=sec(1.6), damping=hz(12000), predelay=ms(12), distance=pct(20), mix=0.30)),
    ('HL Power Ballad Arena', 'BRIGHT HALL',
     dict(decay=sec(4.5), lowdecay=sec(5.0), predelay=ms(60), size=size(1.5), mix=0.32)),
    ('HL Cathedral Of Stone', 'DARK HALL',
     dict(decay=sec(7.5), lowdecay=sec(9.0), damping=hz(4500), predelay=ms(50), size=size(1.6), mix=0.35)),
    ('HL Midnight Dark Hall', 'DARK HALL',
     dict(decay=sec(4.5), lowdecay=sec(6.0), damping=hz(3500), mix=0.30)),
    ('HL Neon City Hall', 'GRAND HALL',
     dict(decay=sec(5.0), lowdecay=sec(6.5), crossover=xo(400), damping=hz(5000), predelay=ms(70), mix=0.35)),
    ('HL Small Recital Hall', 'GRAND HALL', dict(decay=sec(1.6), size=size(0.7), predelay=ms(15), mix=0.25)),
    ('HL Choir Loft', 'GRAND HALL',
     dict(decay=sec(5.5), lowdecay=sec(6.0), predelay=ms(45), damping=hz(6000), mix=0.35)),
    ('HL Orchestral Stage', 'GRAND HALL', dict(decay=sec(2.8), lowdecay=sec(3.2), size=size(1.3), mix=0.30)),

    # ---- Rooms and chambers -------------------------------------------------------------------
    ('RM Studio Live Room', 'ROOM', dict(decay=sec(0.6), predelay=ms(4), mix=0.22)),
    ('RM Wooden Drum Booth', 'ROOM', dict(decay=sec(0.35), size=size(0.6), damping=hz(6000), mix=0.20)),
    ('RM Stone Cellar', 'CHAMBER', dict(decay=sec(1.4), lowdecay=sec(1.8), damping=hz(5000), mix=0.25)),
    ('RM Echo Chamber 1965', 'CHAMBER', dict(decay=sec(2.0), predelay=ms(15), mix=0.25)),
    ('RM Rich Chamber', 'CHAMBER', dict(decay=sec(1.8), lowdecay=sec(2.4), mix=0.26)),
    ('RM Dark Chamber Vault', 'CHAMBER', dict(decay=sec(2.6), damping=hz(3000), mix=0.28)),
    ('RM Living Room', 'ROOM', dict(decay=sec(0.8), lowdecay=sec(0.9), mix=0.18)),
    ('RM Tiled Bathroom', 'ROOM', dict(decay=sec(1.3), damping=OPEN, size=size(0.6), clarity=pct(60), mix=0.22)),
    ('RM Wide Room', 'ROOM', dict(decay=sec(1.1), size=size(1.4), mix=0.25)),

    # ---- Plates -------------------------------------------------------------------------------
    ('PL Vocal Gold Plate', 'VOCAL PLATE', dict(decay=sec(2.2), predelay=ms(25), mix=0.25)),
    ('PL Detroit Soul Plate', 'PLATE', dict(decay=sec(1.8), damping=hz(14000), predelay=ms(15), mix=0.25)),
    ('PL Snare Plate Eighty-Two', 'PLATE', dict(decay=sec(1.2), damping=OPEN, mix=0.25)),
    ('PL Dense Silk Plate', 'DENSE PLATE', dict(decay=sec(3.0), mix=0.28)),
    ('PL Long Steel Plate', 'PLATE', dict(decay=sec(6.5), lowdecay=sec(6.5), predelay=ms(20), mix=0.30)),
    ('PL Bright Shimmer Plate', 'DENSE PLATE',
     dict(decay=sec(4.0), damping=OPEN, modrate=pct(60), shimmer=pct(40), shimmerint=st(12), mix=0.30)),
    ('PL Rich Plate Bloom', 'PLATE', dict(decay=sec(3.2), lowdecay=sec(4.0), mix=0.28)),
    ('PL Classic Dense Plate', 'DENSE PLATE', dict(decay=sec(2.5), mix=0.28)),
    ('PL Twin Plates Wide', 'VOCAL PLATE', dict(decay=sec(2.0), size=size(1.5), mix=0.28)),
    ('PL Vintage Plate Eight-One', 'PLATE', dict(decay=sec(2.0), damping=hz(9000), mix=0.26)),
    ('PL Halo Sheen Plate', 'DENSE PLATE', dict(decay=sec(3.5), damping=OPEN, shimmer=pct(35), shimmerint=st(12), mix=0.30)),
    ('PL Silver Mist Shimmer', 'PLATE',
     dict(decay=sec(5.0), lowdecay=sec(5.0), predelay=ms(30), shimmer=pct(25), shimmerint=st(19), mix=0.28)),
    ('PL Glass Fifth Plate', 'VOCAL PLATE', dict(decay=sec(4.0), shimmer=pct(20), shimmerint=st(7), mix=0.28)),

    # ---- Space: long tails, long predelays ----------------------------------------------------
    ('SP Teardrops In Neon Rain', 'GRAND HALL',
     dict(decay=sec(20), lowdecay=sec(26), crossover=xo(250), damping=hz(4000), predelay=ms(150), size=size(1.6), mix=0.45)),
    ('SP Synthetic Skyline', 'VAST SPACE',
     dict(decay=sec(18), lowdecay=sec(22), predelay=ms(180), damping=hz(5500), modrate=pct(40), mix=0.45)),
    ('SP Mist Runners On The Shore', 'VAST SPACE',
     dict(decay=sec(12), lowdecay=sec(14), predelay=ms(120), damping=hz(9000), mix=0.42)),
    ('SP Starfield Cathedral', 'VAST SPACE',
     dict(decay=sec(30), lowdecay=sec(34), predelay=ms(250), size=size(1.6), mix=0.48)),
    ('SP Monolith At Jupiter', 'VAST SPACE',
     dict(decay=sec(45), lowdecay=sec(60), crossover=xo(300), damping=hz(3000), predelay=ms(400), mix=0.50)),
    ('SP Oxygenic Drift', 'GRAND HALL',
     dict(decay=sec(9), lowdecay=sec(11), modrate=pct(65), predelay=ms(90), mix=0.42)),
    ('SP Equinox Machines', 'GRAND HALL',
     dict(decay=sec(40), lowdecay=sec(40), predelay=ms(300), damping=hz(7000), mix=0.45)),
    ('SP Music For Terminals', 'DARK HALL',
     dict(decay=sec(14), lowdecay=sec(18), damping=hz(3500), predelay=ms(300), mix=0.50)),
    ('SP Prism Moon Shadow', 'VAST SPACE',
     dict(decay=sec(20), lowdecay=sec(24), predelay=ms(500), distance=pct(90), mix=0.48)),
    ('SP Ocean Of Solarion', 'VAST SPACE',
     dict(decay=sec(25), lowdecay=sec(35), crossover=xo(200), modrate=pct(25), size=size(2.0), mix=0.48)),
    ('SP Five Tone Encounter', 'BRIGHT HALL', dict(decay=sec(24), lowdecay=sec(24), predelay=ms(450), mix=0.40)),
    ('SP Frozen Nebula', 'VAST SPACE', dict(decay=sec(58), lowdecay=sec(58), predelay=ms(0), modrate=pct(15), mix=0.50)),
    ('SP Near Infinity Chamber', 'CHAMBER', dict(decay=sec(50), lowdecay=sec(50), mix=0.50)),
    ('SP Empty Highway Haze', 'GRAND HALL',
     dict(decay=sec(18), lowdecay=sec(22), predelay=ms(280), damping=hz(4000), mix=0.45)),
    ('SP Diamond Dust Shimmer', 'DENSE PLATE',
     dict(decay=sec(16), lowdecay=sec(16), modrate=pct(70), damping=OPEN, predelay=ms(140), shimmer=pct(50), shimmerint=st(12),
          mix=0.45)),
    ('SP Aurora Curtain Wash', 'VAST SPACE',
     dict(decay=sec(10), lowdecay=sec(12), size=size(2.0), modrate=pct(50), predelay=ms(60), mix=0.45)),
    ('SP Deep Space Relay', 'VAST SPACE', dict(decay=sec(60), lowdecay=sec(60), predelay=ms(1000), mix=0.50)),
    ('SP Nebula Long Delay', 'VAST SPACE', dict(decay=sec(8), lowdecay=sec(9), predelay=ms(750), mix=0.45)),
    ('SP Dream Machine Cloud', 'GRAND HALL',
     dict(decay=sec(55), lowdecay=sec(55), predelay=ms(320), size=size(0.8), mix=0.45)),
    ('SP Glacier Hall', 'DARK HALL', dict(decay=sec(20), lowdecay=sec(20), damping=hz(3000), mix=0.45)),
    ('SP Ghost Choir Hall', 'GRAND HALL', dict(decay=sec(11), lowdecay=sec(12), predelay=ms(200), mix=0.42)),
    ('SP Split Horizon Expanse', 'VAST SPACE', dict(decay=sec(22), lowdecay=sec(26), size=size(1.4), mix=0.45)),
    ('SP Orbit Of Saturn Rings', 'VAST SPACE',
     dict(decay=sec(36), lowdecay=sec(40), predelay=ms(320), modrate=pct(55), damping=hz(4500), mix=0.48)),
    ('SP Interplanetary Drone', 'DARK HALL',
     dict(decay=sec(50), lowdecay=sec(60), crossover=xo(150), damping=hz(2500), predelay=ms(600), size=size(2.0), mix=0.50)),
    ('SP Cosmic Lullaby', 'VOCAL PLATE', dict(decay=sec(22), predelay=ms(220), modrate=pct(45), mix=0.42)),
    ('SP Octave Choir Cathedral', 'VAST SPACE',
     dict(decay=sec(22), lowdecay=sec(26), predelay=ms(220), shimmer=pct(45), shimmerint=st(12), mix=0.45)),
    ('SP Aurora Staircase', 'GRAND HALL',
     dict(decay=sec(30), lowdecay=sec(34), predelay=ms(180), damping=hz(6000), shimmer=pct(55), shimmerint=st(19), mix=0.45)),
    ('SP Sunward Fall', 'VAST SPACE',
     dict(decay=sec(40), lowdecay=sec(44), predelay=ms(300), shimmer=pct(40), shimmerint=st(-12), mix=0.45)),

    # ---- Echo and delay -----------------------------------------------------------------------
    ('EC Memphis Slapback', 'ENSEMBLE', dict(decay=0.12, predelay=ms(110), modrate=pct(10), mix=0.30)),
    ('EC Canyon Multitap', 'ENSEMBLE', dict(decay=0.60, predelay=ms(240), modrate=pct(15), mix=0.35)),
    ('EC Chorus Echo Wash', 'ENSEMBLE', dict(decay=0.50, predelay=ms(180), modrate=pct(50), mix=0.40)),
    ('EC Resonant Chord Cave', 'ENSEMBLE', dict(decay=0.62, predelay=ms(30), damping=hz(6000), mix=0.35)),
    ('EC Dub Tape Plume', 'ENSEMBLE', dict(decay=0.70, predelay=ms(375), damping=hz(4000), modrate=pct(30), mix=0.40)),
    ('EC Dotted Eighths Anthem', 'ENSEMBLE', dict(decay=0.55, predelay=ms(375), modrate=pct(20), mix=0.35)),
    ('EC Space Delay Five Hundred', 'VAST SPACE', dict(decay=sec(3.0), lowdecay=sec(3.5), predelay=ms(500), mix=0.40)),
    ('EC Grand Canyon Echo', 'ENSEMBLE', dict(decay=0.65, predelay=ms(500), damping=hz(5000), mix=0.35)),
    ('EC Long Echo Cloud', 'ENSEMBLE', dict(decay=0.68, predelay=ms(620), damping=hz(3000), modrate=pct(35), mix=0.40)),

    # ---- Ensemble and effects -----------------------------------------------------------------
    ('FX Classic Ensemble', 'ENSEMBLE', dict(modrate=pct(40), mix=0.50)),
    ('FX Tight Gate Room', 'ROOM', dict(decay=sec(0.4), size=size(0.6), clarity=pct(80), mix=0.35)),
    ('FX Plate Chorus Wide', 'DENSE PLATE', dict(decay=sec(2.5), modrate=pct(70), mix=0.40)),
    ('FX Lush Ensemble', 'ENSEMBLE', dict(mix=0.50)),
    ('FX Endless Pad Maker', 'PLATE', dict(decay=sec(50), lowdecay=sec(50), modrate=pct(40), mix=0.50)),
    ('FX Underwater Hall', 'GRAND HALL',
     dict(decay=sec(6), damping=hz(1500), highcut=hz(2500), modrate=pct(80), mix=0.40)),
]
