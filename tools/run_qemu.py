#!/usr/bin/env python3
"""Run a boot-resolution-locked development VM with a private, opt-in paste endpoint."""
import argparse
import fcntl
import json
import os
from pathlib import Path
import platform
import shlex
import shutil
import subprocess
import tempfile
import sys
import threading
from qemu_output import relay


ROOT=Path(__file__).resolve().parent.parent

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,required=True);p.add_argument('--qemu',default='qemu-system-x86_64')
    p.add_argument('--fullscreen',choices=('on','off'),default='on');p.add_argument('--audio',default='none')
    p.add_argument('--display',default='');p.add_argument('--headless',action='store_true')
    a=p.parse_args();out=a.out.resolve()
    config=json.loads((out/'display.json').read_text())
    # The immutable boot config and VGA EDID already carry the preflight mode.
    # No runtime window observer: guest modes cannot feed back into host geometry.
    # Single owner per output tree. A second invocation never unlinks a live endpoint.
    with (out/'run.lock').open('w') as lock:
        try:fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        except BlockingIOError:raise SystemExit('This output tree already has a running VM.')
        runtime=Path(tempfile.mkdtemp(prefix='aurora-vm-'));runtime.chmod(0o700)
        socket=runtime/'qmp.sock';state=out/'run.json'
        previous_umask=os.umask(0o077)
        process=None
        stderr_thread=None
        diagnostics=None
        try:
            display='none' if a.headless else ('cocoa' if platform.system()=='Darwin' else 'gtk')+f',zoom-to-fit=on,full-screen={a.fullscreen}'
            if not a.headless and platform.system()=='Darwin':display+=',left-command-key=off'
            command=[a.qemu,'-machine','q35,pcspk-audiodev=audio','-m','512M','-cpu','max',
                     '-audiodev',f'{a.audio},id=audio','-device','AC97,audiodev=audio','-cdrom',str(out/'aurora.iso'),'-boot','d',
                     '-vga','std','-global','VGA.vgamem_mb=64','-global',f'VGA.xres={config["width"]}',
                     '-global',f'VGA.yres={config["height"]}','-device','virtio-tablet-pci',
                     '-serial',f'file:{out / "serial.log"}',
                     '-chardev',f'socket,id=hostresize,path={runtime / "resize.sock"},server=on,wait=off',
                     '-serial','chardev:hostresize','-no-reboot','-qmp',f'unix:{socket},server=on,wait=off']
            command+=shlex.split(a.display) if a.display and not a.headless else ['-display',display]
            print(f'Display {config["width"]}x{config["height"]}, UI scale {config["scale"]}x; {config["source"]}',flush=True)
            print('Resolution locked for this boot. Window resizing only scales the view; make resize is explicit.',flush=True)
            print('Host paste: copy a single line, focus the guest field, then run make paste (does not press Enter).',flush=True)
            diagnostics=(out/'qemu.log').open('w')
            process=subprocess.Popen(command,cwd=ROOT,stderr=subprocess.PIPE,text=True,errors='replace')
            stderr_thread=threading.Thread(target=relay,args=(process.stderr,diagnostics,sys.stderr),daemon=True)
            stderr_thread.start()
            state.write_text(json.dumps({'pid':process.pid,'qmp':str(socket),'resize':str(runtime/'resize.sock'),'runtime':str(runtime)})+'\n');state.chmod(0o600)
            try:return process.wait()
            except KeyboardInterrupt:return 130
        finally:
            if process and process.poll() is None:
                process.terminate()
                try:process.wait(timeout=3)
                except subprocess.TimeoutExpired:process.kill();process.wait()
            if stderr_thread:stderr_thread.join(timeout=3)
            if diagnostics:diagnostics.close()
            state.unlink(missing_ok=True);shutil.rmtree(runtime);os.umask(previous_umask)

if __name__=='__main__':
    try:raise SystemExit(main())
    except (OSError,ValueError) as e:raise SystemExit(f'Cannot start VM: {e}')
