"""Compile and exercise custom layout and localisation behaviour without game content."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class UiCustomisationTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('c++'), 'C++ compiler required')
    def test_languages_and_regional_saves(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / ('languages.exe' if os.name == 'nt' else 'languages')
            subprocess.run(['c++', '-std=c++17', '-I' + str(ROOT/'frontend'), str(ROOT/'tests/test_languages.cpp'), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    @unittest.skipUnless(shutil.which('pkg-config') and shutil.which('c++'), 'SDL compiler tools required')
    def test_touch_editor(self):
        import shlex
        flags = subprocess.run(['pkg-config', '--cflags', '--libs', 'sdl2'], capture_output=True, text=True)
        if flags.returncode:
            self.skipTest('SDL2 development package required')
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / ('touch.exe' if os.name == 'nt' else 'touch')
            subprocess.run(['c++', '-std=c++17', '-pthread', '-I' + str(ROOT/'frontend'), str(ROOT/'tests/test_touch_layout.cpp'), '-o', str(exe)] + shlex.split(flags.stdout), check=True)
            subprocess.run([str(exe)], check=True, env=dict(os.environ, SDL_VIDEODRIVER='dummy'), timeout=20)
