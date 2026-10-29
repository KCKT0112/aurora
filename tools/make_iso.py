#!/usr/bin/env python3
"""Assemble the boot image from separately built kernel and system components."""
import argparse
from pathlib import Path
import shutil
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--out', type=Path, required=True)
p.add_argument('--limine-share', type=Path, required=True)
p.add_argument('--limine', default='limine')
a = p.parse_args()
stage = a.out / 'iso'
if stage.exists():
    shutil.rmtree(stage)
shutil.copytree(a.out / 'rootfs', stage)
for name in ('boot/limine', 'EFI/BOOT', 'system/kernel', 'system/boot'):
    (stage / name).mkdir(parents=True, exist_ok=True)
shutil.copyfile(a.out / 'aurora.elf', stage / 'system/kernel/aurora.elf')
shutil.copyfile(a.out / 'accounts.bin', stage / 'system/boot/accounts.bin')
shutil.copyfile(a.out / 'initramfs.bin', stage / 'system/boot/initramfs.bin')
shutil.copyfile(a.out / 'limine.conf', stage / 'boot/limine/limine.conf')
for name in ('limine-bios.sys', 'limine-bios-cd.bin', 'limine-uefi-cd.bin'):
    shutil.copyfile(a.limine_share / name, stage / 'boot/limine' / name)
shutil.copyfile(a.limine_share / 'BOOTX64.EFI', stage / 'EFI/BOOT/BOOTX64.EFI')
iso = a.out / 'aurora.iso'
subprocess.run(['xorriso', '-as', 'mkisofs', '-R', '-r', '-J',
    '-b', 'boot/limine/limine-bios-cd.bin', '-no-emul-boot', '-boot-load-size', '4',
    '-boot-info-table', '-hfsplus', '-apm-block-size', '2048',
    '--efi-boot', 'boot/limine/limine-uefi-cd.bin', '-efi-boot-part',
    '--efi-boot-image', '--protective-msdos-label', str(stage), '-o', str(iso)], check=True)
subprocess.run([a.limine, 'bios-install', str(iso)], check=True)
print(f'Image: {iso}')
