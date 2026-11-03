from pathlib import Path
import shutil,subprocess,tempfile,unittest,sys
ROOT=Path(__file__).resolve().parents[1]
class AllLanguagesTests(unittest.TestCase):
 @unittest.skipUnless(shutil.which('c++'),'C++ compiler required')
 def test_all_supported_interface_languages(self):
  subprocess.run([sys.executable,str(ROOT/'tools/localisation/generate_translations.py'),'--check'],check=True)
  with tempfile.TemporaryDirectory() as folder:
   exe=Path(folder)/'languages'
   subprocess.run(['c++','-std=c++17','-I'+str(ROOT/'frontend'),str(ROOT/'tests/test_all_languages.cpp'),'-o',str(exe)],check=True)
   subprocess.run([str(exe)],check=True)
