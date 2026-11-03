"""Package completeness checks use authored address metadata only."""
from pathlib import Path
import importlib.util
import json
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('package_profiles',ROOT/'tools/regions/package_profiles.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
class PackageProfileTests(unittest.TestCase):
    def test_missing_stale_and_complete_tables(self):
        entries=json.loads((ROOT/'tools/regions/profiles.json').read_text())['profiles']
        with tempfile.TemporaryDirectory() as tmp:
            folder=Path(tmp)/'funcs'
            self.assertTrue(module.audit(folder))
            self.assertEqual(module.audit(folder,copy=True),[])
            first=next(p for p in entries if p['kind']=='functions')
            (folder/(first['code']+'.txt')).write_text('stale metadata')
            self.assertIn(first['code']+': missing/stale function table',module.audit(folder))
            self.assertEqual(module.audit(folder,copy=True),[])
            portable=Path(tmp)/'rc_portable.cpp';portable.write_text('')
            self.assertTrue(module.audit(folder,portable=portable))
            portable.write_text('\n'.join('{"'+p['code']+'", "metadata"}' for p in entries if p['kind']=='portable'))
            self.assertEqual(module.audit(folder,portable=portable),[])
if __name__=='__main__':unittest.main()
