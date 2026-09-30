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


def run(directory, on, off=None, spread=MEASURED_IQR, skip=(), raw=True, switch=None, times=MEASURED, moving=False, moving_on=None):
    """A run whose every case's times are the measured ones, moved to a median of `on`
    (the moving cases, when written, to `moving_on`, or `on`)."""
    os.makedirs(directory, exist_ok=True)
    with open(os.path.join(directory, "report.txt"), "w") as f:
        for case in CASES + (gate.MOVING if moving else ()):
            if case in skip:
                continue
            at = moving_on if moving_on is not None and case in gate.MOVING else on
            middle = sorted(times)[len(times) // 2 - 1: len(times) // 2 + 1]
            shift = at - 0.5 * sum(middle)
            f.write("case %s sun 10.00 on_ms %.3f off_ms %.3f spread_ms %.3f coverage 0.0000 "
                    "frame_crc_on 0 frame_crc_off 0 tiles 700%s%s\nground: ...\n" % (
                        case, at, off if off else at, spread,
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

    def test_a_baseline_without_the_moving_cases_is_still_decided(self):
        # Every baseline before 2026-09-29 predates them: left out, not UNDECIDED.
        with tempfile.TemporaryDirectory() as root:
            run(os.path.join(root, "base"), 10.0)
            run(os.path.join(root, "after"), 10.0, moving=True)
            self.assertEqual(gate.main(os.path.join(root, "base"), os.path.join(root, "after"), 1.0), 0)

    def test_the_moving_cases_are_read_when_both_runs_have_them(self):
        with tempfile.TemporaryDirectory() as root:
            run(os.path.join(root, "base"), 10.0, moving=True)
            run(os.path.join(root, "after"), 10.0, moving=True, moving_on=16.0)
            self.assertEqual(gate.main(os.path.join(root, "base"), os.path.join(root, "after"), 1.0), 1)

    def test_a_run_missing_moving_cases_its_baseline_has_is_undecided(self):
        # A subset, or a run that stopped writing them: never GO.
        with tempfile.TemporaryDirectory() as root:
            run(os.path.join(root, "base"), 10.0, moving=True)
            run(os.path.join(root, "after"), 10.0, moving=True, skip=("50km_drive_dusk10", "1.5m_skim"))
            self.assertEqual(gate.main(os.path.join(root, "base"), os.path.join(root, "after"), 1.0), 2)

    def test_a_run_with_only_the_still_cases_against_a_full_baseline_is_undecided(self):
        with tempfile.TemporaryDirectory() as root:
            run(os.path.join(root, "base"), 10.0, moving=True)
            run(os.path.join(root, "after"), 10.0)
            self.assertEqual(gate.main(os.path.join(root, "base"), os.path.join(root, "after"), 1.0), 2)

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


class NotRiseTest(unittest.TestCase):
    """--not-rise: the baked shadow must cost the frame nothing measurable.
    GO only if no case's cost is over its band -- the medians' error, floored
    at RUN_TO_RUN_MS -- and a case faster than its baseline is GO too."""

    def verdict(self, **after):
        with tempfile.TemporaryDirectory() as root:
            run(os.path.join(root, "base"), 10.0)
            run(os.path.join(root, "after"), **after)
            return gate.main(os.path.join(root, "base"), os.path.join(root, "after"), 0.0, not_rise=True)

    def test_a_free_bake_is_go(self):
        self.assertEqual(self.verdict(on=10.0), 0)

    def test_a_cost_inside_the_band_is_go(self):
        # The measured spread's band is about 0.6 ms: 0.3 ms cannot be told from nothing.
        self.assertEqual(self.verdict(on=10.3), 0)

    def test_faster_is_go(self):
        self.assertEqual(self.verdict(on=9.0), 0)

    def test_a_rise_over_the_band_is_no_go(self):
        self.assertEqual(self.verdict(on=11.0), 1)

    def test_a_missing_case_is_undecided(self):
        self.assertEqual(self.verdict(on=10.0, skip=("1.5m_dusk3",)), 2)

    def test_the_command_line_takes_the_flag(self):
        with tempfile.TemporaryDirectory() as root:
            run(os.path.join(root, "base"), 10.0)
            run(os.path.join(root, "after"), 11.0)
            self.assertEqual(gate.cli(["--not-rise", os.path.join(root, "base"), os.path.join(root, "after")]), 1)
            # Without it the budget is 1 ms, and a cost of 1 ms is too close to call.
            self.assertEqual(gate.cli([os.path.join(root, "base"), os.path.join(root, "after")]), 2)

if __name__ == "__main__":
    unittest.main()
