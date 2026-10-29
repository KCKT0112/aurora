#!/usr/bin/env python3
"""Provision private salted account verifiers; never put plaintext passwords in an ISO."""
import argparse
import getpass
import hashlib
import json
import os
from pathlib import Path
import secrets
import struct

ROOT = Path(__file__).resolve().parent.parent
LOCAL = ROOT / '.local'
ACCOUNTS = [('root', '/root', 0), ('user', '/home/user', 1000), ('guest', '/home/guest', 1001)]
ITERATIONS = 100000
RECORD = struct.Struct('<16s32sII16s32s')

def private_write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.parent.chmod(0o700)
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(descriptor, 'w') as stream:
        stream.write(data)
    path.chmod(0o600)

def record(name, home, uid, password):
    salt = secrets.token_bytes(16)
    verifier = hashlib.pbkdf2_hmac('sha256', password.encode(), salt, ITERATIONS)
    return dict(name=name, home=home, uid=uid, gid=uid, salt=salt.hex(), verifier=verifier.hex())

def provision():
    path = LOCAL / 'accounts.json'
    if path.exists():
        return json.loads(path.read_text())
    data = {'version': 1, 'iterations': ITERATIONS, 'accounts': []}
    credentials = ['Aurora local development credentials. Keep this file private.']
    for name, home, uid in ACCOUNTS:
        password = ''.join(secrets.choice('abcdefghjkmnpqrstuvwxyz23456789') for _ in range(16))
        data['accounts'].append(record(name, home, uid, password))
        credentials.append(f'{name}: {password}')
    private_write(path, json.dumps(data, indent=2) + '\n')
    private_write(LOCAL / 'initial-passwords.txt', '\n'.join(credentials) + '\n')
    print('Local account passwords generated in .local/initial-passwords.txt (not included in image).')
    return data

def database(testing=False):
    if testing:
        data = {'iterations': ITERATIONS, 'accounts': [record(n,h,u,n+'-test') for n,h,u in ACCOUNTS]}
    else:
        data = provision()
    if data['iterations'] != ITERATIONS or len(data['accounts']) != 3:
        raise ValueError('invalid account database')
    output = bytearray(struct.pack('<4I', 0x41555448, 1, 3, ITERATIONS))
    for item, expected in zip(data['accounts'], ACCOUNTS):
        name, home, uid = expected
        if (item['name'], item['home'], item['uid'], item['gid']) != (name,home,uid,uid):
            raise ValueError('account identity mismatch')
        salt, verifier = bytes.fromhex(item['salt']), bytes.fromhex(item['verifier'])
        if len(salt)!=16 or len(verifier)!=32:
            raise ValueError('invalid verifier')
        output.extend(RECORD.pack(name.encode(), home.encode(), uid, uid, salt, verifier))
    return bytes(output)

if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--test-output', type=Path, help='write test-only verifier fixture')
    parser.add_argument('--set-password', choices=[a[0] for a in ACCOUNTS])
    args=parser.parse_args()
    if args.test_output:
        args.test_output.parent.mkdir(parents=True, exist_ok=True)
        args.test_output.write_bytes(database(True))
        raise SystemExit(0)
    data=provision()
    if args.set_password:
        password=getpass.getpass('New password (12–63 printable ASCII characters): ')
        if not 12<=len(password)<=63 or any(ord(c)<32 or ord(c)>126 for c in password):
            raise SystemExit('Password does not meet input constraints.')
        if password!=getpass.getpass('Repeat password: '):
            raise SystemExit('Passwords differ.')
        for i,(name,home,uid) in enumerate(ACCOUNTS):
            if name==args.set_password:data['accounts'][i]=record(name,home,uid,password)
        private_write(LOCAL/'accounts.json',json.dumps(data,indent=2)+'\n')
        print('Verifier updated. Rebuild the image; initial-passwords.txt may contain an old password.')
    else:
        print('Account verifiers ready. Initial credentials: .local/initial-passwords.txt')
