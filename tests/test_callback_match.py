"""Synthetic Thumb callbacks, never commercial game instructions."""
import importlib.util
from pathlib import Path
import struct
import unittest
ROOT=Path(__file__).resolve().parents[1]
CAPSTONE_AVAILABLE=importlib.util.find_spec('capstone') is not None
m=None
if CAPSTONE_AVAILABLE:
    spec=importlib.util.spec_from_file_location('callback_match',ROOT/'tools/regions/callback_match.py')
    m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

def callback(address, targets, constant=None):
    body=struct.pack('<H',0xb510)
    if constant is not None:body+=struct.pack('<H',0x2000|constant)
    for target in targets:
        delta=target-(address+len(body)+4)
        body+=struct.pack('<HH',0xf000|((delta>>12)&0x7ff),0xf800|((delta>>1)&0x7ff))
    return body+struct.pack('<H',0xbd10)

@unittest.skipUnless(CAPSTONE_AVAILABLE,'Optional Capstone 5.x matching dependency required')
class CallbackTests(unittest.TestCase):
    def setUp(self):
        self.old=0x02001000;self.base=0x02002000;self.targets=[0x02006000,0x02006100,0x02006200]
        self.reference=callback(self.old,[0x02003000,0x02003100,0x02003200])
        self.target=bytes(32)+callback(self.base+32,self.targets)
    def test_unique_complete_callback(self):
        self.assertEqual(m.find_callback(self.reference,self.old,self.target,self.base,'thumb',self.targets),self.base+32)
    def test_duplicate_is_unresolved(self):
        target=self.target+bytes(32)+callback(self.base+len(self.target)+32,self.targets)
        self.assertIsNone(m.find_callback(self.reference,self.old,target,self.base,'thumb',self.targets))
    def test_changed_call_target_is_rejected(self):
        self.assertIsNone(m.find_callback(self.reference,self.old,self.target,self.base,'thumb',self.targets[:2]+[0x02007000]))
    def test_too_few_independent_anchors_are_rejected(self):
        self.assertIsNone(m.find_callback(self.reference,self.old,self.target,self.base,'thumb',[self.targets[0],None,self.targets[2]]))
    def test_regional_constant_change_still_requires_call_anchors(self):
        reference=callback(self.old,[0x02003000,0x02003100,0x02003200],1)
        target=bytes(32)+callback(self.base+32,self.targets,2)
        self.assertEqual(m.find_callback(reference,self.old,target,self.base,'thumb',self.targets),self.base+32)

    def test_incomplete_body_is_rejected(self):
        self.assertIsNone(m.find_callback(self.reference[:-2],self.old,self.target,self.base,'thumb',self.targets))
    def test_changed_register_structure_is_rejected(self):
        changed=self.target[:32]+struct.pack('<H',0xb520)+self.target[34:]
        self.assertIsNone(m.find_callback(self.reference,self.old,changed,self.base,'thumb',self.targets))
if __name__=='__main__':unittest.main()
