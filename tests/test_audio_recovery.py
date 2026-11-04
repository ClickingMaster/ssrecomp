from pathlib import Path
import shutil,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
class AudioRecoveryTests(unittest.TestCase):
 @unittest.skipUnless(shutil.which('c++'),'C++ compiler required')
 def test_speed_change_recovery(self):
  with tempfile.TemporaryDirectory() as folder:
   exe=Path(folder)/'audio'
   subprocess.run(['c++','-std=c++17','-O2','-I'+str(ROOT/'frontend'),str(ROOT/'tests/test_audio_recovery.cpp'),'-o',str(exe)],check=True)
   subprocess.run([str(exe)],check=True)
