#!/usr/bin/env python3
"""Choose a bounded native guest framebuffer; never silently stretch an unknown mode."""
import argparse
import ctypes
import json
import platform
from pathlib import Path
import re
import subprocess

DEFAULT=(1440,900)
MAXIMUM=(3840,2160)

def validate_mode(text):
    match=re.fullmatch(r'(\d{3,4})x(\d{3,4})',text)
    if not match: raise ValueError('resolution must be WIDTHxHEIGHT')
    width,height=map(int,match.groups())
    scale=2 if width>=2560 and height>=1400 else 1
    if (width>=2560 and height<1400) or width%8 or height%scale or not 1024*scale<=width<=MAXIMUM[0] or not 700*scale<=height<=MAXIMUM[1]:
        raise ValueError('mode outside supported bounds (1024x700 to 3840x2160, width multiple of 8; HiDPI needs >=1400 high)')
    return width,height

def host_mode():
    if platform.system()=='Darwin':
        cg=ctypes.CDLL('/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics')
        cg.CGMainDisplayID.restype=ctypes.c_uint32
        cg.CGDisplayCopyDisplayMode.argtypes=[ctypes.c_uint32];cg.CGDisplayCopyDisplayMode.restype=ctypes.c_void_p
        mode=cg.CGDisplayCopyDisplayMode(cg.CGMainDisplayID())
        if not mode:return None
        try:
            result=[]
            for name in ('CGDisplayModeGetPixelWidth','CGDisplayModeGetPixelHeight'):
                function=getattr(cg,name);function.argtypes=[ctypes.c_void_p];function.restype=ctypes.c_size_t
                result.append(function(mode))
            return tuple(result)
        finally:
            cg.CGDisplayModeRelease.argtypes=[ctypes.c_void_p];cg.CGDisplayModeRelease(mode)
    try:
        text=subprocess.run(['xrandr','--current'],capture_output=True,text=True,timeout=3,check=True).stdout
        match=re.search(r' connected primary (\d+)x(\d+)\+',text) or re.search(r' connected (\d+)x(\d+)\+',text)
        if match:return tuple(map(int,match.groups()))
    except (OSError,subprocess.SubprocessError):pass
    return None

def choose(request='auto',helper=None):
    if request!='auto':return validate_mode(request),'explicit'
    mode=None
    if helper is not None and Path(helper).exists():
        try:
            result=subprocess.run([str(Path(helper).resolve()),'--screen'],capture_output=True,text=True,timeout=3,check=True)
            mode=tuple(map(int,result.stdout.split()))
            if len(mode)!=2:mode=None
        except (ValueError,OSError,subprocess.SubprocessError):pass
    if mode is None:mode=host_mode()
    if mode:
        try:return validate_mode(f'{mode[0]}x{mode[1]}'),'host pixels'
        except ValueError:
            # Choose once for displays exceeding the renderer's current bounds.
            from host_resize import fit_mode
            bounded=fit_mode(*mode)
            if bounded:return bounded,f'host {mode[0]}x{mode[1]}, bounded to renderer limit'
    return DEFAULT,'fallback; host mode unavailable or unsupported (use RESOLUTION=WIDTHxHEIGHT)'

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode',default='auto');parser.add_argument('--write',type=Path)
    args=parser.parse_args()
    mode,source=choose(args.mode)
    if args.write:
        args.write.parent.mkdir(parents=True,exist_ok=True)
        args.write.write_text(json.dumps({'width':mode[0],'height':mode[1],'scale':2 if mode[0]>=2560 and mode[1]>=1400 else 1,'source':source})+'\n')
    print(f'{mode[0]}x{mode[1]}')
