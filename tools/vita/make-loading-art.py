#!/usr/bin/env python3
"""Prepare the existing LiveArea image for the CPU loading display (Pillow)."""
from pathlib import Path
import argparse
import zlib
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
source = ROOT / 'vita/sce_sys/livearea/contents/bg.png'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, default=ROOT/'vita/boot/loading.rgba.z')
target = parser.parse_args().output
with Image.open(source) as art:
    assert art.size == (840, 500), art.size
    pixels = art.convert('RGBA').tobytes()
packed = zlib.compress(pixels, 9)
assert zlib.decompress(packed) == pixels
target.parent.mkdir(exist_ok=True)
if not target.exists() or target.read_bytes() != packed:
    target.write_bytes(packed)
print(f'Loading artwork: {len(packed)} bytes; original LiveArea pixels preserved.')
