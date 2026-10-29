#!/usr/bin/env python3
"""Verify real WAV playback through file grants, audiod conversion and AC97 DMA."""
import argparse
from pathlib import Path
import struct,time,wave,math
import smoke_x64 as s
from paste import paste

def read_capture(path):
    raw=bytearray(path.read_bytes())
    if raw[:4]!=b'RIFF' or raw[8:16]!=b'WAVEfmt ' or raw[36:40]!=b'data':raise s.SmokeError('unexpected WAV capture header')
    struct.pack_into('<I',raw,4,len(raw)-8);struct.pack_into('<I',raw,40,len(raw)-44);path.write_bytes(raw)
    with wave.open(str(path),'rb') as stream:
        if stream.getsampwidth()!=2 or stream.getnchannels()!=2 or stream.getframerate()!=48000:raise s.SmokeError('capture format mismatch')
        data=stream.readframes(stream.getnframes())
    samples=struct.unpack(f'<{len(data)//2}h',data)
    return samples[::2],samples[1::2]

def strongest_tone(samples,frequency):
    # Sliding analysis avoids silence before/after QEMU opens the voice.
    n=4800;best=0
    sine=[math.sin(2*math.pi*frequency*i/48000) for i in range(n)]
    cosine=[math.cos(2*math.pi*frequency*i/48000) for i in range(n)]
    for start in range(0,len(samples)-n+1,n):
        part=samples[start:start+n]
        real=sum(v*c for v,c in zip(part,cosine));imag=sum(v*c for v,c in zip(part,sine))
        best=max(best,2*math.hypot(real,imag)/n)
    return best

def run(iso,qemu,full=False):
    s.ISO=iso.resolve();s.OUT=s.ISO.parent/('audio-smoke-full' if full else 'audio-smoke');s.OUT.mkdir(exist_ok=True)
    music=sorted((s.ISO.parent/'rootfs/home/user/Music').glob('*.wav'))
    if not music:raise s.SmokeError('test image needs a Music WAV fixture')
    with wave.open(str(music[0]),'rb') as source:
        rate,bits,channels,frames=source.getframerate(),source.getsampwidth()*8,source.getnchannels(),source.getnframes()
    marker=f'audiod: WAV started {rate} Hz {bits}-bit channels={channels}'
    output_frames=(frames*48000+rate-1)//rate
    s.SERIAL,s.MONITOR,s.QMP,s.QEMU_LOG=[s.OUT/n for n in ('serial.log','monitor.sock','qmp.sock','qemu.log')]
    for path in (s.SERIAL,s.MONITOR,s.QMP,s.QEMU_LOG):path.unlink(missing_ok=True)
    process=s.start_qemu(qemu);monitor=qmp=None
    try:
        s.wait_for_log('[ac97] PCM ready',timeout=30);s.wait_for_log('display: login screen',timeout=30)
        monitor=s.Monitor(s.MONITOR);qmp=s.Qmp(s.QMP)
        paste(qmp,'user-test');monitor.send('sendkey ret',settle=.2)
        s.wait_for_log('services: transactional writes and policy checks passed',timeout=30)
        monitor.send('sendkey f2',settle=.3)
        qmp.type_text('play /system/share/test-tone.wav')
        s.wait_for_log('audiod: WAV started 48000 Hz 24-bit channels=2',timeout=8)
        s.wait_for_log('audiod: WAV complete frames=72000',timeout=10)
        qmp.type_text('play /system/share/test-mono.wav')
        s.wait_for_log('audiod: WAV started 22050 Hz 8-bit channels=1',timeout=8)
        s.wait_for_log('audiod: WAV complete frames=14400',timeout=8)
        # Local media when present; otherwise the generated public test fixture.
        previous=s.serial_text().count(marker)
        qmp.type_text('play')
        s.wait_for(lambda:s.serial_text().count(marker)>previous,'user WAV playback',timeout=8)
        if full:
            print('  Playing the complete Music WAV; verifying end-of-stream and DMA frame accounting...',flush=True)
            s.wait_for_log(f'audiod: WAV complete frames={output_frames} underruns=0',timeout=max(15,frames/rate+15))
        else:time.sleep(3)
        qmp.type_text('audio');monitor.screendump(s.OUT/'wav-playing.ppm')
        qmp.type_text('stop');s.wait_for_log('audiod: playback stopped')
        # File Manager delegates the same local or generated Music fixture.
        previous=s.serial_text().count(marker)
        monitor.send('sendkey f3',settle=.3)
        qmp.pointer(104,264);qmp.click();s.wait_for_log('files: list /home/user/Music')
        time.sleep(.3);qmp.pointer(340,267);qmp.double_click()
        s.wait_for(lambda:s.serial_text().count(marker)>previous,'Files WAV playback',timeout=8)
        monitor.screendump(s.OUT/'files-wav.ppm')
        monitor.send('sendkey spc',settle=.3)
        monitor.send('sendkey f2',settle=.3)
        qmp.type_text('play');time.sleep(.7)
        qmp.type_text('logout');s.wait_for_log('authd: logged out')
        time.sleep(.5)
        s.stop_qemu(process,monitor)
        left,right=read_capture(s.OUT/'pcm.wav')
        if strongest_tone(left,440)<3000 or strongest_tone(right,660)<3000:raise s.SmokeError('expected stereo waveform not present in captured output')
        if any(marker in s.serial_text() for marker in ('[fault]','PANIC','KERNEL EXCEPTION','FAILED')):raise s.SmokeError('runtime error')
        print('AUDIO TEST PASSED: 24-bit stereo 48k, mono 8-bit 22.05k resampling, Music WAV, stop/logout, PCM frequency verification')
    except Exception:
        if monitor:monitor.screendump(s.OUT/"failure.ppm")
        print('\n'.join(s.serial_text().splitlines()[-30:]));raise
    finally:
        s.stop_qemu(process,monitor)
        if monitor:monitor.close()
        if qmp:qmp.close()
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--iso',type=Path,required=True);p.add_argument('--qemu',default='qemu-system-x86_64');p.add_argument('--full',action='store_true');a=p.parse_args();run(a.iso,a.qemu,a.full)
