"""Exercise real translated rendering with synthetic settings, without ROMs or saves."""
from pathlib import Path
import shutil, subprocess, tempfile, unittest
ROOT=Path(__file__).resolve().parents[1]
class LocalisedMenuTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('c++'),'C++ compiler required')
    def test_translation_and_rendering(self):
        with tempfile.TemporaryDirectory() as folder:
            exe=Path(folder)/'localised-menu'
            subprocess.run(['c++','-std=c++17','-I'+str(ROOT/'frontend'),str(ROOT/'tests/test_localised_menu.cpp'),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
