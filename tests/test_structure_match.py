"""Synthetic regional fields and relative dispatch tables; no commercial bytes."""
import importlib.util
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/regions'))
AVAILABLE=importlib.util.find_spec('capstone') is not None
if AVAILABLE:
    import structure_match as m
    from test_callback_match import callback

@unittest.skipUnless(AVAILABLE,'Optional Capstone 5.x dependency required')
class StructureTests(unittest.TestCase):
    def setUp(self):
        self.old=0x02001000;self.new=0x02002000;self.targets=[0x02006000,0x02006100]
        a=callback(self.old,[0x02003000,0x02003100]);b=callback(self.new,self.targets)
        self.ref=a[:-2]+struct.pack('<H',0x84c8)+a[-2:]
        self.body=b[:-2]+struct.pack('<H',0x8548)+b[-2:]
    def match(self,target,fields=None,targets=None):
        return m.find_reviewed_callback(self.ref,self.old,target,self.new,'thumb',targets or self.targets,{42:38} if fields is None else fields)
    def test_declared_field_and_complete_calls(self):
        self.assertEqual(self.match(self.body),self.new)
    def test_undeclared_field_rejected(self):
        self.assertIsNone(self.match(self.body,{}))
    def test_wrong_call_rejected(self):
        self.assertIsNone(self.match(self.body,targets=[self.targets[0],0x02007000]))
    def test_insufficient_anchors_rejected(self):
        self.assertIsNone(self.match(self.body,targets=[self.targets[0],None]))
    def test_ambiguity_rejected(self):
        a=callback(self.new+len(self.body),self.targets)
        duplicate=a[:-2]+struct.pack('<H',0x8548)+a[-2:]
        self.assertIsNone(self.match(self.body+duplicate))
    def dispatch(self):
        a=bytearray(64);struct.pack_into('<hh',a,4,16,32)
        for offset,word in [(16,0x2001),(32,0x2101)]:
            for n in range(6):struct.pack_into('<H',a,offset+n*2,word)
        return a
    def test_all_dispatch_handlers_checked(self):
        a=self.dispatch();self.assertTrue(m.dispatch_matches(a,self.old,a,self.new,(4,2,0),{},'thumb'))
        b=bytearray(a);struct.pack_into('<h',b,4,32)
        self.assertFalse(m.dispatch_matches(a,self.old,b,self.new,(4,2,0),{},'thumb'))
    def test_dispatch_outside_body_rejected(self):
        a=self.dispatch();b=bytearray(a);struct.pack_into('<h',b,4,30000)
        self.assertFalse(m.dispatch_matches(a,self.old,b,self.new,(4,2,0),{},'thumb'))
