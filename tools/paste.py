#!/usr/bin/env python3
"""Explicit host-to-guest single-line paste via QMP. No polling, echo, Enter or file logging."""
import argparse
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import time
from qmp_client import QmpClient,QmpError

UNSHIFTED={' ':'spc','-':'minus','=':'equal','[':'bracket_left',']':'bracket_right',
           ';':'semicolon',"'":'apostrophe','`':'grave_accent','\\':'backslash',',':'comma','.':'dot','/':'slash'}
SHIFTED=dict(zip('!@#$%^&*()_+{}:"~|<>?','1234567890-=[];\'`\\,./'))

def key_sequence(text):
    # Reject the whole payload before sending anything. Newlines can submit commands.
    if not text or len(text)>63 or any(ord(c)<32 or ord(c)>126 for c in text):
        raise ValueError('Paste requires 1–63 printable ASCII characters, with no newline or tab.')
    result=[]
    for char in text:
        shift=char.isupper() or char in SHIFTED
        base=SHIFTED.get(char,char.lower())
        key=UNSHIFTED.get(base,base)
        result.append((['shift'] if shift else [])+[key])
    return result

def paste(qmp,text,interval=0.065):
    sequence=key_sequence(text)
    for keys in sequence:
        qmp.command('send-key',{'keys':[{'type':'qcode','data':k} for k in keys],'hold-time':20})
        time.sleep(interval)

def clipboard():
    if platform.system()=='Darwin':command=['pbpaste']
    elif shutil.which('wl-paste'):command=['wl-paste','--no-newline']
    elif shutil.which('xclip'):command=['xclip','-selection','clipboard','-o']
    else:raise ValueError('No clipboard reader; use --stdin to pipe one line without a trailing newline.')
    output=subprocess.run(command,capture_output=True,timeout=5,check=True)
    return output.stdout.decode('utf-8')

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,default=Path('out/x86_64'));p.add_argument('--stdin',action='store_true')
    p.add_argument('--socket',type=Path,help='explicit QMP endpoint (tests)')
    a=p.parse_args()
    endpoint=a.socket
    if endpoint is None:
        state=a.out/'run.json'
        if not state.exists():raise ValueError('Start the VM with make run or make run-headless first.')
        if state.stat().st_uid!=os.getuid() or state.stat().st_mode&0o077:raise ValueError('Unsafe VM endpoint permissions.')
        info=json.loads(state.read_text());endpoint=Path(info['qmp'])
        if endpoint.parent.stat().st_uid!=os.getuid() or endpoint.parent.stat().st_mode&0o077:raise ValueError('Unsafe QMP directory permissions.')
    text=sys.stdin.read(65) if a.stdin else clipboard()
    key_sequence(text)
    with QmpClient(endpoint) as qmp:paste(qmp,text)
    text=''
    print('Paste sent to the focused guest field. Press Enter in the guest to submit.')
if __name__=='__main__':
    try:main()
    except (OSError,ValueError,subprocess.SubprocessError,QmpError):
        # Do not echo inputs, process arguments or QMP payloads on any error.
        print('Paste failed: use a running make run VM and 1–63 printable ASCII characters without newlines. No Enter is sent.',file=sys.stderr)
        raise SystemExit(1)
