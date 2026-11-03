import json
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]

class JapanesePortableTests(unittest.TestCase):
    def test_absent_application_probes_are_disabled_before_watch_registration(self):
        for name in ('heartgold-jp.json','soulsilver-jp.json'):
            p=json.loads((ROOT/'tools/soulsilver'/name).read_text())
            self.assertEqual(len(p['disabled_functions']),3)
            for f in p['disabled_functions']:
                self.assertTrue(f['name'].startswith('VoltorbFlip_'))
                section='voltorb_flip';address=f['addr']
                matches=[delta for start,end,delta in p['shifts'][section] if start<=address<end]
                self.assertEqual(len(matches),1)
                remapped=(address+matches[0])&0xffffffff
                self.assertEqual(remapped,0)
                self.assertFalse(0x02000000<=remapped<0x02400000)
        # Existing runtime ignores addresses outside its watch and probe ranges.
        runtime=(ROOT/'runtime/recomp_rt.cpp').read_text()
        self.assertIn('addr < kWatchBase || addr >= kWatchEnd',runtime)
        self.assertIn('addr < kMainBase || addr >= kMainEnd',runtime)
