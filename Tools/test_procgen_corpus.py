#!/usr/bin/env python3
"""
Tests for Tools/procgen_corpus.py, against a TSV made by hand.

    python3 Tools/test_procgen_corpus.py

Tools/procgen_corpus_sample.tsv is four systems small enough to count by eye:

    Alpha  M  0 ly     barren 0.01, barren 0.02, terrestrial 0.04 (Hollin, 1000 people)
    Beta   K  5.5 ly   no planets -- one row, planet -1
    Gamma  G  7.25 ly  ocean 1, gas giant 4
    Delta  M  9 ly     barren 0.05

Every expected number below was worked out from that list, not by running
the reader: a test that learned its answers from the code would pass on any
bug the code already had. No editor, no corpus run.
"""

import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import procgen_corpus as C

SAMPLE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "procgen_corpus_sample.tsv")
SKIES_SAMPLE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "procgen_corpus_skies_sample.tsv")


def rows():
    return C.load_corpus(SAMPLE)


def near(a, b):
    return abs(a - b) < 1e-9


def test_the_sample_is_written_to_the_contract():
    with open(SAMPLE) as f:
        header = f.readline().rstrip("\n").split("\t")
    assert header == C.load_contract()["columns"], header


def test_one_row_per_planet_and_one_for_an_empty_system():
    assert len(rows()) == 7


def test_an_empty_system_has_no_planet_values():
    beta = [r for r in rows() if r["system"] == "Beta"]
    assert len(beta) == 1
    assert beta[0]["planet"] == -1
    assert beta[0]["kind"] is None and beta[0]["semi_major_axis_au"] is None and beta[0]["population"] is None
    assert beta[0]["star_class"] == "K" and near(beta[0]["distance_ly"], 5.5)


def test_values_are_typed():
    alpha3 = [r for r in rows() if r["designation"] == "Alpha III"][0]
    assert alpha3["planet"] == 2 and isinstance(alpha3["planet"], int)
    assert alpha3["given_name"] == "Hollin"
    assert near(alpha3["population"], 1000.0)
    assert [r for r in rows() if r["system"] == "Delta"][0]["sector_x"] == -1


def test_a_missing_column_is_refused():
    with open(SAMPLE) as f:
        lines = f.read().splitlines()
    cut = [line.split("\t") for line in lines]
    index = cut[0].index("equilibrium_k")
    trimmed = "\n".join("\t".join(c[:index] + c[index + 1:]) for c in cut) + "\n"
    with tempfile.NamedTemporaryFile("w", suffix=".tsv", delete=False) as f:
        f.write(trimmed)
    try:
        C.load_corpus(f.name)
    except ValueError as e:
        assert "equilibrium_k" in str(e)
    else:
        raise AssertionError("a corpus without equilibrium_k was read")
    finally:
        os.unlink(f.name)


def test_systems_group_in_file_order_with_their_planets():
    found = C.systems(rows())
    assert [s["name"] for s in found] == ["Alpha", "Beta", "Gamma", "Delta"]
    assert [len(s["planets"]) for s in found] == [3, 0, 2, 1]
    assert [p["designation"] for p in found[0]["planets"]] == ["Alpha I", "Alpha II", "Alpha III"]


def test_a_system_short_of_its_planet_count_is_refused():
    short = [r for r in rows() if r["designation"] != "Gamma II"]
    try:
        C.systems(short)
    except ValueError as e:
        assert "Gamma" in str(e)
    else:
        raise AssertionError("Gamma says 2 planets and one was accepted")


def test_signatures_are_kinds_in_orbit_order():
    assert [C.signature(s) for s in C.systems(rows())] == ["BBT", "-", "OG", "B"]


def test_histogram_equal_width():
    assert C.histogram([1, 2, 3, 4], 2) == [(1, 2.5, 2), (2.5, 4, 2)]


