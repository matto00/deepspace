"""The cast shadow's GPU gate: for each Eyes.LandingFrame case, the frame with
the term (on_ms) against the same case in a run taken before the term existed
(the baseline's on_ms). On minus off is printed beside it and is not the gate:
with ds.Sky.Shadows 0 the pixel still runs the larger shader. Exit 0 GO,
1 NO-GO, 2 undecided.

The noise the cost is read against is the error of the two MEDIANS, not the
spread of single frames: each median's standard error is 1.2533 x sigma /
sqrt(n), sigma robustly 1.4826 x the median absolute deviation of the raw
times (the report's times_on), and the cost's is the two combined. A cost
within twice that of the budget -- never less than RUN_TO_RUN_MS, the most two
baselines taken with no term at all have differed by -- is too close to call,
and a longer run (more rounds) narrows it. The per-frame IQR (spread_ms) is
printed, and read only for a report without raw times. The review found the
first rule, 2 x the IQR, wider than the budget: a free term came back
UNDECIDED however often it was repeated.

With --not-rise (the baked cast shadow, whose ruling is that it costs nothing
per frame) there is no budget: GO only if no case's cost is over its band, a
case faster than its baseline included; NO-GO if one is.

    python3 Tools/landing_frame_gate.py --not-rise Saved/Eyes/LandingFrame/baseline Saved/Eyes/LandingFrame/shadows-baked

Every case in CASES must be in both runs: a report with none of them (a
misspelt EYES_CASES, a changed line) is UNDECIDED, never GO. A run reporting
`switch absent` found no ds.Sky.Shadows: its times still price whatever the
shaders draw (the spike writes no switch), but its on-off and the switch's
proof are nothing, and the tool says so. The null check is two baselines,
which must read GO.

    python3 Tools/landing_frame_gate.py Saved/Eyes/LandingFrame/baseline Saved/Eyes/LandingFrame/shadows [1.0]
    python3 Tools/landing_frame_gate.py Saved/Eyes/LandingFrame/baseline Saved/Eyes/LandingFrame/baseline-2
"""
import math
import os
import statistics
import sys

CASES = ("50km", "1.5m", "50km_dusk10", "1.5m_dusk3", "200km_dusk10", "1.5m_dusk10")
# Measured: baseline against baseline-2 (2026-09-28, no term in either), the
# largest |on - on| of the six cases was 0.175 ms.
RUN_TO_RUN_MS = 0.2


def read(directory):
    cases = {}
    path = os.path.join(directory, "report.txt")
    if not os.path.exists(path):
        return cases
    with open(path) as f:
        for line in f:
            fields = line.split()
            if len(fields) < 2 or fields[0] != "case":
                continue
            cases[fields[1]] = {k: v for k, v in zip(fields[2::2], fields[3::2])}
    return cases


def median_error(case):
    """The standard error of the case's on_ms median."""
    raw = case.get("times_on")
    if raw:
        times = [float(t) for t in raw.split(",")]
        middle = statistics.median(times)
        sigma = 1.4826 * statistics.median(abs(t - middle) for t in times)
    else:
        # No raw times: the IQR is 1.349 sigma of a normal, and the test's rounds are ten.
        times = [0.0] * 10
        sigma = float(case["spread_ms"]) / 1.349
    return 1.2533 * sigma / math.sqrt(len(times))


def main(baseline_dir, after_dir, budget=1.0, not_rise=False):
    base, after = read(baseline_dir), read(after_dir)
    states = set()
    undecided = []
    notes = []
    print("%-14s %7s %9s %9s %9s %9s %8s %8s" % ("case", "sun", "baseline", "on", "cost", "on-off", "band", "spread"))
    for name in CASES:
        if name not in base or name not in after:
            print("%-14s missing from the %s" % (name, "baseline" if name not in base else "run"))
            undecided.append("%s missing" % name)
            continue
        was, now = base[name], after[name]
        if now.get("switch") == "absent":
            notes.append(name)
        cost = float(now["on_ms"]) - float(was["on_ms"])
        band = max(2.0 * math.hypot(median_error(was), median_error(now)), RUN_TO_RUN_MS)
        spread = max(float(now["spread_ms"]), float(was["spread_ms"]))
        if not_rise:
            # Nothing may be added: over is a cost the noise cannot explain.
            close = False
            over = cost > band
        else:
            close = abs(cost - budget) < band
            over = cost > budget and not close
        print("%-14s %7s %9s %9s %+9.3f %+9.3f %8.3f %8.3f%s" % (
            name, now["sun"], was["on_ms"], now["on_ms"], cost, float(now["on_ms"]) - float(now["off_ms"]), band, spread,
            "  TOO CLOSE TO CALL" if close else ("  OVER" if over else "")))
        states.add("over" if over else ("close" if close else "fits"))
    if notes:
        print("switch absent (no ds.Sky.Shadows) in %s: on-off compares a frame with itself" % ", ".join(notes))
    for reason in undecided:
        print(reason)
    # One case clearly over is NO-GO whatever the rest; otherwise any doubt is UNDECIDED.
    verdict = 1 if "over" in states else (2 if "close" in states or undecided else 0)
    print({0: "GO", 1: "NO-GO", 2: "UNDECIDED"}[verdict])
    return verdict


def cli(argv):
    """[--not-rise] <baseline> <after> [budget ms]: --not-rise holds every case to its own band."""
    not_rise = "--not-rise" in argv
    args = [a for a in argv if a != "--not-rise"]
    budget = float(args[2]) if len(args) > 2 else (0.0 if not_rise else 1.0)
    return main(args[0], args[1], budget, not_rise=not_rise)


if __name__ == "__main__":
    sys.exit(cli(sys.argv[1:]))
