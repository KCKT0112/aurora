#!/usr/bin/env python3
"""Generate Aurora's anti-aliased 8-bit alpha font atlas from Inter.

The generated asset is a compact, self-describing binary consumed by
`libs/ui/font.c`. Inter is OFL-licensed (see assets/fonts/Inter-OFL.txt).

Format (little endian):
  header:  magic u32 ("AURF" as u32), version u32, face_count u32, face_offset u32
  face:    size u32, weight u16, glyph_count u16, ascent i16, descent i16,
           glyph_offset u32, atlas_offset u32, atlas_width u16,
           atlas_height u16, line_height u16, reserved u16
  glyph:   codepoint u32, left i16, top i16, width u16, height u16,
           advance u16, atlas_x u16, atlas_y u16
"""
from __future__ import annotations

import argparse
import pathlib
import struct

from PIL import ImageFont

MAGIC = 0x46525541  # "AURF" little-endian
VERSION = 1
HEADER_SIZE = 16
FACE_FORMAT = "<IHHhhIIHHHH"
FACE_SIZE = struct.calcsize(FACE_FORMAT)
GLYPH_FORMAT = "<IhhHHHHH"
GLYPH_SIZE = struct.calcsize(GLYPH_FORMAT)
ATLAS_WIDTH = 1024
PADDING = 1

# Basic Latin plus punctuation used by the desktop chrome.
EXTRA_CODEPOINTS = [
    0x00B7,  # middle dot
    0x00D7,  # multiplication sign
    0x2013,  # en dash
    0x2014,  # em dash
    0x2022,  # bullet
    0x2026,  # ellipsis
    0x2190, 0x2191, 0x2192, 0x2193,  # arrows
    0x2713,  # check mark
]

BASE_SIZES = [12, 13, 14, 16, 18, 20, 24, 28, 32, 64]
FACES = sorted({(size * scale, weight) for size in BASE_SIZES for scale in (1, 2) for weight in (400, 600)})



def optical_size(px: int) -> int:
    if px <= 18:
        return 14
    if px <= 28:
        return 20
    return 32


def make_font(path: pathlib.Path, px: int, weight: int) -> ImageFont.FreeTypeFont:
    font = ImageFont.truetype(str(path), px)
    if hasattr(font, "get_variation_axes"):
        values = []
        for axis in font.get_variation_axes():
            name = bytes(axis["name"]).decode("ascii", "ignore").lower()
            if "optical" in name:
                values.append(optical_size(px))
            elif "weight" in name:
                values.append(weight)
            else:
                values.append(axis["default"])
        if values:
            font.set_variation_by_axes(values)
    return font


def rasterize(font_path: pathlib.Path, px: int, weight: int):
    font = make_font(font_path, px, weight)
    ascent, descent = font.getmetrics()
    line_height = ascent + descent
    codepoints = list(range(32, 127)) + EXTRA_CODEPOINTS
    glyphs = []
    for codepoint in codepoints:
        ch = chr(codepoint)
        advance = max(0, round(font.getlength(ch)))
        bbox = font.getbbox(ch)
        mask = font.getmask(ch, mode="L")
        width, height = mask.size
        raw = bytes(mask)

        # Trim transparent padding while keeping the glyph bearings coherent.
        left, top, right, bottom = bbox
        nonzero = [(i % width, i // width) for i, value in enumerate(raw) if value]
        if nonzero:
            min_x = min(point[0] for point in nonzero)
            max_x = max(point[0] for point in nonzero)
            min_y = min(point[1] for point in nonzero)
            max_y = max(point[1] for point in nonzero)
            trimmed = bytearray()
            for row in range(min_y, max_y + 1):
                start = row * width + min_x
                trimmed.extend(raw[start:start + max_x - min_x + 1])
            left += min_x
            top += min_y
            width = max_x - min_x + 1
            height = max_y - min_y + 1
            raw = bytes(trimmed)
        else:
            width = 0
            height = 0
            raw = b""

        glyphs.append(
            {
                "codepoint": codepoint,
                "left": left,
                "top": top,
                "width": width,
                "height": height,
                "advance": advance,
                "pixels": raw,
            }
        )
    return glyphs, ascent, descent, line_height


def pack_face(font_path: pathlib.Path, px: int, weight: int):
    glyphs, ascent, descent, line_height = rasterize(font_path, px, weight)

    positions = []
    x = PADDING
    y = PADDING
    row_height = 0
    for glyph in glyphs:
        if glyph["width"] == 0 or glyph["height"] == 0:
            positions.append((0, 0))
            continue
        if x + glyph["width"] + PADDING > ATLAS_WIDTH:
            x = PADDING
            y += row_height + PADDING
            row_height = 0
        positions.append((x, y))
        x += glyph["width"] + PADDING
        row_height = max(row_height, glyph["height"])

    atlas_height = max(1, y + row_height + PADDING)
    atlas = bytearray(ATLAS_WIDTH * atlas_height)
    for glyph, (atlas_x, atlas_y) in zip(glyphs, positions):
        for row in range(glyph["height"]):
            source = row * glyph["width"]
            destination = (atlas_y + row) * ATLAS_WIDTH + atlas_x
            atlas[destination:destination + glyph["width"]] = \
                glyph["pixels"][source:source + glyph["width"]]
        glyph["atlas_x"] = atlas_x
        glyph["atlas_y"] = atlas_y

    return {
        "size": px,
        "weight": weight,
        "ascent": ascent,
        "descent": descent,
        "line_height": line_height,
        "glyphs": glyphs,
        "atlas_width": ATLAS_WIDTH,
        "atlas_height": atlas_height,
        "atlas": bytes(atlas),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font", type=pathlib.Path,
                        default=pathlib.Path("assets/fonts/InterVariable.ttf"))
    parser.add_argument("--output", type=pathlib.Path,
                        default=pathlib.Path("assets/generated/ui_font.bin"))
    args = parser.parse_args()

    faces = [pack_face(args.font, px, weight) for px, weight in FACES]

    blob = bytearray(HEADER_SIZE + FACE_SIZE * len(faces))
    struct.pack_into("<IIII", blob, 0, MAGIC, VERSION, len(faces),
                     HEADER_SIZE)

    for index, face in enumerate(faces):
        glyph_offset = len(blob)
        for glyph in face["glyphs"]:
            blob.extend(struct.pack(
                GLYPH_FORMAT,
                glyph["codepoint"],
                glyph["left"], glyph["top"],
                glyph["width"], glyph["height"],
                glyph["advance"],
                glyph["atlas_x"], glyph["atlas_y"],
            ))
        while len(blob) % 4:
            blob.append(0)
        atlas_offset = len(blob)
        blob.extend(face["atlas"])
        struct.pack_into(
            FACE_FORMAT, blob, HEADER_SIZE + index * FACE_SIZE,
            face["size"], face["weight"], len(face["glyphs"]),
            face["ascent"], face["descent"],
            glyph_offset, atlas_offset,
            face["atlas_width"], face["atlas_height"],
            face["line_height"], 0,
        )
        print(f"  face {face['size']}px/{face['weight']}: "
              f"{len(face['glyphs'])} glyphs, "
              f"atlas {face['atlas_width']}x{face['atlas_height']}")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(blob)
    print(f"wrote {args.output} ({len(blob)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
