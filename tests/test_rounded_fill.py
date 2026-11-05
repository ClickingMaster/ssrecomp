from pathlib import Path
import shutil,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
class RoundedFillTests(unittest.TestCase):
 @unittest.skipUnless(shutil.which('c++') and shutil.which('sdl2-config'),'C++ and SDL required')
 def test_corners(self):
  with tempfile.TemporaryDirectory() as folder:
   exe=Path(folder)/'rounded-fill'
   flags=subprocess.check_output(['sdl2-config','--cflags','--libs'],text=True).split()
   subprocess.run(['c++','-std=c++17','-I'+str(ROOT/'frontend'),str(ROOT/'tests/test_rounded_fill.cpp'),'-o',str(exe)]+flags+['-pthread'],check=True)
   subprocess.run([str(exe)],check=True,env={**__import__('os').environ,'SDL_VIDEODRIVER':'dummy'})
