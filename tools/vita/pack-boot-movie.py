#!/usr/bin/env python3
"""Prepare the complete edited intro for direct CPU playback, without AVPlayer.

Requires ffmpeg/ffprobe, numpy and lz4; runs natively on Windows and Linux.
Each frame uses lossless colour decorrelation
and an independent LZ4 block. Audio is decoded to 48 kHz signed-16 stereo PCM. No rescaling,
frame-rate change, colour quantisation or additional lossy encoding occurs.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import zlib
import numpy as np
import lz4.block

ROOT = Path(__file__).resolve().parents[2]
HEADER = struct.Struct('<8s8I3Q4I')
ENTRY = struct.Struct('<QII')
WIDTH, HEIGHT, FPS, RATE = 640, 544, 25, 48000
RAW_BYTES = WIDTH * HEIGHT * 4


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT/'vita/boot/intro.mp4')
    parser.add_argument('--output', type=Path, default=ROOT/'vita/boot/intro.vtm')
    args = parser.parse_args()
    ffmpeg = shutil.which('ffmpeg') or shutil.which('ffmpeg.exe')
    ffprobe = shutil.which('ffprobe') or shutil.which('ffprobe.exe')
    if not ffmpeg or not ffprobe:
        raise SystemExit('ffmpeg and ffprobe are required')

    source = str(args.source.resolve())
    probe = json.loads(subprocess.check_output([ffprobe, '-v', 'error', '-show_streams',
                                               '-of', 'json', source]))
    video = next(s for s in probe['streams'] if s['codec_type'] == 'video')
    assert (video['width'], video['height'], video['r_frame_rate']) == (WIDTH, HEIGHT, '25/1')
    count = int(video['nb_frames'])
    assert 1 <= count <= 3750
    audio_samples = count * (RATE // FPS)
    color = np.empty((3, WIDTH * HEIGHT), dtype=np.uint8)
    # Audio is deliberately padded/trimmed to the exact video timeline.
    audio = subprocess.check_output([ffmpeg, '-v', 'error', '-i', source, '-map', '0:a:0',
        '-af', f'apad,atrim=end_sample={audio_samples}', '-ar', str(RATE), '-ac', '2',
        '-f', 's16le', '-'])
    assert len(audio) == audio_samples * 4
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temp = args.output.with_suffix('.vtm.tmp')
    video_hash = hashlib.sha256()
    frame_hashes = []
    sizes = []
    index_offset = HEADER.size
    audio_offset = index_offset + count * ENTRY.size
    video_offset = audio_offset + len(audio)
    process = subprocess.Popen([ffmpeg, '-v', 'error', '-i', source, '-map', '0:v:0',
        '-an', '-fps_mode', 'passthrough', '-pix_fmt', 'rgba', '-f', 'rawvideo', '-'],
        stdout=subprocess.PIPE)
    try:
        with temp.open('w+b') as out:
            out.write(bytes(audio_offset))
            out.write(audio)
            index = bytearray()
            for n in range(count):
                frame = process.stdout.read(RAW_BYTES)
                assert len(frame) == RAW_BYTES, (n, len(frame))
                video_hash.update(frame)
                frame_hashes.append(hashlib.sha256(frame).hexdigest())
                rgba = np.frombuffer(frame, dtype=np.uint8).reshape(-1, 4)
                assert np.all(rgba[:, 3] == 255), 'Movie format requires opaque pixels'
                # Same modulo-256 planes as IntroPackColorPlanes, no rounding.
                np.subtract(rgba[:, 0], rgba[:, 1], out=color[0])
                color[1] = rgba[:, 1]
                np.subtract(rgba[:, 2], rgba[:, 1], out=color[2])
                data = lz4.block.compress(color.tobytes(), mode='high_compression',
                                          compression=12, store_size=False)
                size = len(data)
                assert 0 < size <= RAW_BYTES + RAW_BYTES // 255 + 16
                index += ENTRY.pack(out.tell(), size, zlib.crc32(data))
                out.write(data)
                sizes.append(size)
                if n % 250 == 0:
                    print(f'Lossless frames: {n + 1}/{count}', flush=True)
            assert not process.stdout.read(1), 'More frames than declared'
            assert process.wait() == 0
            header = HEADER.pack(b'VITAMV02', WIDTH, HEIGHT, FPS, 1, count, RATE,
                audio_samples, max(sizes), index_offset, audio_offset, video_offset,
                zlib.crc32(audio), zlib.crc32(index), 0, 1)
            header = header[:72] + struct.pack('<I', zlib.crc32(header[:72])) + header[76:]
            out.seek(0)
            out.write(header)
            out.write(index)
        temp.replace(args.output)
    finally:
        process.stdout.close()
        if process.poll() is None:
            process.kill()
            process.wait()
    manifest = {
        'format': 'VITAMV02', 'source_sha256': hashlib.sha256(args.source.read_bytes()).hexdigest(),
        'container_sha256': hashlib.sha256(args.output.read_bytes()).hexdigest(),
        'frames': count, 'width': WIDTH, 'height': HEIGHT, 'fps': FPS,
        'duration_seconds': count / FPS, 'audio_samples': audio_samples,
        'audio_sha256': hashlib.sha256(audio).hexdigest(), 'rgba_sha256': video_hash.hexdigest(),
        'frame_sha256': frame_hashes, 'max_frame_bytes': max(sizes),
        'peak_one_second_bytes': max(sum(sizes[n:n+FPS]) for n in range(count-FPS+1)),
        'mean_video_bytes_per_second': sum(sizes) * FPS / count,
        'container_bytes': args.output.stat().st_size,
    }
    args.output.with_suffix('.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps({k:v for k,v in manifest.items() if k != 'frame_sha256'}, indent=2))


if __name__ == '__main__':
    main()
