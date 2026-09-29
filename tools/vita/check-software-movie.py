#!/usr/bin/env python3
"""Decode every release frame through the production C++ reader under ASan/UBSan.

Compare exact RGBA/PCM SHA256 against the independent FFmpeg decode saved by
the packer, then reject damaged headers, offsets, audio, frames and LZ4 blocks.
Runs natively on Windows and Linux. This validates the actual codec and assets,
not Vita hardware I/O. Host CRC uses test_support; ARM CRC is checked separately.
"""
import hashlib
import argparse
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import zlib
from host_build import build, run

ROOT=Path(__file__).resolve().parents[2]
LAUNCH=ROOT/'vita/launcher'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--media',type=Path,default=ROOT/'build/vita-intro/intro.vtm')
    MEDIA=parser.parse_args().media.resolve()
    manifest=json.loads(MEDIA.with_suffix('.json').read_text())
    with MEDIA.open('rb') as f:
        assert hashlib.sha256(f.read()).hexdigest()==manifest['container_sha256']
        f.seek(0)
        original_header=f.read(80)
        audio_offset=struct.unpack_from('<Q',original_header,48)[0]
        video_offset=struct.unpack_from('<Q',original_header,56)[0]
        f.seek(0)
        prefix=f.read(video_offset)
        first_offset,first_size,first_crc=struct.unpack_from('<QII',prefix,80)
        first_frame=f.read(first_size)
    with tempfile.TemporaryDirectory(prefix='relcs-software-') as temp:
        temp=Path(temp)
        binary=temp/'reader'
        binary,env=build(binary,[ROOT/'tools/vita/check-movie-file.cpp',LAUNCH/'movie_file.cpp',
                                LAUNCH/'lz4/lz4.c'],[ROOT/'tools/vita/test_support',LAUNCH])
        p=subprocess.Popen([str(binary),'dump',str(MEDIA)],stdout=subprocess.PIPE,env=env)
        audio=p.stdout.read(manifest['audio_samples']*4)
        assert hashlib.sha256(audio).hexdigest()==manifest['audio_sha256']
        frames_hash=hashlib.sha256()
        for n,expected in enumerate(manifest['frame_sha256']):
            rgba=p.stdout.read(manifest['width']*manifest['height']*4)
            assert hashlib.sha256(rgba).hexdigest()==expected, f'Pixels differ: frame {n}'
            frames_hash.update(rgba)
        assert frames_hash.hexdigest()==manifest['rgba_sha256']
        for n in [manifest['frames']-1,0,1000,18,277]:
            rgba=p.stdout.read(manifest['width']*manifest['height']*4)
            assert hashlib.sha256(rgba).hexdigest()==manifest['frame_sha256'][n]
        assert not p.stdout.read(1)
        assert p.wait()==0
        print('PASS: all 2774 RGBA frames and complete PCM exactly match FFmpeg; seeks match too.',flush=True)

        # Sparse files preserve the actual release length without duplicating
        # hundreds of MB for each corruption test. Open validates the complete
        # index and PCM; Decode validates only the requested compressed block.
        bad=temp/'bad.vtm'
        def reject(label, mutate, frame=False, length=None):
            data=bytearray(prefix+first_frame)
            mutate(data)
            with bad.open('wb') as f:
                f.write(data)
                f.truncate(MEDIA.stat().st_size if length is None else length)
            run(binary,env,['reject-frame' if frame else 'reject-open',str(bad),'0'])
            print('PASS:',label,flush=True)
        def header_crc(data):
            struct.pack_into('<I',data,72,zlib.crc32(data[:72]))
        def set_header(data,offset,value):
            struct.pack_into('<I',data,offset,value); header_crc(data)
        def index_crc(data):
            struct.pack_into('<I',data,68,zlib.crc32(data[80:audio_offset])); header_crc(data)
        reject('bad magic',lambda d:d.__setitem__(0,0))
        reject('bad header checksum',lambda d:d.__setitem__(72,d[72]^1))
        reject('unbounded duration',lambda d:set_header(d,24,0xffffffff))
        reject('unbounded compressed allocation',lambda d:set_header(d,36,0xffffffff))
        reject('audio duration mismatch',lambda d:set_header(d,32,1))
        reject('truncated file',lambda d:None,length=video_offset-1)
        reject('trailing bytes',lambda d:None,length=MEDIA.stat().st_size+1)
        reject('audio corruption',lambda d:d.__setitem__(audio_offset+128,d[audio_offset+128]^1))
        reject('frame corruption',lambda d:d.__setitem__(video_offset+12,d[video_offset+12]^1),frame=True)
        def invalid_offset(d):
            struct.pack_into('<Q',d,80,0xffffffffffffffff); index_crc(d)
        reject('overflowing frame offset',invalid_offset)
        def invalid_size(d):
            struct.pack_into('<I',d,88,0xffffffff); index_crc(d)
        reject('overflowing frame size',invalid_size)
        def invalid_lz4(d):
            d[video_offset:video_offset+first_size]=b'\xff'*first_size
            struct.pack_into('<I',d,92,zlib.crc32(d[video_offset:video_offset+first_size])); index_crc(d)
        reject('malformed LZ4 with valid container checksums',invalid_lz4,frame=True)
    print('PASS: production decoder and concurrent frame queue, host sanitizers, full data and malformed files.')


if __name__=='__main__':
    main()
