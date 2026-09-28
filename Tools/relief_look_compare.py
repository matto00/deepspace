"""Eyes.ReliefLook's two runs side by side: each frame's mean brightness
before the cast shadow existed, without it now, and with it, the share of the
ground it shades, and its aliasing (1x against 2x downsampled) without and
with it. Fails if any frame drawn without the term is not the frame of the
run before: every channel within 1 LSB, mean luma within 0.01. Exact pixel
identity is not asked: multiplying by 1.0 is exact, but a changed graph can
change how the SPIR-V compiler fuses FMAs elsewhere. The CRCs are printed.

    python3 Tools/relief_look_compare.py Saved/Eyes/ReliefLook/shadows-before Saved/Eyes/ReliefLook/shadows-after
"""
import os
import sys

import numpy
from PIL import Image

MAX_LSB = 1
MAX_MEAN_LUMA = 0.01


def read(directory):
    """{frame name: {field: value}} from a report's `frame` lines."""
    frames = {}
    with open(os.path.join(directory, "report.txt")) as f:
        for line in f:
            fields = line.split()
            if len(fields) < 2 or fields[0] != "frame":
                continue
            frames[fields[1]] = dict(zip(fields[2::2], fields[3::2]))
    return frames


def off_frame(directory, name):
    path = os.path.join(directory, name + "_shadows0.png")
    if not os.path.exists(path):
        return None
    return numpy.asarray(Image.open(path).convert("RGB"), dtype=numpy.int32)


def same(was, now):
    """(same within tolerance, worst channel gap, mean luma gap)."""
    if was is None or now is None or was.shape != now.shape:
        return False, None, None
    worst = int(numpy.abs(was - now).max())
    weights = numpy.array([0.299, 0.587, 0.114])
    mean = abs(float((was * weights).sum(axis=2).mean() - (now * weights).sum(axis=2).mean()))
    return worst <= MAX_LSB and mean < MAX_MEAN_LUMA, worst, mean


def main(before_dir, after_dir):
    before, after = read(before_dir), read(after_dir)
    failed = False
    print("%-20s %4s %8s %8s %8s %9s %11s %9s" % ("frame", "sun", "before", "off", "on", "coverage", "alias", "off gap"))
    for name in sorted(after):
        now = after[name]
        was = before.get(name)
        ok, worst, mean = same(off_frame(before_dir, name), off_frame(after_dir, name))
        failed = failed or was is None or not ok
        crc = "" if was is not None and was["off_crc"] == now["off_crc"] else "  (crc differs)"
        print("%-20s %4s %8s %8s %8s %8.1f%% %5s/%5s %9s%s%s" % (
            name, now["sun"], was["off_mean"] if was else "-", now["off_mean"], now["on_mean"],
            100.0 * float(now["coverage"]), now.get("alias_off", "-"), now.get("alias_on", "-"),
            "-" if worst is None else "%d/%.3f" % (worst, mean), crc, "" if ok else "  OFF FRAME DIFFERS"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2]))
