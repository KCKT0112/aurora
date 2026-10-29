"""Deterministic PCM WAV fixtures, built only into test images."""
import math
import struct

def tone(rate=48000,bits=24,channels=2,seconds=1.5):
    audio=bytearray()
    for i in range(round(rate*seconds)):
        for channel in range(channels):
            value=round(math.sin(2*math.pi*(440 if channel==0 else 660)*i/rate)*0.35*((1<<(bits-1))-1))
            if bits==8:audio.append(value+128)
            else:audio.extend(value.to_bytes(bits//8,'little',signed=True))
    fmt=struct.pack('<HHIIHH',1,channels,rate,rate*channels*(bits//8),channels*(bits//8),bits)
    junk=b'JUNK'+struct.pack('<I',3)+b'abc\0'
    body=b'WAVE'+junk+b'fmt '+struct.pack('<I',16)+fmt+b'data'+struct.pack('<I',len(audio))+audio
    if len(audio)&1:body+=b'\0'
    return b'RIFF'+struct.pack('<I',len(body))+body
