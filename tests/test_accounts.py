import hashlib
import importlib.util
from pathlib import Path
import struct
import unittest
root=Path(__file__).parents[1]
spec=importlib.util.spec_from_file_location('accounts',root/'tools/accounts.py')
accounts=importlib.util.module_from_spec(spec);spec.loader.exec_module(accounts)
class AccountsTests(unittest.TestCase):
    def test_separate_salted_verifiers(self):
        data=accounts.database(True)
        self.assertEqual(len(data),16+3*accounts.RECORD.size)
        salts=[]
        for i,(name,home,uid) in enumerate(accounts.ACCOUNTS):
            n,h,u,g,salt,digest=accounts.RECORD.unpack_from(data,16+i*accounts.RECORD.size)
            self.assertEqual((u,g),(uid,uid))
            self.assertEqual(digest,hashlib.pbkdf2_hmac('sha256',(name+'-test').encode(),salt,accounts.ITERATIONS))
            self.assertNotIn((name+'-test').encode(),data)
            salts.append(salt)
        self.assertEqual(len(set(salts)),3)
    def test_salt_changes_between_images(self):
        self.assertNotEqual(accounts.database(True),accounts.database(True))
