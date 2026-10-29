"""Bounded explicit display-mode protocol. No automatic window polling."""
import struct
from display_mode import validate_mode

def packet(width,height,sequence=0):
    validate_mode(f'{width}x{height}')
    return struct.pack('<4s4H',b'ARZ1',width,height,sequence,(width^height^sequence^0xa75a)&0xffff)

def fit_mode(width,height):
    # Clamp to the renderer limit, preserving aspect if the host exceeds it.
    ratio=min(1,3840/max(width,1),2160/max(height,1))
    width=int(width*ratio)//8*8;height=int(height*ratio)//2*2
    if width<1024 or height<700:return None
    if width>=2560 and height<1400:return None
    try:validate_mode(f'{width}x{height}');return width,height
    except ValueError:return None