def test_histogram_last_bin_includes_its_top_edge():
    assert C.histogram([0, 5, 10], [0, 5, 10]) == [(0, 5, 1), (5, 10, 2)]


def test_histogram_drops_what_explicit_edges_do_not_cover():
    assert [c for _, _, c in C.histogram([-1, 0, 1, 2, 99], [0, 1, 2])] == [1, 2]


def test_histogram_log_bins_are_decades():
    bins = C.histogram([1, 10, 100, 1000], 3, log=True)
    assert [c for _, _, c in bins] == [1, 1, 2]
    assert near(bins[1][0], 10) and near(bins[1][1], 100)


def test_histogram_log_drops_zero_and_below():
    assert sum(c for _, _, c in C.histogram([0, -3, 1, 10], 1, log=True)) == 2


def test_histogram_of_one_value():
    assert C.histogram([3, 3], 4)[0][2] == 2


def test_report_counts_the_sample():
    text = C.report(rows())
    assert "4 systems within 9.0 ly of home, 6 planets (1.50 per system)." in text, text
    # Classes: M twice, K and G once each.
    assert "  M                            2   50.0%" in text, text
    assert "  K                            1   25.0%" in text, text
    # Planets per system 0, 1, 2 and 3, once each.
    for count in range(4):
        assert "  %-22s %7d %6.1f%%" % (count, 1, 25.0) in text, count
    # Kinds of six planets: three barren, one each of the rest but ice.
    assert "  barren                       3   50.0%  0.750/system" in text, text
    assert "  ice                          0    0.0%  0.000/system" in text, text
    # Temperate: Alpha (terrestrial) and Gamma (ocean). Somebody: Alpha only.
    assert "2 systems with a temperate world (50.0%); 1 with somebody in them (25.0%, one in 4)" in text, text
    # Neighbour ratios 2, 2, 4.
    assert "median 2.000, 10th percentile 2.000, range 2.000 - 4.000" in text, text


def test_report_answers_places_or_rolls_on_the_sample():
    text = C.report(rows())
    assert "4 systems come in 4 shapes" in text, text
    assert "4 shapes occur exactly once" in text, text
    # Beta has nothing and Delta only rock: two of four.
    assert "2 systems (50.0%) are nothing but barren rock, or nothing at all." in text, text
    # Alpha (a temperate world, somebody) and Gamma (an ocean, a giant).
    assert "2 systems (50.0%) have a reason to stop" in text, text
    nearest = text.split("Nearest home")[1]
    lines = {line[4:34].strip(): line[34:].split() for line in nearest.splitlines()[1:]}
    assert lines["somebody"] == ["Alpha", "0.00", "ly"], lines
    assert lines["an ocean"] == ["Gamma", "7.25", "ly"], lines
    assert lines["a gas giant"] == ["Gamma", "7.25", "ly"], lines
    assert lines["a sun that is not a red dwarf"] == ["Beta", "5.50", "ly"], lines


def test_report_gives_relief_by_kind():
    text = C.report(rows())
    assert "Relief, km, of solid worlds (median, highest, count by kind)" in text, text
    # Barren: Alpha I 7.5, Alpha II 6.0, Delta I 4.2 -> median 6.00, highest 7.50.
    assert "  barren       median   6.00  highest   7.50  n=3" in text, text
    assert "  ice          none" in text, text
    assert "  terrestrial  median   3.80  highest   3.80  n=1" in text, text


def test_gravity_and_relief_are_typed():
    alpha1 = [r for r in rows() if r["designation"] == "Alpha I"][0]
    assert near(alpha1["surface_gravity_g"], 0.78125)
    assert near(alpha1["relief_km"], 7.5)
    beta = [r for r in rows() if r["system"] == "Beta"][0]
    assert beta["relief_km"] is None and beta["surface_gravity_g"] is None


