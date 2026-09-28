#!/usr/bin/env python3
"""
Reads the procgen corpus and prints what the universe is made of.

    ./test.sh DeepSpace.Universe.Corpus      # writes Saved/procgen_corpus.tsv
    python3 Tools/procgen_corpus.py          # reads it
    python3 Tools/procgen_corpus.py PATH     # reads another
    ./test.sh Atmosphere.Full.CorpusSkies    # writes Saved/procgen_corpus_skies.tsv, read beside it

The C++ generator is the only generator (ADR 0006, procgen decision 10);
this only reads what it wrote, so changing what the report shows needs no
rebuild. A prior tune is an edit to Config/DefaultGame.ini and a corpus run:
no compile either.

The question it is for is not "are the numbers plausible" -- the
SystemGeneration test holds the invariants -- but the one statistics can
only half answer: do the systems read as *places* or as *rolls*? The last
section tries: how many shapes a system comes in, how often the commonest
repeat, how much of the sky is nothing but rock, and how far home is from
the first thing worth flying to. Read Saved/procgen_describe.txt for the
other half.

The columns are Tools/procgen_corpus_contract.json's, read by name.
"""

import collections
import colorsys
import json
import math
import os
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
CONTRACT_PATH = os.path.join(TOOLS, "procgen_corpus_contract.json")
DEFAULT_TSV = os.path.join(os.path.dirname(TOOLS), "Saved", "procgen_corpus.tsv")
DEFAULT_SKIES = os.path.join(os.path.dirname(TOOLS), "Saved", "procgen_corpus_skies.tsv")

INT_COLUMNS = {"sector_x", "sector_y", "sector_z", "slot", "planet_count", "planet"}
TEXT_COLUMNS = {"system", "star_class", "designation", "given_name", "kind", "air_mix"}
PLANET_COLUMNS = ("designation", "given_name", "kind", "semi_major_axis_au", "mass_earth",
                  "radius_earth", "equilibrium_k", "population", "surface_gravity_g", "relief_km",
                  "air_mix", "surface_pressure_bar", "scale_height_km", "nadir_tau_450")
AIR_MIXES = ("nitrogen-oxygen", "carbon-dioxide", "hydrogen-helium")
SOLID = ("barren", "ice", "terrestrial")

# One letter per kind, for a system's shape written in orbit order.
KIND_LETTER = {"barren": "B", "terrestrial": "T", "ocean": "O", "ice": "I", "gas giant": "G"}
TEMPERATE = {"terrestrial", "ocean"}


def load_contract(path=CONTRACT_PATH):
    with open(path) as f:
        return json.load(f)


def load_corpus(path, contract=None):
    """Every row as a dict of typed values. A planetless system's row has
    planet -1 and None in every planet column. Raises ValueError if the file
    lacks a column the contract names: a report built on a missing column
    would print zeros rather than fail."""
    contract = contract or load_contract()
    with open(path) as f:
        lines = f.read().splitlines()
    if not lines:
        raise ValueError("%s is empty" % path)
    header = lines[0].split("\t")
    missing = [c for c in contract["columns"] if c not in header]
    if missing:
        raise ValueError("%s lacks columns %s" % (path, ", ".join(missing)))

    rows = []
    for number, line in enumerate(lines[1:], start=2):
        if not line:
            continue
        cells = line.split("\t")
        if len(cells) != len(header):
            raise ValueError("%s:%d has %d cells, the header %d" % (path, number, len(cells), len(header)))
        raw = dict(zip(header, cells))
        row = {}
        for name in contract["columns"]:
            text = raw[name]
            if name in INT_COLUMNS:
                row[name] = int(text)
            elif name in TEXT_COLUMNS:
                row[name] = text
            else:
                row[name] = float(text) if text != "" else None
        if row["planet"] < 0:
            for name in PLANET_COLUMNS:
                row[name] = None
        rows.append(row)
    return rows


