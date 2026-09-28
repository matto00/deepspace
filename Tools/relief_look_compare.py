"""Eyes.ReliefLook's two runs side by side: each frame's mean brightness
before the cast shadow existed, without it now, and with it; how much of the
frame the measures could read (lit, held); the share of the ground the term
shades, beside the flicker that is the noise; and its aliasing without and
with it, beside the aliasing's own noise.

It fails if a frame drawn without the term is not the frame of the run
before. Not over the whole picture: two captures of one still scene differ
by up to 65 LSB on the far tiles' skirts and seams (the review measured it
inside one run), so a whole-frame identity fails every ground frame whatever
the shader did. The frames compared are the READ frames (the game's exposure
some stops brighter; the report's read_stops must match), and only on the
pixels both runs held still between their own two captures without the term
(`_read_shadows0` and `_read_shadows0b`, within 1 LSB in every channel).
Even there two runs are not one frame: which tiles are resident, and at which
level, differs from run to run, so whole tiles near the horizon (and some
near the ship) draw differently -- two runs before the term, 2026-09-28,
moved 1.2-6.2% of the held pixels by more than 1 LSB, and matched the rest
within 1 LSB, whose mean luma still differed by up to 0.09; the orbits
matched everywhere. So the rule is: at most MAX_MOVED of the held pixels
moved by more than 1 LSB, and over the rest the mean luma within
MAX_MEAN_LUMA, a quarter of a level. A changed off path moves most of the
picture, or shifts all of it by a level, and fails either way. Exact
identity is not asked: multiplying by 1.0 is exact, but a changed graph can
change how the SPIR-V compiler fuses FMAs elsewhere. The CRCs are printed.

It also fails, rather than passing on nothing, when a frame of FRAMES is
missing from either run, when the pixels held in both runs are under
MIN_HELD of the frame, and when a read frame is too dark to measure (lit
under MIN_LIT: re-choose the test's ReadStops with EYES_METER=1).

ALIASING? marks alias_on over alias_off by more than 1.5x and by more than
max(0.1, 3 x |alias_off2 - alias_off|): the second capture without the term
is what the measure reads with nothing changed.

    python3 Tools/relief_look_compare.py Saved/Eyes/ReliefLook/shadows-before Saved/Eyes/ReliefLook/shadows-after
"""
import os
import sys

import numpy
from PIL import Image

MAX_LSB = 1
MAX_MEAN_LUMA = 0.25
MAX_MOVED = 0.10
MIN_HELD = 0.5
MIN_LIT = 0.05
FRAMES = tuple("world_%d_%s" % (world, view) for world in (3, 4, 5)
               for view in ("orbit", "ground", "orbit_low", "ground_low"))
WEIGHTS = numpy.array([0.299, 0.587, 0.114])


def read(directory):
    """{frame name: {field: value}} from a report's `frame` lines."""
    frames = {}
    path = os.path.join(directory, "report.txt")
    if not os.path.exists(path):
        return frames
    with open(path) as f:
        for line in f:
            fields = line.split()
            if len(fields) < 2 or fields[0] != "frame":
                continue
            frames[fields[1]] = dict(zip(fields[2::2], fields[3::2]))
    return frames


def png(directory, name, suffix):
    path = os.path.join(directory, "%s_read_shadows%s.png" % (name, suffix))
    if not os.path.exists(path):
        return None
    return numpy.asarray(Image.open(path).convert("RGB"), dtype=numpy.int32)


def held(a, b):
    """The pixels of two captures within MAX_LSB in every channel."""
    return (numpy.abs(a - b) <= MAX_LSB).all(axis=2)


def same(before_dir, after_dir, name):
    """(same within tolerance, held share, share moved, mean luma gap of the rest) over the pixels both runs held."""
    frames = [png(d, name, s) for d in (before_dir, after_dir) for s in ("0", "0b")]
    if any(f is None for f in frames) or len({f.shape for f in frames}) != 1:
        return False, None, None, None
    was, was_again, now, now_again = frames
    mask = held(was, was_again) & held(now, now_again)
    share = float(mask.mean())
    if not mask.any():
        return False, share, None, None
    moved = (numpy.abs(was - now) > MAX_LSB).any(axis=2) & mask
    moved_share = float(moved.sum()) / float(mask.sum())
    rest = mask & ~moved
    mean = abs(float((was[rest] * WEIGHTS).sum(axis=1).mean() - (now[rest] * WEIGHTS).sum(axis=1).mean())) if rest.any() else 0.0
    return share >= MIN_HELD and moved_share <= MAX_MOVED and mean < MAX_MEAN_LUMA, share, moved_share, mean


def aliasing(frame):
    off, again, on = (float(frame.get(k, "nan")) for k in ("alias_off", "alias_off2", "alias_on"))
    noise = abs(again - off)
    return on > 1.5 * off and on - off > max(0.1, 3.0 * noise)


def main(before_dir, after_dir):
    before, after = read(before_dir), read(after_dir)
    failed = False
    print("%-20s %4s %8s %8s %8s %6s %6s %9s %8s %17s %11s" % (
        "frame", "sun", "before", "off", "on", "lit", "held", "coverage", "flicker", "alias off/2/on", "moved/gap"))
    for name in FRAMES:
        was, now = before.get(name), after.get(name)
        if was is None or now is None:
            print("%-20s missing from the %s" % (name, "run before" if was is None else "run after"))
            failed = True
            continue
        notes = []
        if was.get("read_stops") != now.get("read_stops"):
            notes.append("READ EXPOSURE DIFFERS (%s, %s)" % (was.get("read_stops"), now.get("read_stops")))
        ok, share, worst, mean = same(before_dir, after_dir, name)
        if not ok:
            notes.append("OFF FRAME DIFFERS" if share is not None and share >= MIN_HELD else "TOO LITTLE HELD TO COMPARE")
        if float(now.get("lit", 0.0)) < MIN_LIT:
            notes.append("TOO DARK TO READ")
        if aliasing(now):
            notes.append("ALIASING?")
        failed = failed or any(n != "ALIASING?" for n in notes)
        if was.get("off_crc") != now.get("off_crc"):
            notes.append("(crc differs)")
        print("%-20s %4s %8s %8s %8s %6.3f %6.3f %8.1f%% %7.1f%% %5s/%5s/%5s %11s  %s" % (
            name, now["sun"], was["off_mean"], now["off_mean"], now["on_mean"], float(now.get("lit", 0.0)),
            share if share is not None else 0.0, 100.0 * float(now["coverage"]), 100.0 * float(now.get("flicker", 0.0)),
            now.get("alias_off", "-"), now.get("alias_off2", "-"), now.get("alias_on", "-"),
            "-" if worst is None else "%.3f/%.3f" % (worst, mean), "  ".join(notes)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2]))
