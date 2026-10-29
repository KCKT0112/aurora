#!/usr/bin/env python3
"""Compile a small audited SVG subset into fixed-point vector segments (not bitmaps)."""
import argparse
import math
from pathlib import Path
import re
import xml.etree.ElementTree as ET
NAMES=['folder','terminal','monitor','settings','document','refresh','arrow-left']

def segments(path):
    root=ET.parse(path).getroot()
    if root.attrib.get('viewBox')!='0 0 24 24': raise ValueError('icons require 24x24 viewBox')
    result=[]
    def line(a,b):
        if a!=b: result.append(tuple(round(v*256) for v in (*a,*b)))
    for element in root:
        tag=element.tag.rsplit('}',1)[-1]
        if tag=='circle':
            x,y,r=[float(element.attrib[k]) for k in ('cx','cy','r')]
            points=[(x+r*math.cos(i*math.tau/32),y+r*math.sin(i*math.tau/32)) for i in range(33)]
            for a,b in zip(points,points[1:]):line(a,b)
        elif tag=='path':
            tokens=re.findall(r'[MLCZ]|-?\d+(?:\.\d+)?',element.attrib['d'])
            i=0;current=(0,0);start=current
            while i<len(tokens):
                command=tokens[i];i+=1
                if command in ('M','L'):
                    point=(float(tokens[i]),float(tokens[i+1]));i+=2
                    if command=='L':line(current,point)
                    else:start=point
                    current=point
                elif command=='C':
                    values=list(map(float,tokens[i:i+6]));i+=6
                    a,b,end=values[:2],values[2:4],tuple(values[4:]);origin=current
                    for step in range(1,13):
                        t=step/12;u=1-t
                        point=tuple(u**3*origin[k]+3*u*u*t*a[k]+3*u*t*t*b[k]+t**3*end[k] for k in (0,1))
                        line(current,point);current=point
                elif command=='Z':line(current,start);current=start
                else:raise ValueError('unsupported SVG command')
        else:raise ValueError('unsupported SVG element')
    if len(result)>160:raise ValueError('icon complexity limit')
    return result

def generate(directory):
    lines=['/* Generated from SVG sources in assets/icons. Fixed-point coordinates: 1/256. */']
    for index,name in enumerate(NAMES):
        data=segments(directory/(name+'.svg'))
        lines.append(f'static const vector_segment_t icon_{index}[] = {{')
        lines.extend('    {'+','.join(map(str,s))+'},' for s in data)
        lines.append('};')
    lines.append('static const vector_icon_t icons[] = {')
    for i in range(len(NAMES)):lines.append(f'    {{icon_{i}, sizeof(icon_{i}) / sizeof(icon_{i}[0])}},')
    lines.append('};')
    return '\n'.join(lines)+'\n'
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    data=generate(Path(__file__).resolve().parent.parent/'assets/icons')
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(data)
