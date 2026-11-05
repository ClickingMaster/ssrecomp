from pathlib import Path
import shutil,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
class SmoothKeyRingTests(unittest.TestCase):
 @unittest.skipUnless(shutil.which('c++') and shutil.which('sdl2-config'),'C++ and SDL required')
 def test_smooth_matches_nearest(self):
  with tempfile.TemporaryDirectory() as folder:
   exe=Path(folder)/'smooth-key-ring'
   flags=subprocess.check_output(['sdl2-config','--cflags','--libs'],text=True).split()
   subprocess.run(['c++','-std=c++17','-I'+str(ROOT/'frontend'),str(ROOT/'tests/test_smooth_key_ring.cpp'),'-o',str(exe)]+flags,check=True)
   subprocess.run([str(exe)],check=True)
