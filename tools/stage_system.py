#!/usr/bin/env python3
"""Stage the immutable system tree, user modules and Limine boot configuration."""
import argparse
import json
from pathlib import Path
import shutil
from mkfs import pack, validate
from policy import generate
from accounts import database
from display_mode import choose

ROOT = Path(__file__).resolve().parent.parent

def stage(out, testing, resolution="1440x900"):
    (width,height),source=choose(resolution,out/'host-window')
    spec = json.loads((ROOT / 'config/system.json').read_text())
    generate(spec)  # validate before touching the generated staging tree
    tree = out / 'rootfs'
    # Only remove this tool's generated staging directory.
    if tree.exists():
        shutil.rmtree(tree)
    shutil.copytree(ROOT / 'rootfs', tree, symlinks=True, ignore=shutil.ignore_patterns('.*'))
    programs = spec['programs'][:]
    if testing:
        programs.append({'name': 'security-test', 'path': 'system/tests/security-test.elf',
                         'grants': [], 'source': 'apps/security-test'})
    if testing:
        programs.append({'name':'service-test', 'path':'system/tests/service-test.elf',
                         'grants':['fs_read','fs_write','task_control'], 'source':'tests'})
    if testing:
        from wav_fixture import tone
        (tree/'system/share/test-tone.wav').write_bytes(tone())
        (tree/'system/share/test-mono.wav').write_bytes(tone(22050,8,1,.3))
        music = tree / 'home/user/Music'
        music.mkdir(parents=True, exist_ok=True)
        if not any(music.glob('*.wav')):
            (music / 'aurora-test.wav').write_bytes(tone(seconds=6))
    for item in programs:
        destination = tree / item['path']
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(out / 'user' / (item['name'] + '.elf'), destination)
        manifest = {**item, 'abi_version': spec['abi_version'], 'format': 1}
        destination.with_suffix('.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (out / 'accounts.bin').write_bytes(database(bool(testing)))
    (out / 'accounts.bin').chmod(0o600)
    data = pack(tree)
    count, size = validate(data)
    (out / 'initramfs.bin').write_bytes(data)
    boot = ['timeout: 1', 'quiet: yes', '', '/Aurora', '    protocol: limine',
            '    path: boot():/system/kernel/aurora.elf', f'    resolution: {width}x{height}x32',
            '    paging_mode: 4level']
    for item in programs:
        boot.extend(['    module_path: boot():/' + item['path'],
                     '    module_string: ' + item['name']])
    boot.extend(['    module_path: boot():/system/boot/accounts.bin', '    module_string: accounts'])
    boot.extend(['    module_path: boot():/system/boot/initramfs.bin',
                 '    module_string: initramfs'])
    (out / 'limine.conf').write_text('\n'.join(boot) + '\n')
    (out/'display.json').write_text(json.dumps({'width':width,'height':height,'scale':2 if width>=2560 and height>=1400 else 1,'source':source})+'\n')
    print(f'Display: {width}x{height} ({source})')
    print(f'System: {count} nodes, {size} data bytes, ABI {spec["abi_version"]}')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--test', type=int, choices=(0, 1), default=0)
    parser.add_argument('--resolution',default='1440x900')
    args = parser.parse_args()
    stage(args.out, args.test,args.resolution)
