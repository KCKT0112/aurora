#!/usr/bin/env python3
"""Explicitly request one guest mode change. The launcher never calls this automatically."""
import argparse,json,os,socket
from pathlib import Path
from display_mode import validate_mode
from host_resize import packet
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,default=Path('out/x86_64'));p.add_argument('--mode',required=True);a=p.parse_args()
try:
    w,h=validate_mode(a.mode);state=a.out/'run.json'
    if state.stat().st_uid!=os.getuid() or state.stat().st_mode&0o077:raise ValueError('unsafe state')
    info=json.loads(state.read_text());endpoint=Path(info['resize'])
    if endpoint.parent.stat().st_uid!=os.getuid() or endpoint.parent.stat().st_mode&0o077:raise ValueError('unsafe endpoint')
    with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as connection:
        connection.settimeout(3);connection.connect(str(endpoint));connection.sendall(packet(w,h))
    print(f'Requested {w}x{h} once. No automatic mode tracking is active.')
except (OSError,ValueError,KeyError) as e:raise SystemExit(f'Cannot resize VM: {e}')
