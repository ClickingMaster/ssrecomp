import importlib.util
from pathlib import Path
import struct
import unittest
spec=importlib.util.spec_from_file_location('pointer_match',Path(__file__).resolve().parents[1]/'tools/regions/pointer_match.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

class PointerTests(unittest.TestCase):
    def setUp(self):
        self.old=0x06800001;self.new=0x06801001
        self.ref=b'abcdefgh'+struct.pack('<I',self.old)+b'ijkl'
        self.body=b'abcdefgh'+struct.pack('<I',self.new)+b'ijkl'
    def test_unique_full_wrapper(self):
        self.assertEqual(m.find_pointer_wrapper(self.ref,bytes(16)+self.body,0x02000000,{self.old:self.new}),0x02000010)
    def test_duplicate_rejected(self):
        self.assertIsNone(m.find_pointer_wrapper(self.ref,self.body*2,0x02000000,{self.old:self.new}))
    def test_other_byte_change_rejected(self):
        self.assertIsNone(m.find_pointer_wrapper(self.ref,b'X'+self.body[1:],0x02000000,{self.old:self.new}))
    def test_unproven_pointer_rejected(self):
        self.assertIsNone(m.find_pointer_wrapper(self.ref,self.body,0x02000000,{0x06800005:self.new}))
    def test_truncated_rejected(self):
        self.assertIsNone(m.find_pointer_wrapper(self.ref,self.body[:-1],0x02000000,{self.old:self.new}))
    def test_unaligned_rejected(self):
        self.assertIsNone(m.find_pointer_wrapper(self.ref,b'X'+self.body,0x02000000,{self.old:self.new}))
