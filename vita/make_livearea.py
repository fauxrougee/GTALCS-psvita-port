#!/usr/bin/env python3
"""Export generated LiveArea artwork to Vita-compatible indexed PNGs.

The creative source images live in <root>/art/ (default vita/art/). FFmpeg only
resizes and exports these images; rerunning this script never regenerates or
replaces the art. --root vita/bench exports the reLCS Benchmark app's art
(vita/bench/make_bench_art.py can also write it without FFmpeg).
"""
from pathlib import Path
import argparse
import struct
import subprocess
import xml.etree.ElementTree as ET

HERE = Path(__file__).resolve().parent
ASSETS = (
    ('icon-source.png', 'sce_sys/icon0.png', 128, 128),
    ('livearea-source.png', 'sce_sys/livearea/contents/bg.png', 840, 500),
    ('startup-source.png', 'sce_sys/livearea/contents/startup.png', 280, 158),
)

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root', type=Path, default=HERE,
                   help='directory holding art/ and sce_sys/ (default: vita/)')
    root = p.parse_args().root.resolve()
    for source, target, width, height in ASSETS:
        src, dst = root/'art'/source, root/target
        if not src.is_file():
            raise SystemExit(f'Missing artwork source: {src}')
        dst.parent.mkdir(parents=True, exist_ok=True)
        graph = (f'scale={width}:{height}:flags=lanczos,split[a][b];'
                 '[a]palettegen=max_colors=256:reserve_transparent=0[p];'
                 '[b][p]paletteuse=dither=sierra2_4a')
        subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-y',
                        '-i',str(src),'-vf',graph,'-pix_fmt','pal8','-frames:v','1',
                        str(dst)],check=True)
        data = dst.read_bytes()
        assert data[:8] == b'\x89PNG\r\n\x1a\n'
        w,h,depth,color,comp,flt,interlace = struct.unpack('>IIBBBBB',data[16:29])
        assert (w,h,depth,color,interlace) == (width,height,8,3,0)
        print(f'{target}: {w}x{h}, indexed PNG, {len(data)} bytes')
    template = root/'sce_sys/livearea/contents/template.xml'
    tree = ET.parse(template)
    tree.getroot().set('content-rev','2')
    tree.write(template,encoding='utf-8',xml_declaration=True)

if __name__ == '__main__':
    main()
