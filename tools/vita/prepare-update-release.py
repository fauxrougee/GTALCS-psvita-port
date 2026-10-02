#!/usr/bin/env python3
"""Verify a full VPK and prepare the checksum and LiveArea update metadata.

This prepares local files; it does not publish or modify a GitHub release.
Upload update.txt beside the VPK after testing it on a console.
"""
import argparse
import importlib.util
from pathlib import Path
from zipfile import ZipFile
import re

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('package_release',ROOT/'tools/vita/package-release.py')
package=importlib.util.module_from_spec(spec); spec.loader.exec_module(package)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('vpk',type=Path)
    p.add_argument('--output-dir',type=Path)
    a=p.parse_args(); path=a.vpk.resolve(); output=(a.output_dir or path.parent).resolve()
    with ZipFile(path) as z:
        assert z.testzip() is None, 'ZIP CRC failed'
        sfo=package.sfo_values(z.read('sce_sys/param.sfo')); version=sfo['APP_VER']
        assert sfo['TITLE_ID']=='RELCS0001' and sfo['ATTRIBUTE2']==12
        assert re.fullmatch(r'\d{2}\.\d{2}',version)
        assert path.name==f'reLCS-{version}-intro-complete.vpk', 'Release filename must match APP_VER'
        for name in ['game.bin','boot/intro.vtm','sce_sys/package/head.bin','updater/eboot.bin']:
            assert name in z.namelist(),f'Missing {name}'
        assert b'psla:-update' in z.read('sce_sys/livearea/contents/template.xml')
        assert not any(name.startswith('updater/sce_sys/') for name in z.namelist()), 'Separate updater app is forbidden'
        unpacked=sum(info.file_size for info in z.infolist())
    checksum=package.file_digest(path)
    metadata=('RELCS-UPDATE-1\n'+f'version={version}\nsize={path.stat().st_size}\n'
              f'unpacked_size={unpacked}\nsha256={checksum}\n'
              'url=https://github.com/fauxrougee/GTALCS-psvita-port/releases/download/'
              f'v{version}/{path.name}\n')
    output.mkdir(parents=True,exist_ok=True)
    (output/'update.txt').write_text(metadata,encoding='ascii',newline='\n')
    (output/'SHA256SUMS.txt').write_text(checksum+'  '+path.name+'\n',encoding='ascii',newline='\n')
    print(f'PASS: v{version}, full VPK, checksum and update.txt ready in {output}')


if __name__=='__main__': main()
