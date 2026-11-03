"""Host tests for the CI checks that must reject a black-screen first start."""
import tempfile
import unittest
from pathlib import Path
from PIL import Image, ImageDraw
from check_simulator_start import check_start

LOG = """[launcher] folder /Documents
[launcher] iOS window=1 renderer-window=1 association=correct
[launcher] drawable window=1 points=200x400 pixels=400x800
[launcher] frame presented window=1 renderer=metal canvas=200x400 drawable=400x800
"""


class SimulatorStartChecks(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="build-ios-ci-", dir=Path(__file__).resolve().parents[2])
        self.addCleanup(self.tmp.cleanup)
        self.shot = Path(self.tmp.name) / "screen.png"
        image = Image.new("RGB", (400, 800), (16, 16, 16))
        ImageDraw.Draw(image).rectangle((50, 150, 350, 180), fill=(200, 200, 200))
        image.save(self.shot)

    def test_valid_high_density_frame(self):
        self.assertIn("PASS", check_start(LOG, self.shot))

    def test_original_black_screen_log(self):
        with self.assertRaises(ValueError):
            check_start("[launcher] folder /Documents\n[launcher] no window: No connected UIWindowScene for SDL display", self.shot)

    def test_wrong_renderer_window(self):
        with self.assertRaises(ValueError):
            check_start(LOG.replace("renderer-window=1", "renderer-window=2"), self.shot)

    def test_no_presented_frame(self):
        with self.assertRaises(ValueError):
            check_start(LOG.split("[launcher] frame")[0], self.shot)

    def test_zero_drawable(self):
        with self.assertRaises(ValueError):
            check_start(LOG.replace("pixels=400x800", "pixels=0x0"), self.shot)

    def test_unpresented_resize(self):
        with self.assertRaises(ValueError):
            check_start(LOG + "[launcher] drawable window=1 points=400x200 pixels=800x400\n", self.shot)

    def test_oversized_margins(self):
        with self.assertRaises(ValueError):
            check_start(LOG.replace("400x800", "200x400"), self.shot)

    def test_black_screen_with_system_chrome(self):
        image = Image.new("RGB", (400, 800))
        ImageDraw.Draw(image).rectangle((150, 780, 250, 790), fill="white")
        image.save(self.shot)
        with self.assertRaises(ValueError):
            check_start(LOG, self.shot)

    def test_uniform_blank_screen(self):
        Image.new("RGB", (400, 800), "white").save(self.shot)
        with self.assertRaises(ValueError):
            check_start(LOG, self.shot)


if __name__ == "__main__":
    unittest.main()
