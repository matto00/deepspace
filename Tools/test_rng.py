#!/usr/bin/env python3
"""
Tests for the RNG mirror, and the writer of the vectors it shares with C++.

    python3 Tools/test_rng.py            # check
    python3 Tools/test_rng.py --write    # regenerate Tools/rng_vectors.json

Two different claims are checked, and they must not be confused:

- **The mirror against the outside world.** KNOWN is procgen's known-value
  table typed in from the spec, not computed. The stream's first three outputs
  from seed 0 are SplitMix64's published reference sequence. Nothing here
  generates those numbers, so a change to the hash or the stream fails here
  even if the JSON was regenerated with it.
- **The file against the mirror.** Every row of rng_vectors.json reproduces
  exactly, doubles compared as float.hex. DeepSpace.Universe.Stream reads the
  same file, which is the only thing standing between FGenStream and this
  mirror drifting apart.

--write is for a deliberate change to the contract, and a deliberate change to
the contract re-rolls every universe. The KNOWN table does not move with it.
"""

import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rng as R

VECTORS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "rng_vectors.json")

INT64_MIN = -(1 << 63)
INT64_MAX = (1 << 63) - 1

# Procgen's known-value table, verbatim from
# docs/superpowers/specs/2026-09-25-procgen-foundation-design.md.
KNOWN = {
    "Mix(0)": 0xE220A8397B1DCDAF,
    "Mix(1)": 0x910A2DEC89025CC1,
    'Label("star")': 0xAEFD58191D95E091,
    'Derive(1, Label("system"), 0)': 0x07389B5FDEDF9306,
    "HashCoord((1,0,0))": 0xB18A02F46D8D86C3,
    "HashCoord((0,1,0))": 0x44E5B98100C67FB0,
    "HashCoord((-1,0,0))": 0xFC042709560421DA,
    "HashCoord((0,0,0))": 0x238275BC38FCBE91,
    "galaxy": 0x499B04106B52A25F,
    "sector(0,0,0)": 0xABDDDC1BF5D4C7FB,
    "sector(0,0,0) slot 0": 0x4F6049C72F9DF12D,
}
KNOWN_STREAM_0 = [0xE220A8397B1DCDAF, 0x6E789E6AA1B965F4, 0x06C45D188009454F]
KNOWN_UNIT_0 = 0.88331080821364261
KNOWN_COUNTS = {(0, 0, 0): 0, (1, 0, 0): 2}
ROOT = 20260925

SAMPLER_SEED_LABEL = "vectors"
SAMPLER_DRAWS = 32

STAR_CLASS_WEIGHTS = [0.76, 0.12, 0.076, 0.030, 0.006, 0.0013]

# (C++ method, arguments, what it returns). Each section is a fresh stream on
# the same seed, so a section can be added or dropped without moving another.
SAMPLERS = [
    ("Unit", [], "double"),
    ("UnitOpen", [], "double"),
    ("UniformInt", [-3, 9], "int64"),
    # A span just over 2^62 rejects about one draw in four, so Lemire's
    # rejection loop is actually walked rather than merely compiled.
    ("UniformInt", [0, (1 << 62) + 12345], "int64"),
    ("UniformInt", [INT64_MIN, INT64_MAX], "int64"),
    ("Chance", [0.3], "bool"),
    ("Poisson", [0.5, 8], "int"),
    ("Poisson", [2.5, 12], "int"),
    ("Poisson", [4.0, 12], "int"),
    ("Poisson", [25.0, 60], "int"),
    ("Poisson", [6.0, 3], "int"),                     # the cap bites
    ("Normal", [0.0, 1.0], "double"),
    ("Normal", [10.0, 3.0], "double"),
    ("LogNormal", [20.0, 0.4], "double"),
    ("LogNormalBounded", [20.0, 0.4, 10.0, 60.0], "double"),
    ("LogNormalBounded", [1.0, 0.9, 0.05, 14.9], "double"),
    ("LogNormalBounded", [1.0, 2.0, 0.999, 1.001], "double"),   # the clamp fires
    ("Exponential", [1.0], "double"),
    ("Exponential", [0.18], "double"),
    ("ParetoBounded", [1.5, 1.0, 400.0], "double"),
    ("Gamma", [0.5], "double"),
    ("Gamma", [1.0], "double"),
    ("Gamma", [1.2], "double"),
    ("Gamma", [5.0], "double"),
    ("Beta", [2.0, 4.0], "double"),
    ("Beta", [3.0, 3.0], "double"),
    ("Beta", [4.0, 2.0], "double"),
    ("Beta", [1.0, 3.0], "double"),
    ("Beta", [1.2, 2.0], "double"),
    ("Beta", [0.867, 3.03], "double"),
    ("Categorical", [[4.0, 3.0, 3.0, 2.0, 1.0]], "int"),
    ("Categorical", [STAR_CLASS_WEIGHTS], "int"),
    ("Categorical", [[0.0, 1.0, 0.0, 2.0]], "int"),
]

