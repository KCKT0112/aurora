#!/usr/bin/env python3
"""Compile the trusted boot policy; manifests cannot grant themselves authority."""
import argparse
import json
from pathlib import Path

IDS = {'display':'IMAGE_DISPLAY', 'input':'IMAGE_INPUT', 'taskmgr':'IMAGE_TASK',
       'vfsd':'IMAGE_VFS', 'files':'IMAGE_FILES', 'init':'IMAGE_INIT',
       'terminal':'IMAGE_TERMINAL', 'audiod':'IMAGE_AUDIO', 'authd':'IMAGE_AUTH'}
CAPS = {'spawn','framebuffer','input','timer','inspect','task_control','fs_read','fs_write',
        'audio','audio_device','service','session'}
def generate(spec):
    if spec['abi_version'] != 4 or spec['layout_version'] != 1:
        raise ValueError('unsupported ABI or layout')
    lines = ['/* Generated from config/system.json. Do not edit. */']
    seen = set()
    for item in spec['programs']:
        name = item['name']
        if name not in IDS or name in seen:
            raise ValueError('unknown or duplicate image')
        seen.add(name)
        grants = item['grants']
        if len(grants) != len(set(grants)) or set(grants) - CAPS:
            raise ValueError('unknown or duplicate grant')
        path = Path(item['path'])
        if path.is_absolute() or '..' in path.parts or path.parts[0] != 'system':
            raise ValueError('program must be inside /system')
        if ('spawn' in grants) != (name == 'init'):
            raise ValueError('only init may spawn boot programs')
        identity = item.get('identity')
        if identity not in ('root', 'session'):
            raise ValueError('explicit identity required')
        if ('service' in grants) != (identity == 'root'):
            raise ValueError('system services must use root identity')
        if ('session' in grants) != (name == 'authd'):
            raise ValueError('only authd can establish sessions')
        caps = ' | '.join('CAP_' + c.upper() for c in grants) or '0'
        lines.append(f'[{IDS[name]}] = {{ "{name}", {caps}, {str(identity == "session").lower()} }},')
    if seen != set(IDS):
        raise ValueError('missing required boot program')
    lines.append('[IMAGE_SECURITY_TEST] = { "security-test", 0, true },')
    lines.append('[IMAGE_SERVICE_TEST] = { "service-test", CAP_FS_READ | CAP_FS_WRITE | CAP_TASK_CONTROL, true },')
    return '\n'.join(lines) + '\n'

if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--config', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a=p.parse_args()
    data=generate(json.loads(a.config.read_text()))
    a.output.parent.mkdir(parents=True, exist_ok=True)
    if not a.output.exists() or a.output.read_text() != data:
        a.output.write_text(data)
