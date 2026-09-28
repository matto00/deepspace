"""Tools/landing_frame_gate.py: the cast shadow's cost is each case's frame
with the term against the same case before the term existed, and the gate
decides only when the noise lets it -- the noise of the medians, not the
spread of single frames.

    python3 Tools/test_landing_frame_gate.py
"""
import os
import tempfile
import unittest

import landing_frame_gate as gate

CASES = gate.CASES
# 200km_dusk10's raw times in the second baseline (2026-09-28): an IQR of
# 0.92 ms, the widest the gate has seen, with its slow drift upward.
MEASURED = (12.75, 12.14, 12.66, 12.58, 12.81, 13.22, 13.39, 13.24, 13.45, 13.61)
MEASURED_IQR = 0.922


def run(directory, on, off=None, spread=MEASURED_IQR, skip=(), raw=True, switch=None, times=MEASURED):
    """A run whose every case's times are the measured ones, moved to a median of `on`."""
    os.makedirs(directory, exist_ok=True)
    middle = sorted(times)[len(times) // 2 - 1: len(times) // 2 + 1]
    shift = on - 0.5 * sum(middle)
    with open(os.path.join(directory, "report.txt"), "w") as f:
        for case in CASES:
            if case in skip:
                continue
            f.write("case %s sun 10.00 on_ms %.3f off_ms %.3f spread_ms %.3f coverage 0.0000 "
                    "frame_crc_on 0 frame_crc_off 0 tiles 700%s%s\nground: ...\n" % (
                        case, on, off if off else on, spread,
                        " switch %s" % switch if switch else "",
                        " times_on %s" % ",".join("%.2f" % (t + shift) for t in times) if raw else ""))


class GateTest(unittest.TestCase):
    def verdict(self, **after):
        with tempfile.TemporaryDirectory() as root:
            run(os.path.join(root, "base"), 10.0)
            run(os.path.join(root, "after"), **after)
            return gate.main(os.path.join(root, "base"), os.path.join(root, "after"), 1.0)

    def test_a_free_term_is_go_at_the_measured_noise(self):
        # The review's finding: at 2 x the IQR a free term was never GO.
        self.assertEqual(self.verdict(on=10.0), 0)

    def test_under_budget_is_go_at_the_measured_noise(self):
        self.assertEqual(self.verdict(on=10.3, off=10.2), 0)

    def test_over_budget_is_no_go(self):
        self.assertEqual(self.verdict(on=16.0, off=10.4), 1)

    def test_on_minus_off_is_not_the_gate(self):
        # The switch's own frame is dearer than the baseline: on - off looks free.
        self.assertEqual(self.verdict(on=12.0, off=11.9), 1)

    def test_too_close_to_call(self):
        self.assertEqual(self.verdict(on=10.95, off=10.0), 2)

    def test_the_run_to_run_floor_holds_a_quiet_run_back(self):
        # Times with no spread at all still differ run to run by RUN_TO_RUN_MS.
        self.assertEqual(self.verdict(on=10.85, times=(5.0,) * 10), 2)

    def test_without_raw_times_the_iqr_is_read_as_sigma(self):
        self.assertEqual(self.verdict(on=10.3, raw=False), 0)
        self.assertEqual(self.verdict(on=10.9, raw=False), 2)

    def test_a_missing_case_is_undecided(self):
        self.assertEqual(self.verdict(on=10.2, skip=("1.5m_dusk3",)), 2)

    def test_empty_reports_are_undecided_never_go(self):
        with tempfile.TemporaryDirectory() as root:
            for name in ("base", "after"):
                os.makedirs(os.path.join(root, name))
                open(os.path.join(root, name, "report.txt"), "w").close()
            self.assertEqual(gate.main(os.path.join(root, "base"), os.path.join(root, "after"), 1.0), 2)

    def test_a_missing_report_is_undecided(self):
        with tempfile.TemporaryDirectory() as root:
            run(os.path.join(root, "base"), 10.0)
            self.assertEqual(gate.main(os.path.join(root, "base"), os.path.join(root, "nowhere"), 1.0), 2)

    def test_a_run_without_the_switch_is_still_priced(self):
        # The spike writes no switch: its cost is still a cost.
        self.assertEqual(self.verdict(on=16.0, switch="absent"), 1)


if __name__ == "__main__":
    unittest.main()