# Which arguments are integers, by C++ signature. Everything else is a double.
INT_ARGS = {"UniformInt": (0, 1), "Poisson": (1,)}

PY_NAME = {
    "Unit": "unit", "UnitOpen": "unit_open", "UniformInt": "uniform_int",
    "Chance": "chance", "Poisson": "poisson", "Normal": "normal",
    "LogNormal": "log_normal", "LogNormalBounded": "log_normal_bounded",
    "Exponential": "exponential", "ParetoBounded": "pareto_bounded",
    "Gamma": "gamma", "Beta": "beta", "Categorical": "categorical",
}


def hex64(v):
    return "0x%016X" % (v & R.MASK)


# -- encoding ---------------------------------------------------------------
# JSON numbers are doubles, which hold neither a uint64 nor a double's exact
# bits reliably through every parser. So: uint64 as 0x hex strings, int64 as
# decimal strings, doubles as float.hex strings (std::strtod reads them), and
# only small integers and bools as bare JSON.

def encode_arg(sampler, i, value):
    if isinstance(value, list):
        return [float(w).hex() for w in value]
    if i in INT_ARGS.get(sampler, ()):
        return str(value) if sampler == "UniformInt" else value
    return float(value).hex()


def decode_arg(sampler, i, value):
    if isinstance(value, list):
        return [float.fromhex(w) for w in value]
    if i in INT_ARGS.get(sampler, ()):
        return int(value)
    return float.fromhex(value)


def encode_value(kind, value):
    return {"double": lambda v: v.hex(), "int64": str, "int": int, "bool": bool}[kind](value)


def draw(sampler, args, seed, count):
    stream = R.Stream(seed)
    fn = getattr(stream, PY_NAME[sampler])
    return [fn(*args) for _ in range(count)]


def build_vectors():
    sampler_seed = R.derive(1, R.label(SAMPLER_SEED_LABEL))
    galaxy = R.derive(ROOT, R.label("galaxy"))
    sectors = []
    for coord in [(0, 0, 0), (1, 0, 0)]:
        sector = R.derive(galaxy, R.label("sector"), R.hash_coord(*coord))
        count_seed = R.derive(sector, R.label("count"))
        sectors.append({
            "coord": list(coord),
            "seed": hex64(sector),
            "count_seed": hex64(count_seed),
            "count_mean": (0.5).hex(),
            "count_max": 8,
            "count": R.Stream(count_seed).poisson(0.5, 8),
            "slot0": hex64(R.derive(sector, R.label("system"), 0)),
        })

    def seed_row(expr, fn, args, value):
        return {"expr": expr, "fn": fn, "args": args, "value": hex64(value)}

    return {
        "_comment": (
            "Shared by Tools/test_rng.py and DeepSpace.Universe.Stream; see "
            "GenStream.h for the contract. Regenerate only with "
            "`python3 Tools/test_rng.py --write`, and only for a deliberate "
            "change, which re-rolls every universe. Encoding: uint64 as 0x hex "
            "strings, int64 as decimal strings, doubles as float.hex strings "
            "(std::strtod parses them), small ints and bools bare. Every "
            "sampler section is a fresh FGenStream on its own 'seed'."),
        "seed": [
            seed_row("Mix(0)", "Mix", [hex64(0)], R.mix(0)),
            seed_row("Mix(1)", "Mix", [hex64(1)], R.mix(1)),
            seed_row('Label("star")', "Label", ["star"], R.label("star")),
            seed_row('Label("galaxy")', "Label", ["galaxy"], R.label("galaxy")),
            seed_row('Label("sky.starfield")', "Label", ["sky.starfield"], R.label("sky.starfield")),
            seed_row('Derive(1, Label("system"), 0)', "Derive",
                     [hex64(1), hex64(R.label("system")), hex64(0)],
                     R.derive(1, R.label("system"), 0)),
            seed_row("HashCoord((1,0,0))", "HashCoord", [1, 0, 0], R.hash_coord(1, 0, 0)),
            seed_row("HashCoord((0,1,0))", "HashCoord", [0, 1, 0], R.hash_coord(0, 1, 0)),
            seed_row("HashCoord((-1,0,0))", "HashCoord", [-1, 0, 0], R.hash_coord(-1, 0, 0)),
            seed_row("HashCoord((0,0,0))", "HashCoord", [0, 0, 0], R.hash_coord(0, 0, 0)),
        ],
        "chain": {
            "root": hex64(ROOT),
            "galaxy": hex64(galaxy),
            "sectors": sectors,
        },
        "stream": [
            {"seed": hex64(seed), "next_u64": [hex64(v) for v in draw_u64(seed, 16)]}
            for seed in (0, 1, 0xDEADBEEF)
        ],
        "unit": {"seed": hex64(0), "value": R.Stream(0).unit().hex()},
        "samplers": [
            {
                "name": "%s(%s)" % (sampler, ", ".join(describe(a) for a in args)),
                "sampler": sampler,
                "args": [encode_arg(sampler, i, a) for i, a in enumerate(args)],
                "returns": kind,
                "seed": hex64(sampler_seed),
                "values": [encode_value(kind, v) for v in draw(sampler, args, sampler_seed, SAMPLER_DRAWS)],
            }
            for sampler, args, kind in SAMPLERS
        ],
    }


