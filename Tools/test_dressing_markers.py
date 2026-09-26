#!/usr/bin/env python3
"""
Tests that Tools/dressing_markers.json is what the layout exports now.

    python3 Tools/test_dressing_markers.py

The C++ end-to-end dressing tests spawn their markers from that file, so a
stale file would test a ship that no longer exists. Regenerate with
`python3 Tools/dressing_markers.py`.
"""

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dressing_markers as D
import hauler_layout as L


def committed():
    with open(D.PATH) as f:
        return json.load(f)


def test_the_file_is_what_the_layout_exports():
    fresh = json.loads(D.dumps(D.export()))
    assert committed() == fresh, "stale: run python3 Tools/dressing_markers.py"


def test_every_surface_and_keep_out_is_in_it():
    ship = L.generate()
    data = committed()
    assert [s["label"] for s in data["surfaces"]] == [m.label for m in ship.surfaces]
    assert [k["label"] for k in data["keep_outs"]] == [k.label for k in ship.keep_outs]
    assert len(data["surfaces"]) > 0 and len(data["keep_outs"]) > 0


def test_the_screens_excludes_lie_on_a_surface():
    """Each screen's rectangle meets a surface it would otherwise be dressed
    over, or a test of the exclude would pass on an empty desk."""
    data = committed()
    for screen in ("laptop", "chart"):
        (x0, y0), (x1, y1) = data[screen]["exclude"]
        hit = False
        for s in data["surfaces"]:
            sx, sy = s["size"]
            if int(s["yaw"]) % 180:
                sx, sy = sy, sx
            cx, cy, _ = s["location"]
            if x0 < cx + sx / 2 and cx - sx / 2 < x1 and y0 < cy + sy / 2 and cy - sy / 2 < y1:
                hit = True
        assert hit, screen


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
