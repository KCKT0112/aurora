#!/usr/bin/env python3
"""Verify the real launcher/paste path without reading the user's host clipboard."""
import argparse
from pathlib import Path
import subprocess
import time
import json
from qmp_client import QmpClient
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
root=Path(__file__).resolve().parent.parent;out=a.out.resolve()
log=out/'serial.log';log.unlink(missing_ok=True)
process=subprocess.Popen(['python3','tools/run_qemu.py','--out',str(out),'--headless'],cwd=root)
try:
    deadline=time.monotonic()+30
    while time.monotonic()<deadline:
        if log.exists() and 'display: login screen' in log.read_text(errors='replace'):break
        if process.poll() is not None:raise RuntimeError('launcher exited')
        time.sleep(0.1)
    else:raise RuntimeError('boot timeout')
    state=json.loads((out/'run.json').read_text());runtime=Path(state['runtime'])
    assert runtime.stat().st_mode&0o077==0
    result=subprocess.run(['python3','tools/paste.py','--out',str(out),'--stdin'],input='user-test',text=True,capture_output=True,cwd=root,check=True)
    assert 'user-test' not in result.stdout+result.stderr
    assert 'authd: login accepted' not in log.read_text()
    rejected=subprocess.run(['python3','tools/paste.py','--out',str(out),'--stdin'],input='bad\n',text=True,capture_output=True,cwd=root)
    assert rejected.returncode and 'bad\n' not in rejected.stdout+rejected.stderr
    with QmpClient(state['qmp']) as qmp:
        qmp.command('send-key',{'keys':[{'type':'qcode','data':'ret'}],'hold-time':20})
        deadline=time.monotonic()+30
        while time.monotonic()<deadline:
            if 'authd: login accepted' in log.read_text(errors='replace'):break
            time.sleep(0.1)
        else:raise RuntimeError('paste login failed')
        qmp.command('quit')
    process.wait(timeout=5)
    assert not (out/'run.json').exists() and not runtime.exists()
    print('RUNNER TEST PASSED: private endpoint, stdin paste, no automatic Enter, newline rejected, cleanup')
finally:
    if process.poll() is None:
        process.send_signal(2)
        try:process.wait(timeout=5)
        except subprocess.TimeoutExpired:process.kill();process.wait()
