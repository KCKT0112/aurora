import struct
import unittest
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parents[1]/'tools'))
from host_resize import packet,fit_mode
class ResizeTests(unittest.TestCase):
    def test_alignment_and_limit(self):
        self.assertEqual(fit_mode(1925,1081),(1920,1080))
        self.assertEqual(fit_mode(7680,4320),(3840,2160))
        self.assertIsNone(fit_mode(800,600))
    def test_wire_format(self):
        raw=packet(2880,1800,123)
        self.assertEqual(len(raw),12)
        magic,w,h,seq,check=struct.unpack('<4s4H',raw)
        self.assertEqual(magic,b'ARZ1');self.assertEqual(check,w^h^seq^0xa75a)
        with self.assertRaises(ValueError):packet(99999,99999)

