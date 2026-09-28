"""Tools/relief_look_compare.py reads two Eyes.ReliefLook runs, and fails
when a frame drawn without the cast shadow is not the frame of the run before
it: every channel within 1 LSB, and the mean luma within 0.01.

    python3 Tools/test_relief_look_compare.py
"""
import os
import tempfile
import unittest

import numpy
from PIL import Image

import relief_look_compare as compare

LINE = ("frame world_4_orbit sun 10 off_mean 12.70 on_mean %s coverage %s off_crc %s on_crc 0badf00d"
        " alias_off 0.100 alias_on 0.120\n")


def run(directory, pixels, *lines):
    """A run: its report, and world_4_orbit's shadows-off frame."""
    os.makedirs(directory, exist_ok=True)
    with open(os.path.join(directory, "report.txt"), "w") as f:
        f.writelines(lines)
    if pixels is not None:
        Image.fromarray(pixels).save(os.path.join(directory, "world_4_orbit_shadows0.png"))


def frame(value):
    return numpy.full((8, 8, 3), value, dtype=numpy.uint8)


class CompareTest(unittest.TestCase):
    def test_reads_every_field(self):
        with tempfile.TemporaryDirectory() as root:
            run(root, frame(40), LINE % ("12.10", "0.0040", "1a2b3c4d"))
            frames = compare.read(root)
            self.assertEqual(frames["world_4_orbit"]["on_mean"], "12.10")
            self.assertEqual(frames["world_4_orbit"]["alias_on"], "0.120")

    def test_same_off_frames_pass(self):
        with tempfile.TemporaryDirectory() as root:
            run(os.path.join(root, "a"), frame(40), LINE % ("12.70", "0.0000", "1a2b3c4d"))
            run(os.path.join(root, "b"), frame(40), LINE % ("12.10", "0.0040", "1a2b3c4d"))
            self.assertEqual(compare.main(os.path.join(root, "a"), os.path.join(root, "b")), 0)

    def test_one_lsb_in_one_pixel_passes_though_the_crc_differs(self):
        with tempfile.TemporaryDirectory() as root:
            nudged = frame(40)
            nudged[3, 3, 1] = 41
            run(os.path.join(root, "a"), frame(40), LINE % ("12.70", "0.0000", "1a2b3c4d"))
            run(os.path.join(root, "b"), nudged, LINE % ("12.10", "0.0040", "ffffffff"))
            self.assertEqual(compare.main(os.path.join(root, "a"), os.path.join(root, "b")), 0)

    def test_two_lsb_fails(self):
        with tempfile.TemporaryDirectory() as root:
            nudged = frame(40)
            nudged[3, 3, 1] = 42
            run(os.path.join(root, "a"), frame(40), LINE % ("12.70", "0.0000", "1a2b3c4d"))
            run(os.path.join(root, "b"), nudged, LINE % ("12.10", "0.0040", "ffffffff"))
            self.assertEqual(compare.main(os.path.join(root, "a"), os.path.join(root, "b")), 1)

    def test_a_mean_shift_within_one_lsb_fails(self):
        with tempfile.TemporaryDirectory() as root:
            run(os.path.join(root, "a"), frame(40), LINE % ("12.70", "0.0000", "1a2b3c4d"))
            run(os.path.join(root, "b"), frame(41), LINE % ("12.10", "0.0040", "ffffffff"))
            self.assertEqual(compare.main(os.path.join(root, "a"), os.path.join(root, "b")), 1)

    def test_a_frame_missing_before_fails(self):
        with tempfile.TemporaryDirectory() as root:
            run(os.path.join(root, "a"), None)
            run(os.path.join(root, "b"), frame(40), LINE % ("12.10", "0.0040", "1a2b3c4d"))
            self.assertEqual(compare.main(os.path.join(root, "a"), os.path.join(root, "b")), 1)


if __name__ == "__main__":
    unittest.main()