def describe(a):
    if isinstance(a, list):
        return "[" + ", ".join(repr(w) for w in a) + "]"
    return repr(a)


def draw_u64(seed, count):
    stream = R.Stream(seed)
    return [stream.next_u64() for _ in range(count)]


def load():
    with open(VECTORS) as f:
        return json.load(f)


# -- the mirror against the outside world -----------------------------------

def test_hash_rows_match_the_spec():
    assert R.mix(0) == KNOWN["Mix(0)"]
    assert R.mix(1) == KNOWN["Mix(1)"]
    assert R.label("star") == KNOWN['Label("star")']
    assert R.derive(1, R.label("system"), 0) == KNOWN['Derive(1, Label("system"), 0)']
    assert R.hash_coord(1, 0, 0) == KNOWN["HashCoord((1,0,0))"]
    assert R.hash_coord(0, 1, 0) == KNOWN["HashCoord((0,1,0))"]
    assert R.hash_coord(-1, 0, 0) == KNOWN["HashCoord((-1,0,0))"]
    assert R.hash_coord(0, 0, 0) == KNOWN["HashCoord((0,0,0))"]


def test_stream_matches_splitmix64s_published_sequence():
    assert draw_u64(0, 3) == KNOWN_STREAM_0


def test_stream_output_from_state_s_is_mix_of_s():
    for seed in (0, 1, 0xDEADBEEF, R.MASK):
        assert R.Stream(seed).next_u64() == R.mix(seed)


def test_first_unit_is_the_top_53_bits_exactly():
    u = R.Stream(0).unit()
    assert u == KNOWN_UNIT_0
    assert u == (0xE220A8397B1DCDAF >> 11) * 2.0 ** -53


def test_chain_from_root_to_slot_matches_the_spec():
    galaxy = R.derive(ROOT, R.label("galaxy"))
    assert galaxy == KNOWN["galaxy"]
    sector = R.derive(galaxy, R.label("sector"), R.hash_coord(0, 0, 0))
    assert sector == KNOWN["sector(0,0,0)"]
    assert R.derive(sector, R.label("system"), 0) == KNOWN["sector(0,0,0) slot 0"]


def test_sector_counts_match_the_spec():
    galaxy = R.derive(ROOT, R.label("galaxy"))
    for coord, expected in KNOWN_COUNTS.items():
        sector = R.derive(galaxy, R.label("sector"), R.hash_coord(*coord))
        count = R.Stream(R.derive(sector, R.label("count"))).poisson(0.5, 8)
        assert count == expected, (coord, count)


# -- the file against the mirror --------------------------------------------

