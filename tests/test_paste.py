import sys
from pathlib import Path
import unittest
sys.path.insert(0,str(Path(__file__).parents[1]/'tools'))
from paste import key_sequence,paste
class PasteTests(unittest.TestCase):
    def test_all_printable_ascii_and_symbols(self):
        for n in range(32,127):self.assertTrue(key_sequence(chr(n)))
        self.assertEqual(key_sequence('aA!_ '),[['a'],['shift','a'],['shift','1'],['shift','minus'],['spc']])
    def test_reject_before_sending(self):
        class Qmp:
            calls=[]
            def command(self,*args):self.calls.append(args)
        for text in ['', 'x\n', '\t', 'a'*64, '\x1b','中文']:
            q=Qmp()
            with self.assertRaises(ValueError):paste(q,text,0)
            self.assertEqual(q.calls,[])
    def test_never_submits(self):
        class Qmp:
            def __init__(self):self.calls=[]
            def command(self,*args):self.calls.append(args)
        q=Qmp();paste(q,'user-test',0)
        self.assertEqual(len(q.calls),9)
        self.assertNotIn('ret',str(q.calls))