def load_skies(path, contract=None):
    """The skies file (Atmosphere.Full.CorpusSkies) as a dict keyed by
    (sector_x, sector_y, sector_z, slot, planet), each a dict of the world's
    designation, its noon zenith as three floats and that colour's
    saturation. Raises ValueError if the file lacks a column the contract's
    sky_columns names."""
    contract = contract or load_contract()
    with open(path) as f:
        lines = f.read().splitlines()
    if not lines:
        raise ValueError("%s is empty" % path)
    header = lines[0].split("\t")
    missing = [c for c in contract["sky_columns"] if c not in header]
    if missing:
        raise ValueError("%s lacks columns %s" % (path, ", ".join(missing)))
    skies = {}
    for number, line in enumerate(lines[1:], start=2):
        if not line:
            continue
        cells = line.split("\t")
        if len(cells) != len(header):
            raise ValueError("%s:%d has %d cells, the header %d" % (path, number, len(cells), len(header)))
        raw = dict(zip(header, cells))
        key = tuple(int(raw[c]) for c in ("sector_x", "sector_y", "sector_z", "slot", "planet"))
        skies[key] = {
            "designation": raw["designation"],
            "sky_zenith_rgb": tuple(float(v) for v in raw["sky_zenith_rgb"].split(",")),
            "sky_zenith_saturation": float(raw["sky_zenith_saturation"]),
        }
    return skies


def systems(rows):
    """The rows grouped into systems, in file order (nearest home first).
    Each is {'id', 'name', 'class', 'distance_ly', 'luminosity', 'planets'}
    with planets innermost first. Raises ValueError if a system's rows
    disagree with its own planet_count."""
    by_id = collections.OrderedDict()
    for row in rows:
        key = (row["sector_x"], row["sector_y"], row["sector_z"], row["slot"])
        system = by_id.get(key)
        if system is None:
            system = by_id[key] = {
                "id": key,
                "name": row["system"],
                "class": row["star_class"],
                "distance_ly": row["distance_ly"],
                "luminosity": row["star_luminosity_solar"],
                "planet_count": row["planet_count"],
                "planets": [],
            }
        if row["planet"] >= 0:
            system["planets"].append(row)
    for system in by_id.values():
        system["planets"].sort(key=lambda p: p["planet"])
        if len(system["planets"]) != system["planet_count"]:
            raise ValueError("%s says %d planets and has %d rows" % (
                system["name"], system["planet_count"], len(system["planets"])))
    return list(by_id.values())


def histogram(values, bins, log=False):
    """[(lo, hi, count)]. bins is a count of equal-width bins spanning the
    values -- equal in log10 when log is set -- or a list of ascending edges.
    Every bin is [lo, hi) except the last, which includes hi. With log,
    values <= 0 have no place and are dropped; values outside explicit edges
    are dropped too, so a count that looks short is a range that is."""
    values = [v for v in values if v is not None and (not log or v > 0)]
    if isinstance(bins, int):
        if not values or bins < 1:
            return []
        lo, hi = min(values), max(values)
        if log:
            lo, hi = math.log10(lo), math.log10(hi)
        if hi == lo:
            hi = lo + 1.0
        step = (hi - lo) / bins
        edges = [lo + i * step for i in range(bins)] + [hi]
        if log:
            edges = [10.0 ** e for e in edges]
            # Exact ends, not ten to a rounded logarithm: the extremes must
            # land inside.
            edges[0], edges[-1] = min(values), max(values)
    else:
        edges = list(bins)
    counts = [0] * (len(edges) - 1)
    last = len(counts) - 1
    for v in values:
        if v < edges[0] or v > edges[-1]:
            continue
        # Linear scan: a few dozen bins, and it keeps the edge rule obvious.
        for i in range(len(counts)):
            if v < edges[i + 1] or i == last:
                counts[i] += 1
                break
    return [(edges[i], edges[i + 1], counts[i]) for i in range(len(counts))]


def categorical(values, order=None):
    """[(label, count)] in the given order, then any others by count."""
    counts = collections.Counter(values)
    labels = list(order or [])
    labels += [k for k, _ in counts.most_common() if k not in labels]
    return [(label, counts.get(label, 0)) for label in labels]


