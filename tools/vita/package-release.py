#!/usr/bin/env python3
"""Build and verify a VPK from this build's executables, without old releases."""
import argparse
import hashlib
import json
from pathlib import Path
import re
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
    p.add_argument('--title-id', default='RELCS0001',
                   help='TITLE_ID expected in param.sfo (benchmark: RLCSBENCH)')
    p.add_argument('--sce-sys', type=Path, default=ROOT/'vita/sce_sys',
                   help='LiveArea art directory (benchmark: vita/bench/sce_sys)')
    args = p.parse_args()
    if not re.fullmatch(r'[A-Z0-9]{9}', args.title_id):
        p.error('--title-id must be 9 characters A-Z/0-9')
    sce_sys = args.sce_sys
    if not sce_sys.is_absolute() and not sce_sys.is_dir() and (ROOT/sce_sys).is_dir():
        sce_sys = ROOT/sce_sys
    sce_sys = sce_sys.resolve()
    build = args.build_dir.resolve()
    package = build/'reLCS.vpk'
    files = {'eboot.bin': build/('intro.bin' if args.intro else 'game.bin'),
             'sce_sys/param.sfo': build/'param.sfo',
             'sce_sys/package/head.bin': build/'head.bin',
             'licenses/vitashell.txt': ROOT/'vita/updater/licenses/vitashell.txt',
             'boot/loading.rgba.z': build/'loading.rgba.z'}
    for name in ['icon0.png', 'livearea/contents/bg.png', 'livearea/contents/startup.png',
                 'livearea/contents/template.xml']:
        files['sce_sys/'+name] = sce_sys/name
    if not args.intro and args.title_id=='RELCS0001':
        # Direct-launch test builds do not contain the updater or its Update tile.
        files['sce_sys/livearea/contents/template.xml']=ROOT/'vita/updater/template.xml'
    if args.intro:
        files.update({'game.bin': build/'game.bin', 'boot/intro.vtm': build/'intro.vtm',
                      'licenses/lz4.txt': ROOT/'vita/launcher/lz4/LICENSE',
                      'sce_sys/livearea/contents/update.png': sce_sys/'livearea/contents/update.png',
                      'updater/eboot.bin': build/'update.bin',
                      'updater/certs/ca-bundle.pem': ROOT/'vita/updater/ca-bundle.pem',
                      'licenses/vitashell.txt': ROOT/'vita/updater/licenses/vitashell.txt'})
        for name in ['curl.txt','mbedtls.txt','zstd.txt','font.txt','vitashell.txt']:
            files['updater/licenses/'+name]=ROOT/'vita/updater/licenses'/name
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
        assert sfo['APP_VER'] == args.version and sfo['TITLE_ID'] == args.title_id, \
            f"param.sfo: APP_VER={sfo['APP_VER']} TITLE_ID={sfo['TITLE_ID']}"
        assert sfo['ATTRIBUTE2'] == 12, 'Extended memory attribute missing'
        for name, width, height in [('sce_sys/icon0.png', 128, 128),
                                  ('sce_sys/livearea/contents/bg.png', 840, 500),
                                  ('sce_sys/livearea/contents/startup.png', 280, 158)]:
            assert struct.unpack('>IIBBBBB', z.read(name)[16:29]) == (width,height,8,3,0,0,0)
        unpacked_size=sum(info.file_size for info in z.infolist())
        if args.intro:
            assert not any(name.startswith('updater/sce_sys/') for name in z.namelist()), \
                'Internal updater must not contain a separate app definition or icon'
            assert b'psla:-update' in z.read('sce_sys/livearea/contents/template.xml')
    manifest = {'version': args.version, 'title_id': args.title_id, 'intro': args.intro,
                'unpacked_bytes':unpacked_size, 'updater':args.intro,
                'vpk_bytes': package.stat().st_size, 'vpk_sha256': file_digest(package),
                'files_sha256': hashes}
    if args.intro:
        movie = json.loads((build/'intro.json').read_text())
        assert hashes['boot/intro.vtm'] == movie['container_sha256']
        assert movie['frames'] == 2774 and movie['audio_samples'] == 5326080
        manifest['movie'] = {key: movie[key] for key in ['frames', 'duration_seconds', 'audio_samples',
                                                       'source_sha256', 'rgba_sha256', 'audio_sha256']}
    (build/'release.json').write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
    if args.intro:
        update=('RELCS-UPDATE-1\n'
                f'version={args.version}\nsize={manifest["vpk_bytes"]}\n'
                f'unpacked_size={unpacked_size}\nsha256={manifest["vpk_sha256"]}\n'
                'url=https://github.com/fauxrougee/GTALCS-psvita-port/releases/download/'
                f'v{args.version}/reLCS-{args.version}-intro-complete.vpk\n')
        (build/'update.txt').write_text(update,encoding='ascii',newline='\n')
    title = '' if args.title_id == 'RELCS0001' else f' {args.title_id}'
    print(f'PASS: {args.version}{title}, {"full intro + audio" if args.intro else "direct launch"}, '
          f'matching executables, assets, memory flag and ZIP CRCs; {package.stat().st_size} bytes')


if __name__ == '__main__':
    main()
