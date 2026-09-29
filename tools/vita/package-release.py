#!/usr/bin/env python3
"""Build and verify a VPK from this build's executables, without old releases."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
from zipfile import ZipFile, ZipInfo, ZIP_DEFLATED

ROOT = Path(__file__).resolve().parents[2]


def digest(stream):
    result = hashlib.sha256()
    for data in iter(lambda: stream.read(1024 * 1024), b''):
        result.update(data)
    return result.hexdigest()


def file_digest(path):
    with path.open('rb') as stream:
        return digest(stream)


def sfo_values(data):
    magic, version, keys, values, count = struct.unpack_from('<5I', data)
    assert magic == 0x46535000 and version == 0x101
    result = {}
    for i in range(count):
        key, fmt, length, capacity, offset = struct.unpack_from('<HHIII', data, 20 + 16 * i)
        name = data[keys + key:data.index(b'\0', keys + key)].decode('ascii')
        raw = data[values + offset:values + offset + length]
        result[name] = struct.unpack('<I', raw)[0] if fmt == 0x404 else raw.rstrip(b'\0').decode('utf-8')
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build-dir', type=Path, required=True)
    p.add_argument('--version', required=True)
    p.add_argument('--intro', action='store_true')
    p.add_argument('--verify-only', action='store_true')
    args = p.parse_args()
    build = args.build_dir.resolve()
    package = build/'reLCS.vpk'
    files = {'eboot.bin': build/('intro.bin' if args.intro else 'game.bin'),
             'sce_sys/param.sfo': build/'param.sfo',
             'boot/loading.rgba.z': build/'loading.rgba.z'}
    for name in ['icon0.png', 'livearea/contents/bg.png', 'livearea/contents/startup.png',
                 'livearea/contents/template.xml']:
        files['sce_sys/'+name] = ROOT/'vita/sce_sys'/name
    if args.intro:
        files.update({'game.bin': build/'game.bin', 'boot/intro.vtm': build/'intro.vtm',
                      'licenses/lz4.txt': ROOT/'vita/launcher/lz4/LICENSE'})
    for path in files.values():
        if not path.is_file():
            raise SystemExit(f'Missing build input: {path}')
    hashes = {name: file_digest(path) for name, path in files.items()}
    if not args.verify_only:
        temp = package.with_suffix('.vpk.tmp')
        with ZipFile(temp, 'w', ZIP_DEFLATED, compresslevel=6) as z:
            for name, path in files.items():
                info = ZipInfo(name, (2026, 1, 1, 0, 0, 0))
                info.compress_type = ZIP_DEFLATED
                info.external_attr = 0o100644 << 16
                with path.open('rb') as src, z.open(info, 'w') as dst:
                    shutil.copyfileobj(src, dst, 1024 * 1024)
        temp.replace(package)
    with ZipFile(package) as z:
        assert len(z.namelist()) == len(files) and set(z.namelist()) == set(files)
        for name, expected in hashes.items():
            with z.open(name) as stream:
                assert digest(stream) == expected, f'Packaged file differs: {name}'
        sfo = sfo_values(z.read('sce_sys/param.sfo'))
        assert sfo['APP_VER'] == args.version and sfo['TITLE_ID'] == 'RELCS0001'
        assert sfo['ATTRIBUTE2'] == 12, 'Extended memory attribute missing'
        for name, width, height in [('sce_sys/icon0.png', 128, 128),
                                  ('sce_sys/livearea/contents/bg.png', 840, 500),
                                  ('sce_sys/livearea/contents/startup.png', 280, 158)]:
            assert struct.unpack('>IIBBBBB', z.read(name)[16:29]) == (width,height,8,3,0,0,0)
    manifest = {'version': args.version, 'intro': args.intro,
                'vpk_bytes': package.stat().st_size, 'vpk_sha256': file_digest(package),
                'files_sha256': hashes}
    if args.intro:
        movie = json.loads((build/'intro.json').read_text())
        assert hashes['boot/intro.vtm'] == movie['container_sha256']
        assert movie['frames'] == 2774 and movie['audio_samples'] == 5326080
        manifest['movie'] = {key: movie[key] for key in ['frames', 'duration_seconds', 'audio_samples',
                                                       'source_sha256', 'rgba_sha256', 'audio_sha256']}
    (build/'release.json').write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
    print(f'PASS: {args.version}, {"full intro + audio" if args.intro else "direct launch"}, '
          f'matching executables, assets, memory flag and ZIP CRCs; {package.stat().st_size} bytes')


if __name__ == '__main__':
    main()