def test_report_gives_air_by_mix():
    text = C.report(rows())
    assert "Air, by mix (worlds, median surface pressure, median nadir tau at 450 nm)" in text, text
    # Alpha III n2/o2 1 bar 0.277; Gamma I co2 0.8 bar 0.34; Gamma II h2/he 0.474 bar 0.32
    # (a 0.826 g giant's disc under AirFacts: 0.32 / NadirTau450(H2/He, 1 bar, 1 g) x g,
    # MaxNadirTau450 as atmosphere plan ruling 2 lowered it).
    assert "  nitrogen-oxygen  n=1      median   1.000 bar  tau450 0.277" in text, text
    assert "  carbon-dioxide   n=1      median   0.800 bar  tau450 0.340" in text, text
    assert "  hydrogen-helium  n=1      median   0.474 bar  tau450 0.320" in text, text


def test_air_is_typed():
    alpha3 = [r for r in rows() if r["designation"] == "Alpha III"][0]
    assert alpha3["air_mix"] == "nitrogen-oxygen"
    assert near(alpha3["surface_pressure_bar"], 1.0) and near(alpha3["scale_height_km"], 8.78)
    alpha1 = [r for r in rows() if r["designation"] == "Alpha I"][0]
    assert alpha1["air_mix"] == "none" and near(alpha1["surface_pressure_bar"], 0.0)
    giant = [r for r in rows() if r["designation"] == "Gamma II"][0]
    assert giant["air_mix"] == "hydrogen-helium" and near(giant["nadir_tau_450"], 0.32)
    # The row is a worked example of the law, not only reader input: at
    # 0.826 g and 140 K its disc is 0.474 bar and its scale height 62.4 km.
    assert near(giant["surface_pressure_bar"], 0.474) and near(giant["scale_height_km"], 62.4)
    beta = [r for r in rows() if r["system"] == "Beta"][0]
    assert beta["air_mix"] is None and beta["surface_pressure_bar"] is None


def test_the_skies_sample_is_written_to_the_contract():
    with open(SKIES_SAMPLE) as f:
        header = f.readline().rstrip("\n").split("\t")
    assert header == C.load_contract()["sky_columns"], header


def test_skies_are_typed_and_keyed_by_world():
    skies = C.load_skies(SKIES_SAMPLE)
    assert len(skies) == 2
    alpha3 = skies[(0, 0, 0, 0, 2)]
    assert alpha3["designation"] == "Alpha III"
    assert all(near(a, b) for a, b in zip(alpha3["sky_zenith_rgb"], (0.21, 0.34, 0.62)))
    assert near(alpha3["sky_zenith_saturation"], 0.66129)
    assert near(skies[(0, 1, 0, 1, 0)]["sky_zenith_saturation"], 0.175)


def test_report_gives_the_spread_of_skies():
    text = C.report(rows(), skies=C.load_skies(SKIES_SAMPLE))
    assert "  2 of 2 temperate worlds have a noon sky" in text, text
    assert "Noon zenith saturation of temperate worlds' skies (share of those with one)" in text, text


def test_report_without_skies_says_how_to_write_them():
    text = C.report(rows())
    assert "No skies: ./test.sh Atmosphere.Full.CorpusSkies writes Saved/procgen_corpus_skies.tsv" in text, text


def test_a_skies_file_missing_a_column_is_refused():
    with open(SKIES_SAMPLE) as f:
        lines = f.read().splitlines()
    cut = [line.split("\t") for line in lines]
    index = cut[0].index("sky_zenith_saturation")
    trimmed = "\n".join("\t".join(c[:index] + c[index + 1:]) for c in cut) + "\n"
    with tempfile.NamedTemporaryFile("w", suffix=".tsv", delete=False) as f:
        f.write(trimmed)
    try:
        C.load_skies(f.name)
    except ValueError as e:
        assert "sky_zenith_saturation" in str(e)
    else:
        raise AssertionError("a skies file without sky_zenith_saturation was read")
    finally:
        os.unlink(f.name)


def main():
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
