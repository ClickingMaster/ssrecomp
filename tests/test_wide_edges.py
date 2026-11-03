from pathlib import Path
import shutil,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
class WideEdgesTests(unittest.TestCase):
 @unittest.skipUnless(shutil.which('c++') and shutil.which('sdl2-config'),'C++ and SDL required')
 def test_patterned_edges(self):
  with tempfile.TemporaryDirectory() as folder:
   exe=Path(folder)/'wide-edges'
   flags=subprocess.check_output(['sdl2-config','--cflags','--libs'],text=True).split()
   subprocess.run(['c++','-std=c++17','-I'+str(ROOT/'frontend'),str(ROOT/'tests/test_wide_edges.cpp'),'-o',str(exe)]+flags,check=True)
   subprocess.run([str(exe)],check=True)
