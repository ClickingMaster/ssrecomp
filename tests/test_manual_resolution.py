from pathlib import Path
import shutil,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
class ManualResolutionTests(unittest.TestCase):
 @unittest.skipUnless(shutil.which('c++') and shutil.which('sdl2-config'),'C++ and SDL required')
 def test_launcher_persistence(self):
  with tempfile.TemporaryDirectory() as folder:
   exe=Path(folder)/'resolution'
   flags=subprocess.check_output(['sdl2-config','--cflags','--libs'],text=True).split()
   subprocess.run(['c++','-std=c++17','-O1','-I'+str(ROOT/'frontend'),str(ROOT/'tests/test_manual_resolution.cpp'),'-o',str(exe)]+flags,check=True)
   subprocess.run([str(exe),folder],check=True)
