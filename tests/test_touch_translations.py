from pathlib import Path
import shutil,subprocess,tempfile,unittest,shlex
ROOT=Path(__file__).resolve().parents[1]
class TouchTranslationTests(unittest.TestCase):
 @unittest.skipUnless(shutil.which('pkg-config') and shutil.which('c++'),'SDL compiler tools required')
 def test_translated_editor_actions_fit(self):
  flags=subprocess.run(['pkg-config','--cflags','--libs','sdl2'],capture_output=True,text=True)
  if flags.returncode:self.skipTest('SDL development package required')
  with tempfile.TemporaryDirectory() as folder:
   exe=Path(folder)/'actions'
   subprocess.run(['c++','-std=c++17','-I'+str(ROOT/'frontend'),str(ROOT/'tests/test_touch_translations.cpp'),'-o',str(exe)]+shlex.split(flags.stdout),check=True)
   subprocess.run([str(exe)],check=True,timeout=20)
