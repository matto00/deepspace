"""Tools/relief_look_compare.py reads two Eyes.ReliefLook runs, and fails
when a frame drawn without the cast shadow is not the frame of the run before
it -- on the pixels both runs held still between their own two captures
without it -- and whenever it could not compare at all.

    python3 Tools/test_relief_look_compare.py
"""
import os
import tempfile
import unittest

import numpy
from PIL import Image

import relief_look_compare as compare

LINE = ("frame %s sun 10 off_mean 12.70 on_mean 12.10 read_stops 3 read_p95 200 lit %s held 0.9 coverage 0.0400 "
        "flicker 0.0100 off_crc %s on_crc 0badf00d alias_off %s alias_off2 %s alias_on %s switch present\n")


def frame(value):
    return numpy.full((8, 8, 3), value, dtype=numpy.uint8)


def run(directory, off=None, again=None, lit="0.800", crc="1a2b3c4d", alias=("0.100", "0.100", "0.120"),
        skip=(), stops="3", on=None):
    """A run: its report, and every frame's two read captures without the term."""
    os.makedirs(directory, exist_ok=True)
    off = frame(40) if off is None else off
    again = off if again is None else again
    with open(os.path.join(directory, "report.txt"), "w") as f:
        for name in compare.FRAMES:
            if name in skip:
                continue
            f.write((LINE % ((name, lit, crc) + alias)).replace("read_stops 3", "read_stops " + stops))
            Image.fromarray(off).save(os.path.join(directory, name + "_read_shadows0.png"))
            Image.fromarray(again).save(os.path.join(directory, name + "_read_shadows0b.png"))
            if on is not None:
                Image.fromarray(on).save(os.path.join(directory, name + "_read_shadows1.png"))


class CompareTest(unittest.TestCase):
    def verdict(self, before=None, after=None):
        with tempfile.TemporaryDirectory() as root:
            run(os.path.join(root, "a"), **(before or {}))
            run(os.path.join(root, "b"), **(after or {}))
            return compare.main(os.path.join(root, "a"), os.path.join(root, "b"))

    def test_reads_every_field(self):
        with tempfile.TemporaryDirectory() as root:
            run(root)
            frames = compare.read(root)
            self.assertEqual(frames["world_4_orbit"]["alias_on"], "0.120")
            self.assertEqual(frames["world_4_orbit"]["alias_off2"], "0.100")
            self.assertEqual(len(frames), 12)

    def test_same_off_frames_pass(self):
        self.assertEqual(self.verdict(), 0)

    def test_one_lsb_in_one_pixel_passes_though_the_crc_differs(self):
        nudged = frame(40)
        nudged[3, 3, 1] = 41
        self.assertEqual(self.verdict(after={"off": nudged, "again": nudged, "crc": "ffffffff"}), 0)

    def test_two_lsb_on_every_held_pixel_fails(self):
        self.assertEqual(self.verdict(after={"off": frame(42)}), 1)

    def test_a_mean_shift_within_one_lsb_fails(self):
        self.assertEqual(self.verdict(after={"off": frame(41)}), 1)

    def test_flicker_is_not_a_difference(self):
        # The review's finding: a pixel that moved between a run's own two
        # captures is the far tiles' flicker, not the shader.
        flicker, other = frame(40), frame(40)
        flicker[0, 0] = 105
        other[0, 0] = 40
        self.assertEqual(self.verdict(after={"off": flicker, "again": other}), 0)

    def test_a_few_moved_tiles_pass_and_many_fail(self):
        # Two runs draw a few tiles at another level; a changed off path moves most of the frame.
        few, many = frame(40), frame(40)
        few[0, :6] = 90      # 6 of 64 pixels: under MAX_MOVED
        many[:4] = 90        # half the frame
        self.assertEqual(self.verdict(after={"off": few, "again": few}), 0)
        self.assertEqual(self.verdict(after={"off": many, "again": many}), 1)

    def test_too_little_held_fails(self):
        wild = frame(40)
        wild[:5] = 200
        self.assertEqual(self.verdict(after={"off": wild, "again": frame(40)}), 1)

    def test_a_frame_missing_before_fails(self):
        self.assertEqual(self.verdict(before={"skip": ("world_4_orbit",)}), 1)

    def test_a_frame_missing_after_fails(self):
        self.assertEqual(self.verdict(after={"skip": ("world_4_orbit",)}), 1)

    def test_empty_reports_fail(self):
        with tempfile.TemporaryDirectory() as root:
            for name in ("a", "b"):
                os.makedirs(os.path.join(root, name))
                open(os.path.join(root, name, "report.txt"), "w").close()
            self.assertEqual(compare.main(os.path.join(root, "a"), os.path.join(root, "b")), 1)

    def test_a_frame_too_dark_to_read_fails(self):
        self.assertEqual(self.verdict(after={"lit": "0.010"}), 1)

    def test_a_changed_read_exposure_fails(self):
        self.assertEqual(self.verdict(after={"stops": "4"}), 1)

    def test_aliasing_is_read_against_its_own_noise(self):
        self.assertTrue(compare.aliasing({"alias_off": "0.100", "alias_off2": "0.110", "alias_on": "0.400"}))
        # 0.014 -> 0.084 is the before run's own noise on a black frame: under the 0.1 floor.
        self.assertFalse(compare.aliasing({"alias_off": "0.014", "alias_off2": "0.020", "alias_on": "0.084"}))
        # Past the floor, but the two captures without the term already differ by 0.2.
        self.assertFalse(compare.aliasing({"alias_off": "0.200", "alias_off2": "0.400", "alias_on": "0.500"}))


    def test_held_means_are_read_on_the_pixels_both_runs_held(self):
        # The term darkens rows 0-3 to 10; one pixel of row 0 flickered in the
        # run after, so 63 pixels are held in both runs.
        with tempfile.TemporaryDirectory() as root:
            on = frame(40)
            on[:4] = 10
            again = frame(40)
            again[0, 0] = 90
            run(os.path.join(root, "a"))
            run(os.path.join(root, "b"), again=again, on=on)
            before, off, lit = compare.held_means(os.path.join(root, "a"), os.path.join(root, "b"), "world_4_orbit")
            self.assertAlmostEqual(before, 40.0, places=6)
            self.assertAlmostEqual(off, 40.0, places=6)
            self.assertAlmostEqual(lit, (31 * 10 + 32 * 40) / 63.0, places=6)

    def test_held_means_without_the_frame_with_the_term(self):
        with tempfile.TemporaryDirectory() as root:
            run(os.path.join(root, "a"))
            run(os.path.join(root, "b"))
            self.assertIsNone(compare.held_means(os.path.join(root, "a"), os.path.join(root, "b"), "world_4_orbit")[2])


if __name__ == "__main__":
    unittest.main()
