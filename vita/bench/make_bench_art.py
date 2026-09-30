#!/usr/bin/env python3
"""LiveArea art of the reLCS Benchmark app, derived from the game's art (Pillow only).

An amber "BENCHMARK" band is drawn over the game's icon, LiveArea background
and launch gate, so the two apps are easy to tell apart on the home screen.
Writes:
  vita/bench/art/*-source.png  RGB at twice the final size (vita/make_livearea.py
                               --root vita/bench can export them with FFmpeg)
  vita/bench/sce_sys/...       final 8-bit indexed PNGs (tools/vita/package-release.py)
  vita/bench/sce_sys/livearea/contents/template.xml  copy of the game's
Input: vita/art/*-source.png when present (high resolution), otherwise the
packaged vita/sce_sys PNGs (--from-sce-sys forces the latter).
"""
from pathlib import Path
import argparse
import shutil
import struct
from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
VITA = HERE.parent
SCALE = 2   # art sources are drawn at twice the final size

AMBER = (255, 184, 28)
INK = (16, 16, 16)

# name, game art source, packaged file, final size, band (top, height), text, font size
# (band and font in final pixels)
ASSETS = (
    ('icon', 'icon-source.png', 'icon0.png', (128, 128), (86, 22), 'BENCH', 17),
    ('livearea', 'livearea-source.png', 'livearea/contents/bg.png', (840, 500), (398, 48), 'BENCHMARK', 34),
    ('startup', 'startup-source.png', 'livearea/contents/startup.png', (280, 158), (8, 26), 'BENCHMARK', 17),
)

FONTS = (
    'C:/Windows/Fonts/ariblk.ttf',
    'C:/Windows/Fonts/arialbd.ttf',
    'C:/Windows/Fonts/segoeuib.ttf',
    '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf',
    '/usr/share/fonts/TTF/DejaVuSans-Bold.ttf',
    '/Library/Fonts/Arial Bold.ttf',
)


def load_font(size):
    for path in FONTS:
        if Path(path).is_file():
            return ImageFont.truetype(path, size), path
    try:
        return ImageFont.load_default(size), 'Pillow default'
    except TypeError:
        return ImageFont.load_default(), 'Pillow default (bitmap)'


def draw_band(img, band, text, font_size):
    top, height = band[0] * SCALE, band[1] * SCALE
    edge = max(2, height // 12)
    draw = ImageDraw.Draw(img)
    draw.rectangle((0, top, img.width, top + height - 1), fill=AMBER)
    draw.rectangle((0, top, img.width, top + edge - 1), fill=INK)
    draw.rectangle((0, top + height - edge, img.width, top + height - 1), fill=INK)
    font, font_name = load_font(font_size * SCALE)
    spacing = font_size * SCALE // 10
    widths = [font.getlength(ch) for ch in text]
    x = (img.width - (sum(widths) + spacing * (len(text) - 1))) / 2
    ink = font.getbbox(text)
    y = top + height / 2 - (ink[1] + ink[3]) / 2
    for ch, width in zip(text, widths):
        draw.text((x, y), ch, font=font, fill=INK)
        x += width + spacing
    return font_name


def check_png(path, size):
    data = path.read_bytes()
    assert data[:8] == b'\x89PNG\r\n\x1a\n', path
    header = struct.unpack('>IIBBBBB', data[16:29])
    assert header == (size[0], size[1], 8, 3, 0, 0, 0), (path, header)
    return len(data)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--from-sce-sys', action='store_true',
                   help='start from the packaged vita/sce_sys PNGs even if vita/art exists')
    args = p.parse_args()
    (HERE/'art').mkdir(exist_ok=True)
    for name, art_name, packaged, size, band, text, font_size in ASSETS:
        source = VITA/'art'/art_name
        if args.from_sce_sys or not source.is_file():
            source = VITA/'sce_sys'/packaged
        work = (size[0] * SCALE, size[1] * SCALE)
        with Image.open(source) as original:
            img = original.convert('RGB').resize(work, Image.Resampling.LANCZOS)
        font_name = draw_band(img, band, text, font_size)
        art_path = HERE/'art'/art_name
        img.save(art_path, format='PNG', optimize=True)
        final = img.resize(size, Image.Resampling.LANCZOS)
        indexed = final.quantize(colors=256, method=Image.Quantize.MEDIANCUT,
                                 dither=Image.Dither.FLOYDSTEINBERG)
        target = HERE/'sce_sys'/packaged
        target.parent.mkdir(parents=True, exist_ok=True)
        indexed.save(target, format='PNG', optimize=True, bits=8)
        length = check_png(target, size)
        print(f'{target.relative_to(VITA.parent).as_posix()}: {size[0]}x{size[1]} indexed, {length} bytes '
              f'(from {source.relative_to(VITA.parent).as_posix()}, font {font_name})')
    template = HERE/'sce_sys/livearea/contents/template.xml'
    shutil.copyfile(VITA/'sce_sys/livearea/contents/template.xml', template)
    print(f'{template.relative_to(VITA.parent).as_posix()}: copied from vita/sce_sys')


if __name__ == '__main__':
    main()
