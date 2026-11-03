"""Synthetic instruction and data fixtures, never commercial game content."""
import importlib.util
from pathlib import Path
import struct
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('match_functions', ROOT/'tools/regions/match_functions.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class RegionalMatchingTests(unittest.TestCase):
    def test_missing_module_cannot_silently_drop_a_required_hook(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            frontend = Path(tmp) / 'frontend.h'
            frontend.write_text('"CriticalHook"')
            function = {'addr': 0x2000000, 'isa': 'thumb', 'name': 'CriticalHook', 'module': 'ov7'}
            with patch.object(module, 'images', side_effect=[('TEST', {'ov7': (0x2000000, bytes(32))}), ('DEST', {})]), \
                 patch.object(module, 'load_functions', return_value={'ov7': [function]}):
                result = module.match('source', 'target', 'table', [frontend])
            self.assertEqual(result['missing'], [function])
            self.assertEqual(result['functions'], [])

    def test_unique_thumb_function_after_relocation(self):
        code = struct.pack('<8H', 0xb510, 0x2001, 0x2102, 0x2203, 0x2304, 0x2405, 0xbd10, 0x46c0)
        target = bytes(32) + code + bytes(64)
        self.assertEqual(module.unique_match(code, target, 0, 'thumb'), (32, 16))
        self.assertIsNone(module.unique_match(code, code+bytes(16)+code, 0, 'thumb'))

    def test_unique_function_outside_the_original_window(self):
        code = struct.pack('<8H', 0xb510, 0x2001, 0x2102, 0x2203, 0x2304, 0x2405, 0xbd10, 0x46c0)
        self.assertEqual(module.unique_match(code, bytes(4096)+code+bytes(64), 0, 'thumb'), (4096,16))
        self.assertIsNone(module.unique_match(code, bytes(4096)+code+bytes(16)+code, 0, 'thumb'))

    def test_application_pointer_slot_requires_two_peers_and_unique_target(self):
        base=0x02200000
        funcs=[{'addr':base+i*64,'isa':'thumb','module':'ov194','name':str(i)} for i in range(3)]
        hits={0:(base+256,'unique-prefix-96'),2:(base+384,'unique-prefix-96')}
        table=struct.pack('<4I',base+1,base+65,base+129,194)
        newtable=struct.pack('<4I',base+257,base+321,base+385,194)
        source={'main':(0x02000000,bytes(64)+table+bytes(96)),'ov194':(base,bytes(512))}
        target={'main':(0x02000000,bytes(64)+newtable+bytes(96)),'ov194':(base,bytes(512))}
        self.assertEqual(module.pointer_slot_match(funcs[1],funcs,hits,source,target),base+320)
        self.assertIsNone(module.pointer_slot_match(funcs[1],funcs,{0:hits[0]},source,target))
        alternate=struct.pack('<4I',base+257,base+337,base+385,194)
        target['main']=(0x02000000,bytes(64)+newtable+bytes(96)+alternate+bytes(96))
        self.assertIsNone(module.pointer_slot_match(funcs[1],funcs,hits,source,target))

    def test_complete_caller_pointer_is_unique_or_unresolved(self):
        base=0x02200000
        f={'addr':base+64,'isa':'thumb','module':'ov1','name':'ChangedMain'}
        caller={'addr':0x02000000,'isa':'thumb','module':'main','name':'Caller'}
        code=struct.pack('<4H',0xb510,0x2001,0x2102,0xbd10)
        body=code+struct.pack('<II',base+65,0x12345678)
        newbody=code+struct.pack('<II',base+129,0x12345678)
        source={'main':(0x02000000,body),'ov1':(base,bytes(512))}
        target={'main':(0x02000000,bytes(32)+newbody),'ov1':(base,bytes(512))}
        self.assertEqual(module.literal_reference_match(f,{'main':[caller]},source,target),base+128)
        target['main']=(0x02000000,bytes(32)+newbody+bytes(32)+newbody)
        self.assertIsNone(module.literal_reference_match(f,{'main':[caller]},source,target))

    def test_data_tables_are_unique_or_rejected(self):
        block = struct.pack('<8I', *range(1, 9))
        self.assertEqual(module.unique_data_address((0x2000000, block), (0x2000100, bytes(16)+block), 0x2000000, len(block)), 0x2000110)
        with self.assertRaisesRegex(ValueError, 'multiple'):
            module.unique_data_address((0x2000000, block), (0x2000100, block+block), 0x2000000, len(block))

    def test_black_white_tables_have_the_weathers(self):
        # main.cpp looks these up by names it puts together (bw2.weatherObj).
        for code in ('IRBF', 'IRAF', 'IREF', 'IRDF'):
            names = {line.split()[-1] for line in (ROOT/'tools/regions'/(code + '.txt')).read_text().splitlines()
                     if line and not line.startswith('#')}
            self.assertEqual(module.COMPOSED - names, set(), code)
