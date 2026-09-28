"""Tools/landing_frame_gate.py: the cast shadow's cost is each case's frame
with the term against the same case before the term existed, and the gate
decides only when the noise lets it.

    python3 Tools/test_landing_frame_gate.py
"""
import os
import tempfile
import unittest

import landing_frame_gate as gate

CASES = ("50km", "1.5m", "50km_dusk10", "1.5m_dusk10", "1.5m_dusk3", "200km_dusk10")


def run(directory, on, off=None, spread=0.05, skip=()):
    os.makedirs(directory, exist_ok=True)
    with open(os.path.join(directory, "report.txt"), "w") as f:
        for case in CASES:
            if case in skip:
                continue
            f.write("case %s sun 10.00 on_ms %.3f off_ms %.3f spread_ms %.3f coverage 0.0000 "
                    "frame_crc_on 0 frame_crc_off 0 tiles 700\nground: ...\n" % (case, on, off if off else on, spread))


class GateTest(unittest.TestCase):
    def verdict(self, **after):
        with tempfile.TemporaryDirectory() as root:
            run(os.path.join(root, "base"), 10.0)
            run(os.path.join(root, "after"), **after)
            return gate.main(os.path.join(root, "base"), os.path.join(root, "after"), 1.0)

    def test_under_budget_is_go(self):
        self.assertEqual(self.verdict(on=10.5, off=10.3), 0)

    def test_over_budget_is_no_go(self):
        self.assertEqual(self.verdict(on=16.0, off=10.4), 1)

    def test_on_minus_off_is_not_the_gate(self):
        # The switch's own frame is dearer than the baseline: on - off looks free.
        self.assertEqual(self.verdict(on=11.5, off=11.4), 1)

    def test_too_close_to_call(self):
        self.assertEqual(self.verdict(on=10.95, off=10.0, spread=0.1), 2)

    def test_a_missing_case_is_undecided(self):
        self.assertEqual(self.verdict(on=10.2, skip=("1.5m_dusk3",)), 2)


if __name__ == "__main__":
    unittest.main()
