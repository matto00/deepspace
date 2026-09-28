"""The cast shadow's GPU gate: for each Eyes.LandingFrame case, the frame with
the term (on_ms) against the same case in a run taken before the term existed
(the baseline's on_ms). On minus off is printed beside it and is not the gate:
with ds.Sky.Shadows 0 the pixel still runs the larger shader. Exit 0 GO,
1 NO-GO, 2 undecided (a case missing, or a cost within 2x its spread of the
budget: repeat the run).

    python3 Tools/landing_frame_gate.py Saved/Eyes/LandingFrame/baseline Saved/Eyes/LandingFrame/shadows [1.0]
"""
import os
import sys


def read(directory):
    cases = {}
    with open(os.path.join(directory, "report.txt")) as f:
        for line in f:
            fields = line.split()
            if len(fields) < 2 or fields[0] != "case":
                continue
            cases[fields[1]] = {k: v for k, v in zip(fields[2::2], fields[3::2])}
    return cases


def main(baseline_dir, after_dir, budget=1.0):
    base, after = read(baseline_dir), read(after_dir)
    states = set()
    missing = False
    print("%-14s %7s %9s %9s %9s %9s %8s" % ("case", "sun", "baseline", "on", "cost", "on-off", "spread"))
    for name in sorted(set(base) | set(after)):
        if name not in base or name not in after:
            print("%-14s missing from the %s" % (name, "baseline" if name not in base else "run"))
            missing = True
            continue
        was, now = base[name], after[name]
        cost = float(now["on_ms"]) - float(was["on_ms"])
        spread = max(float(now["spread_ms"]), float(was["spread_ms"]))
        close = abs(cost - budget) < 2.0 * spread
        over = cost > budget and not close
        print("%-14s %7s %9s %9s %+9.3f %+9.3f %8.3f%s" % (
            name, now["sun"], was["on_ms"], now["on_ms"], cost, float(now["on_ms"]) - float(now["off_ms"]), spread,
            "  TOO CLOSE TO CALL" if close else ("  OVER" if over else "")))
        states.add("over" if over else ("close" if close else "fits"))
    # One case clearly over is NO-GO whatever the rest; otherwise any doubt is UNDECIDED.
    verdict = 1 if "over" in states else (2 if "close" in states or missing else 0)
    print({0: "GO", 1: "NO-GO", 2: "UNDECIDED"}[verdict])
    return verdict


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2], float(sys.argv[3]) if len(sys.argv) > 3 else 1.0))
