#!/usr/bin/env python3
"""Replace two cards in the user's 412x360, 25 FPS intro with FFmpeg.

Keeps the complete sequence and music. Uses the baked present.png/port.png
masks supplied in vita/boot; no Windows font installation is required.
Fits the reference into 624x544, padded to a 640-pixel decoder stride.
"""
import argparse
import json
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('source', type=Path)
args = parser.parse_args()
source = args.source.resolve()
probe = json.loads(subprocess.check_output(['ffprobe','-v','error','-select_streams','v:0',
    '-show_entries','stream=width,height,r_frame_rate','-of','json',str(source)]))['streams'][0]
if (probe['width'],probe['height'],probe['r_frame_rate']) != (412,360,'25/1'):
    raise SystemExit('This edit is timed for the supplied 412x360, 25 FPS reference only.')
out = root/'vita/boot'
# Source frames: instant appearance, short hold, then fade to black.
graph = (
    "[0:v]drawbox=x=0:y=150:w=iw:h=60:color=black:t=fill:enable='between(t,18.28,22.72)'[clean];"
    "[1:v]format=rgba,fade=t=out:st=18.48:d=1.72:alpha=1[p];"
    "[2:v]format=rgba,fade=t=out:st=21.08:d=1.60:alpha=1[q];"
    "[clean][p]overlay=x=(W-w)/2:y=167:enable='between(t,18.32,20.20)':shortest=1[a];"
    "[a][q]overlay=x=(W-w)/2:y=167:enable='between(t,20.92,22.68)':shortest=1,"
    "scale=624:544:flags=lanczos,pad=640:544:8:0:black,setsar=1,format=yuv420p[v]"
)
subprocess.run(['ffmpeg','-hide_banner','-loglevel','warning','-y','-i',str(source),
    '-loop','1','-framerate','25','-i',str(out/'present.png'),
    '-loop','1','-framerate','25','-i',str(out/'port.png'),
    '-filter_complex',graph,'-map','[v]','-map','0:a:0',
    '-c:v','libx264','-profile:v','baseline','-level:v','3.0','-preset','slow','-crf','16',
    '-g','50','-bf','0','-colorspace','smpte170m','-color_primaries','smpte170m',
    '-color_trc','smpte170m','-color_range','tv',
    '-c:a','aac','-b:a','192k','-ar','48000','-ac','2','-movflags','+faststart',
    '-shortest',str(out/'intro.mp4')],check=True)
print(out/'intro.mp4')