def signature(system):
    """A system's shape: its planets' kinds in orbit order, one letter each
    (B barren, T terrestrial, O ocean, I ice, G gas giant), or '-' for none.
    Two systems with one signature differ only in numbers a player reads off
    a chart; the same signature over and over is what a roll looks like."""
    return "".join(KIND_LETTER[p["kind"]] for p in system["planets"]) or "-"


def _bar(count, peak, width=40):
    return "#" * (int(round(width * count / peak)) if peak else 0)


def _render(title, pairs, total, unit=""):
    """pairs are (label, count). Shares are of total."""
    out = [title]
    peak = max((c for _, c in pairs), default=0)
    for label, count in pairs:
        share = 100.0 * count / total if total else 0.0
        out.append("  %-22s %7d %6.1f%%  %s" % (str(label) + unit, count, share, _bar(count, peak)))
    return out


def _range_label(lo, hi):
    return "%.3g - %.3g" % (lo, hi)


def _render_histogram(title, bins, total):
    return _render(title, [(_range_label(lo, hi), c) for lo, hi, c in bins], total)


def report(rows, contract=None, skies=None):
    """The whole report as text. skies, if given, is load_skies' dict."""
    contract = contract or load_contract()
    found = systems(rows)
    planets = [p for s in found for p in s["planets"]]
    n = len(found)
    out = []
    if not n:
        return "The corpus is empty."

    farthest = max(s["distance_ly"] for s in found)
    out.append("%d systems within %.1f ly of home, %d planets (%.2f per system)." % (
        n, farthest, len(planets), len(planets) / n))
    out.append("")

    out += _render("Star classes (share of systems)", categorical([s["class"] for s in found], contract["classes"]), n)
    out.append("")
    out += _render("Planets per system", categorical([len(s["planets"]) for s in found], range(0, 13)), n)
    out.append("")

    kinds = categorical([p["kind"] for p in planets], contract["kinds"])
    out.append("Planet kinds (share of planets; per system)")
    peak = max((c for _, c in kinds), default=0)
    for label, count in kinds:
        out.append("  %-22s %7d %6.1f%%  %5.3f/system  %s" % (
            label, count, 100.0 * count / len(planets) if planets else 0.0, count / n, _bar(count, peak)))
    out.append("")

    out += _render_histogram("Orbit radius, AU (log bins; share of planets)",
                             histogram([p["semi_major_axis_au"] for p in planets], 12, log=True), len(planets))
    out.append("")
    outermost = [s["planets"][-1]["semi_major_axis_au"] for s in found if s["planets"]]
    out += _render_histogram("Outermost orbit, AU (log bins; share of systems with planets)",
                             histogram(outermost, 10, log=True), len(outermost))
    out.append("")
    out += _render_histogram("Mass, Earth masses (log bins; share of planets)",
                             histogram([p["mass_earth"] for p in planets], 12, log=True), len(planets))
    out.append("")
    ratios = [b["semi_major_axis_au"] / a["semi_major_axis_au"]
              for s in found for a, b in zip(s["planets"], s["planets"][1:])]
    out += _render_histogram("Neighbour orbit ratio a(i+1)/a(i) (log bins; share of pairs)",
                             histogram(ratios, 10, log=True), len(ratios))
    if ratios:
        ordered = sorted(ratios)
        out.append("  median %.3f, 10th percentile %.3f, range %.3f - %.3f" % (
            ordered[len(ordered) // 2], ordered[len(ordered) // 10], ordered[0], ordered[-1]))
    out.append("")
    out += _render_histogram("Equilibrium temperature, K (share of planets)",
                             histogram([p["equilibrium_k"] for p in planets], 12), len(planets))
    out.append("")

    inhabited = [s for s in found if any((p["population"] or 0) > 0 for p in s["planets"])]
    temperate = [s for s in found if any(p["kind"] in TEMPERATE for p in s["planets"])]
    people = [p["population"] for p in planets if (p["population"] or 0) > 0]
    out.append("Who lives there")
    out.append("  %d systems with a temperate world (%.1f%%); %d with somebody in them (%.1f%%, one in %s)" % (
        len(temperate), 100.0 * len(temperate) / n, len(inhabited), 100.0 * len(inhabited) / n,
        "%.0f" % (n / len(inhabited)) if inhabited else "none"))
    if people:
        out += _render_histogram("  Settlement size, people (log bins; share of settlements)",
                                 histogram(people, 8, log=True), len(people))
    out.append("")

    out.append("Relief, km, of solid worlds (median, highest, count by kind)")
    for kind in SOLID:
        heights = sorted(p["relief_km"] for p in planets if p["kind"] == kind)
        if heights:
            out.append("  %-12s median %6.2f  highest %6.2f  n=%d" % (
                kind, heights[len(heights) // 2], heights[-1], len(heights)))
        else:
            out.append("  %-12s none" % kind)
    gravity = [p["surface_gravity_g"] for p in planets if p["kind"] in SOLID]
    if gravity:
        out += _render_histogram("Surface gravity of solid worlds, g (share of solid worlds)",
                                 histogram(gravity, 8), len(gravity))
    out.append("")

    airy = [p for p in planets if p["air_mix"] not in (None, "none")]
    out.append("Air, by mix (worlds, median surface pressure, median nadir tau at 450 nm)")
    for mix in AIR_MIXES:
        worlds = [p for p in airy if p["air_mix"] == mix]
        if worlds:
            pressures = sorted(p["surface_pressure_bar"] for p in worlds)
            taus = sorted(p["nadir_tau_450"] for p in worlds)
            out.append("  %-16s n=%-6d median %7.3f bar  tau450 %5.3f" % (
                mix, len(worlds), pressures[len(pressures) // 2], taus[len(taus) // 2]))
        else:
            out.append("  %-16s none" % mix)
    out.append("")

    temperate_worlds = [r for r in rows if r["kind"] in TEMPERATE]
    if skies is None:
        out.append("No skies: ./test.sh Atmosphere.Full.CorpusSkies writes Saved/procgen_corpus_skies.tsv")
    else:
        seen = [skies[k]["sky_zenith_saturation"] for k in
                ((r["sector_x"], r["sector_y"], r["sector_z"], r["slot"], r["planet"]) for r in temperate_worlds)
                if k in skies]
        out.append("  %d of %d temperate worlds have a noon sky" % (len(seen), len(temperate_worlds)))
        if seen:
            out += _render_histogram("Noon zenith saturation of temperate worlds' skies (share of those with one)",
                                     histogram(seen, [0.0, 0.2, 0.4, 0.6, 0.8, 1.0]), len(seen))
            out += sky_colours(temperate_worlds, skies)
    out.append("")

    out += places_or_rolls(found)
    return "\n".join(out)


def zenith_hue(rgb):
    """The hue of a zenith colour in degrees, 0 red, 120 green, 240 blue;
    None for a grey sky, which has none."""
    h, sat, _ = colorsys.rgb_to_hsv(*rgb)
    return None if sat <= 0.0 else 360.0 * h


def _median(values):
    values = sorted(values)
    return values[len(values) // 2]


def sky_colours(temperate_worlds, skies):
    """The zenith colour's spread, which is what the mix weights are judged
    by (atmospheres spec, decision 2): by mix, so a carbon-dioxide sky that
    reads like a nitrogen-oxygen one shows, and as a hue histogram, which
    saturation alone cannot give -- a peach red-dwarf sky and Earth's blue
    land in the same saturation bin."""
    by_mix = collections.OrderedDict((mix, []) for mix in ("none",) + AIR_MIXES)
    for r in temperate_worlds:
        sky = skies.get((r["sector_x"], r["sector_y"], r["sector_z"], r["slot"], r["planet"]))
        if sky is not None:
            by_mix.setdefault(r["air_mix"] or "none", []).append(sky)
    out = ["Noon zenith colour of temperate worlds' skies, by mix (worlds, median RGB, hue range, median saturation)"]
    for mix, seen in by_mix.items():
        if not seen:
            out.append("  %-16s none" % mix)
            continue
        rgb = tuple(_median(s["sky_zenith_rgb"][i] for s in seen) for i in range(3))
        hues = [h for h in (zenith_hue(s["sky_zenith_rgb"]) for s in seen) if h is not None]
        hue = "hue %3.0f-%3.0f deg" % (min(hues), max(hues)) if hues else "hue none (grey)"
        out.append("  %-16s n=%-6d rgb %.2f,%.2f,%.2f  %s  saturation %.2f" % (
            mix, len(seen), rgb[0], rgb[1], rgb[2], hue, _median(s["sky_zenith_saturation"] for s in seen)))
    hues = [h for h in (zenith_hue(s["sky_zenith_rgb"]) for seen in by_mix.values() for s in seen) if h is not None]
    if hues:
        out += _render_histogram("Noon zenith hue of temperate worlds' skies, degrees: 0 red, 60 yellow, 120 green, "
                                 "240 blue (share of those with a hue)",
                                 histogram(hues, [30.0 * i for i in range(13)]), len(hues))
    return out


def places_or_rolls(found):
    """What the numbers can say about the question they cannot settle."""
    n = len(found)
    out = ["Places or rolls?"]

    shapes = collections.Counter(signature(s) for s in found)
    example = {}
    for s in found:
        example.setdefault(signature(s), s["name"])
    top = shapes.most_common(10)
    covered = sum(c for _, c in top)
    out.append("  %d systems come in %d shapes (kinds in orbit order); the commonest ten are %.1f%% of them:" % (
        n, len(shapes), 100.0 * covered / n))
    for shape, count in top:
        out.append("    %-14s %6d %6.1f%%   e.g. %s" % (shape, count, 100.0 * count / n, example[shape]))
    once = sum(1 for c in shapes.values() if c == 1)
    out.append("  %d shapes occur exactly once (%.1f%% of systems are the only one of their shape)." % (
        once, 100.0 * once / n))

    rock = [s for s in found if all(p["kind"] == "barren" for p in s["planets"])]
    out.append("  %d systems (%.1f%%) are nothing but barren rock, or nothing at all." % (len(rock), 100.0 * len(rock) / n))

    def reason(s):
        return any(p["kind"] in TEMPERATE or p["kind"] == "gas giant" or (p["population"] or 0) > 0
                   for p in s["planets"])

    stops = [s for s in found if reason(s)]
    out.append("  %d systems (%.1f%%) have a reason to stop: a temperate world, a gas giant, or somebody." % (
        len(stops), 100.0 * len(stops) / n))

    out.append("  Nearest home (home itself is 0 ly):")
    firsts = (
        ("somebody", lambda s: any((p["population"] or 0) > 0 for p in s["planets"])),
        ("an ocean", lambda s: any(p["kind"] == "ocean" for p in s["planets"])),
        ("a gas giant", lambda s: any(p["kind"] == "gas giant" for p in s["planets"])),
        ("a sun that is not a red dwarf", lambda s: s["class"] != "M"),
    )
    for label, test in firsts:
        hit = min((s for s in found if test(s)), key=lambda s: s["distance_ly"], default=None)
        if hit is None:
            out.append("    %-30s none in the corpus" % label)
        else:
            out.append("    %-30s %-12s %6.2f ly" % (label, hit["name"], hit["distance_ly"]))
    return out


def main(argv):
    path = argv[1] if len(argv) > 1 else DEFAULT_TSV
    if not os.path.exists(path):
        print("No corpus at %s. Write one with: ./test.sh DeepSpace.Universe.Corpus" % path, file=sys.stderr)
        return 1
    skies = load_skies(DEFAULT_SKIES) if len(argv) <= 1 and os.path.exists(DEFAULT_SKIES) else None
    print(report(load_corpus(path), skies=skies))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
