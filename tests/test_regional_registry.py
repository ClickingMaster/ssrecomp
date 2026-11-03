"""Version checks use synthetic ROM headers and bytes, never game content."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class RegistryTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('c++') and shutil.which('cc'),'C/C++ compilers required')
    def test_version_fingerprint_and_legacy_revision_behaviour(self):
        with tempfile.TemporaryDirectory() as folder:
            folder=Path(folder);source=folder/'registry.cpp'
            source.write_text(r'''#include "gamecode.h"
#include <cassert>
#include <vector>
int main() {
    using namespace RegionalProfiles;
    std::vector<uint8_t> rom(512,0); std::memcpy(rom.data()+12,"TEST",4);
    // SHA-1 of this synthetic fixture is supplied by the test runner.
    Profile p={"TEST","BASE","Fixture","fixture.sav",FIXTURE_SHA,0,512,false,0,0,0,0};
    assert(Check(p,rom.data(),rom.size())==nullptr);
    rom[30]=1; assert(Check(p,rom.data(),rom.size())!=nullptr); rom[30]=0;
    assert(Check(p,rom.data(),rom.size()-1)!=nullptr);
    rom[100]=1; assert(Check(p,rom.data(),rom.size())!=nullptr); rom[100]=0;
    rom[12]='X'; assert(Check(p,rom.data(),rom.size())!=nullptr);
    std::memcpy(rom.data()+12,"IRBI",4); assert(Check(rom.data(),16)!=nullptr);
    std::memcpy(rom.data()+12,"IPGE",4); rom[30]=10;
    assert(Check(rom.data(),rom.size())==nullptr);
    assert(BaseGameCode("IRBI")=="IRBO"); assert(BaseGameCode("IRBZ")=="IRBZ");
    for (const auto& profile: profiles) assert(BaseGameCode(profile.code)==profile.base);
}
''')
            import hashlib
            fixture=bytearray(512);fixture[12:16]=b'TEST';sha=hashlib.sha1(fixture).hexdigest()
            obj=folder/'sha.o';binary=folder/'registry'
            subprocess.run(['cc','-c',str(ROOT/'melonDS/src/sha1/sha1.c'),'-o',str(obj)],check=True,capture_output=True,text=True)
            subprocess.run(['c++','-std=c++17','-I'+str(ROOT/'frontend'),'-I'+str(ROOT/'melonDS/src'),'-DFIXTURE_SHA="'+sha+'"',str(source),str(obj),'-o',str(binary)],check=True,capture_output=True,text=True)
            subprocess.run([str(binary)],check=True,capture_output=True,text=True)

    def test_gate_precedes_loading_player_save(self):
        source=(ROOT/'frontend/main.cpp').read_text()
        self.assertLess(source.index('RegionalProfiles::Check(rom.data(),rom.size())'),source.index('std::vector<uint8_t> sav = ReadFile(opt.save);'))

    def test_generated_registry_is_current(self):
        import sys
        subprocess.run([sys.executable,str(ROOT/'tools/regions/generate_profiles.py'),'--check'],check=True,capture_output=True,text=True)

if __name__=='__main__':unittest.main()