def test_vectors_file_carries_the_spec_table():
    # The file is written by the mirror, so on its own it proves only that the
    # mirror agrees with itself. These rows tie it to the spec as well.
    v = load()
    seed = {row["expr"]: int(row["value"], 16) for row in v["seed"]}
    for expr in ("Mix(0)", "Mix(1)", 'Label("star")', 'Derive(1, Label("system"), 0)',
                 "HashCoord((1,0,0))", "HashCoord((0,1,0))", "HashCoord((-1,0,0))",
                 "HashCoord((0,0,0))"):
        assert seed[expr] == KNOWN[expr], expr
    chain = v["chain"]
    assert int(chain["root"], 16) == ROOT
    assert int(chain["galaxy"], 16) == KNOWN["galaxy"]
    origin, = [s for s in chain["sectors"] if s["coord"] == [0, 0, 0]]
    assert int(origin["seed"], 16) == KNOWN["sector(0,0,0)"]
    assert int(origin["slot0"], 16) == KNOWN["sector(0,0,0) slot 0"]
    for s in chain["sectors"]:
        assert s["count"] == KNOWN_COUNTS[tuple(s["coord"])]
    zero, = [s for s in v["stream"] if int(s["seed"], 16) == 0]
    assert [int(x, 16) for x in zero["next_u64"][:3]] == KNOWN_STREAM_0
    assert float.fromhex(v["unit"]["value"]) == KNOWN_UNIT_0


def test_seed_rows_reproduce():
    fns = {
        "Mix": lambda a: R.mix(int(a[0], 16)),
        "Label": lambda a: R.label(a[0]),
        "Derive": lambda a: R.derive(*(int(x, 16) for x in a)),
        "HashCoord": lambda a: R.hash_coord(*a),
    }
    for row in load()["seed"]:
        assert fns[row["fn"]](row["args"]) == int(row["value"], 16), row["expr"]


def test_chain_rows_reproduce():
    chain = load()["chain"]
    galaxy = R.derive(int(chain["root"], 16), R.label("galaxy"))
    assert galaxy == int(chain["galaxy"], 16)
    for s in chain["sectors"]:
        sector = R.derive(galaxy, R.label("sector"), R.hash_coord(*s["coord"]))
        assert sector == int(s["seed"], 16), s["coord"]
        count_seed = R.derive(sector, R.label("count"))
        assert count_seed == int(s["count_seed"], 16), s["coord"]
        count = R.Stream(count_seed).poisson(float.fromhex(s["count_mean"]), s["count_max"])
        assert count == s["count"], s["coord"]
        assert R.derive(sector, R.label("system"), 0) == int(s["slot0"], 16), s["coord"]


def test_stream_rows_reproduce():
    rows = load()["stream"]
    assert sorted(int(r["seed"], 16) for r in rows) == [0, 1, 0xDEADBEEF]
    for row in rows:
        seed = int(row["seed"], 16)
        assert len(row["next_u64"]) == 16
        assert draw_u64(seed, 16) == [int(x, 16) for x in row["next_u64"]], hex(seed)


def test_sampler_rows_reproduce_to_the_bit():
    rows = load()["samplers"]
    assert [(r["sampler"], r["returns"]) for r in rows] == [(s, k) for s, _, k in SAMPLERS]
    for row in rows:
        sampler = row["sampler"]
        args = [decode_arg(sampler, i, a) for i, a in enumerate(row["args"])]
        got = [encode_value(row["returns"], x)
               for x in draw(sampler, args, int(row["seed"], 16), len(row["values"]))]
        assert len(got) == SAMPLER_DRAWS, row["name"]
        for i, (g, e) in enumerate(zip(got, row["values"])):
            assert g == e, "%s draw %d: got %s, file has %s" % (row["name"], i, g, e)


def test_sampler_seed_is_derived_from_the_vectors_label():
    expected = hex64(R.derive(1, R.label(SAMPLER_SEED_LABEL)))
    assert all(r["seed"] == expected for r in load()["samplers"])


# -- the contract's edges ---------------------------------------------------

class Fixed(R.Stream):
    """A stream that returns the given words, for probing a sampler's edges."""

    def __init__(self, words):
        super().__init__(0)
        self.words = list(words)

    def next_u64(self):
        return self.words.pop(0)


def test_unit_open_is_open_at_both_ends():
    assert 0.0 < Fixed([0]).unit_open() < 1.0
    assert 0.0 < Fixed([R.MASK]).unit_open() < 1.0
    # The spec's first form, 53 bits plus a half, rounds its top value up to
    # exactly 1.0 -- which is why the contract keeps 52.
    assert ((R.MASK >> 11) + 0.5) * 2.0 ** -53 == 1.0


def test_unit_is_half_open():
    assert Fixed([0]).unit() == 0.0
    assert Fixed([R.MASK]).unit() < 1.0


