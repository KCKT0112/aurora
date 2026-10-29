#!/usr/bin/env python3
"""Exercise the live host-resize path, HiDPI painting, paste and cursor composition."""
import argparse
from pathlib import Path
import socket
import time
import struct
import smoke_x64 as s
from host_resize import packet
from paste import paste


def run(iso,qemu):
    s.ISO=iso.resolve();s.OUT=s.ISO.parent/'graphics-smoke';s.OUT.mkdir(exist_ok=True)
    s.SERIAL,s.MONITOR,s.QMP,s.QEMU_LOG=[s.OUT/n for n in ('serial.log','monitor.sock','qmp.sock','qemu.log')]
    resize=s.OUT/'resize.sock'
    for path in (s.SERIAL,s.MONITOR,s.QMP,s.QEMU_LOG,resize):path.unlink(missing_ok=True)
    process=s.start_qemu(qemu,resize.relative_to(s.ROOT));monitor=qmp=transport=None
    try:
        s.wait_for_log('authd: ready',timeout=30);s.wait_for_log('display: login screen',timeout=30)
        monitor=s.Monitor(s.MONITOR);qmp=s.Qmp(s.QMP)
        transport=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM);transport.connect(str(resize))
        sequence=0
        def mode(w,h,name):
            nonlocal sequence
            sequence+=1;transport.sendall(packet(w,h,sequence))
            s.wait_for_log(f'display: mode {w}x{h}',timeout=30)
            # Full high-density redraw must finish before examining the screenshot.
            time.sleep(0.5)
            shot=s.OUT/(name+'.ppm');monitor.screendump(shot)
            actual=s.parse_ppm(shot)
            if actual!=(w,h):raise s.SmokeError(f'expected native {w}x{h}, got {actual}')
            qmp.width=w;qmp.height=h
            print(f'  live mode: {w}x{h}')
        # Text remains private and editable across a display mode transition.
        paste(qmp,'user-')
        mode(1920,1080,'login-1920')
        mode(2880,1800,'login-2880')
        paste(qmp,'test');monitor.send('sendkey ret',settle=0.2)
        s.wait_for_log('authd: login accepted',timeout=30)
        s.wait_for_log('services: transactional writes and policy checks passed',timeout=30)
        monitor.screendump(s.OUT/'desktop-2880.ppm')
        # Interleave pointer motion with real window dragging; every sample must contain a cursor.
        qmp.pointer(500,210);time.sleep(0.2);qmp.button(True);time.sleep(0.1)
        for i in range(12):
            x=500+i*12;y=210+i*4
            qmp.pointer(x,y);time.sleep(0.04)
            shot=s.OUT/f'drag-{i:02d}.ppm';monitor.screendump(shot)
            w,h,pixels=s.read_ppm(shot)
            count=0
            # Include both queued and last-presented coordinates, excluding window text colors.
            for row in range(200,310):
                for col in range(480,680):
                    at=(row*w+col)*3
                    if pixels[at:at+3]==bytes((28,42,38)):count+=1
            if count<30:raise s.SmokeError(f'cursor disappeared in drag frame {i}: {count} pixels')
        qmp.button(False)
        mode(1280,800,'desktop-1280')
        mode(3840,2160,'desktop-3840')
        mode(1440,900,'desktop-1440')
        # Invalid dimensions are not allowed to program the display controller.
        malformed=struct.pack('<4s4H',b'ARZ1',65528,65534,99,(65528^65534^99^0xa75a)&0xffff)
        transport.sendall(malformed);time.sleep(0.3)
        monitor.screendump(s.OUT/'invalid-rejected.ppm')
        if s.parse_ppm(s.OUT/'invalid-rejected.ppm')!=(1440,900):raise s.SmokeError('invalid mode accepted')
        if any(marker in s.serial_text() for marker in ('[fault]','PANIC','KERNEL EXCEPTION','FAILED')):
            raise s.SmokeError('fault in graphics integration')
        print('GRAPHICS TEST PASSED: live modes, preserved password, native HiDPI and 12 cursor-present drag samples')
    finally:
        if transport:transport.close()
        s.stop_qemu(process,monitor)
        if qmp:qmp.close()
        if monitor:monitor.close()
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--iso',type=Path,required=True);p.add_argument('--qemu',default='qemu-system-x86_64');a=p.parse_args()
    run(a.iso,a.qemu)
