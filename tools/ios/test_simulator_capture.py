"""Regression checks for the real two-display simctl error from the Duo CI run."""
import subprocess
import unittest
from pathlib import Path
from capture_simulator_displays import capture
from check_simulator_start import check_screens
import test_simulator_start as fixtures

LOG = fixtures.LOG

AMBIGUOUS = "Cannot choose between 2 displays. Specify one with --display:\n    --display=3  LCD-1 (primary-1) 2007x2853\n    --display=1  LCD (primary) 1398x2034\n"


class CaptureChecks(unittest.TestCase):
    setUp = fixtures.SimulatorStartChecks.setUp
    def test_duo_display_enumeration(self):
        calls = []
        def run(command, **kwargs):
            calls.append(command)
            return subprocess.CompletedProcess(command, 22 if len(calls) == 1 else 0, "", AMBIGUOUS if len(calls) == 1 else "")
        paths = capture("duo", Path(self.tmp.name) / "duo", run)
        self.assertEqual([path.name for path in paths], ["duo-display-3.png", "duo-display-1.png"])
        self.assertIn("--display=3", calls[1])
        self.assertIn("--display=1", calls[2])

    def test_single_display(self):
        paths = capture("phone", Path(self.tmp.name) / "phone", lambda cmd, **kw: subprocess.CompletedProcess(cmd, 0, "", ""))
        self.assertEqual(paths[0].name, "phone-display-default.png")

    def test_other_errors_are_not_hidden(self):
        with self.assertRaises(RuntimeError):
            capture("phone", Path(self.tmp.name) / "phone", lambda cmd, **kw: subprocess.CompletedProcess(cmd, 22, "", "Device is not booted"))

    def test_inactive_display_does_not_mask_active_drawable(self):
        from PIL import Image
        inactive = Path(self.tmp.name) / "inactive.png"
        Image.new("RGB", (600, 900)).save(inactive)
        self.assertIn("PASS", check_screens(LOG, [inactive, self.shot]))

    def test_all_invalid_displays_fail(self):
        from PIL import Image
        Image.new("RGB", (400, 800)).save(self.shot)
        with self.assertRaises(ValueError):
            check_screens(LOG, [self.shot])


if __name__ == "__main__":
    unittest.main()