def test_uniform_int_stays_in_range_and_walks_its_rejection():
    s = R.Stream(7)
    assert all(-3 <= s.uniform_int(-3, 9) <= 9 for _ in range(2000))
    assert R.Stream(7).uniform_int(5, 5) == 5
    # Low word under the threshold forces a second draw.
    span = (1 << 62) + 12345
    first = 0                      # low word 0, under the threshold: rejected
    second = R.MASK                # low word 2^64 - span, over it: accepted
    fixed = Fixed([first, second])
    assert fixed.uniform_int(0, span - 1) == span - 1
    assert fixed.words == []


def test_chance_always_draws_one_word():
    for p in (-1.0, 0.0, 0.5, 1.0, 2.0):
        s = R.Stream(3)
        s.chance(p)
        assert s.state == (3 + R.GOLDEN) & R.MASK, p


def test_poisson_stops_at_its_max_and_refuses_a_large_mean():
    s = R.Stream(11)
    assert all(s.poisson(20.0, 5) <= 5 for _ in range(500))
    try:
        R.Stream(1).poisson(30.5, 100)
    except ValueError:
        return
    raise AssertionError("expected ValueError for a mean over 30")


def test_poisson_mean_and_variance_agree():
    s = R.Stream(12)
    xs = [s.poisson(2.5, 30) for _ in range(20000)]
    mean = sum(xs) / len(xs)
    var = sum((x - mean) ** 2 for x in xs) / len(xs)
    assert abs(mean - 2.5) < 0.05 and abs(var - 2.5) < 0.1, (mean, var)


def test_log_normal_bounded_truncates_rather_than_clamps():
    s = R.Stream(13)
    xs = [s.log_normal_bounded(20.0, 0.4, 10.0, 60.0) for _ in range(20000)]
    assert all(10.0 <= x <= 60.0 for x in xs)
    assert sum(1 for x in xs if x in (10.0, 60.0)) == 0
    xs.sort()
    assert abs(xs[len(xs) // 2] / 20.0 - 1.0) < 0.02


def test_log_normal_bounded_clamps_only_after_sixteen_misses():
    row, = [r for r in load()["samplers"] if r["name"].startswith("LogNormalBounded(1.0, 2.0")]
    values = [float.fromhex(v) for v in row["values"]]
    assert any(v in (0.999, 1.001) for v in values)
    assert all(0.999 <= v <= 1.001 for v in values)


def test_beta_means():
    for a, b in ((2.0, 4.0), (0.867, 3.03), (1.2, 2.0)):
        s = R.Stream(14)
        xs = [s.beta(a, b) for _ in range(20000)]
        assert all(0.0 <= x <= 1.0 for x in xs)
        assert abs(sum(xs) / len(xs) - a / (a + b)) < 0.01, (a, b)


def test_gamma_mean_below_and_above_one():
    for shape in (0.5, 1.2, 5.0):
        s = R.Stream(15)
        xs = [s.gamma(shape) for _ in range(20000)]
        assert abs(sum(xs) / len(xs) / shape - 1.0) < 0.03, shape


def test_pareto_bounded_is_steep_and_bounded():
    s = R.Stream(16)
    xs = sorted(s.pareto_bounded(1.5, 1.0, 400.0) for _ in range(20000))
    assert xs[0] >= 1.0 and xs[-1] <= 400.0
    # alpha = 1.5 puts the median at 2^(1/1.5), about 1.59; uniform would be ~200.
    assert abs(xs[len(xs) // 2] - 2.0 ** (1 / 1.5)) < 0.05


def test_categorical_follows_weights_and_never_picks_a_zero():
    s = R.Stream(17)
    counts = [0, 0, 0, 0]
    for _ in range(20000):
        counts[s.categorical([0.0, 1.0, 0.0, 3.0])] += 1
    assert counts[0] == 0 and counts[2] == 0
    assert abs(counts[3] / 20000 - 0.75) < 0.015


def main():
    if "--write" in sys.argv[1:]:
        with open(VECTORS, "w") as f:
            json.dump(build_vectors(), f, indent=1)
            f.write("\n")
        print("wrote " + VECTORS)
    tests = [(n, f) for n, f in sorted(globals().items()) if n.startswith("test_")]
    failed = 0
    for name, fn in tests:
        try:
            fn()
            print("  ok    " + name)
        except Exception as e:                     # noqa: BLE001
            failed += 1
            print("  FAIL  %s: %s: %s" % (name, type(e).__name__, e))
    print("\n%d passed, %d failed" % (len(tests) - failed, failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
