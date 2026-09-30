#!/usr/bin/env python3
"""Export publishable sources with pinned vendor sources, no local game data."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import subprocess
import tarfile
from zipfile import ZipFile, ZIP_DEFLATED

ROOT = Path(__file__).resolve().parents[2]
EXCLUDED = {'assets', 'sdk', 'build', 'dist', '.local', '.git', '__pycache__'}
PRIVATE_SUFFIXES = {'.vpk', '.elf', '.dmp', '.psp2dmp', '.log', '.vtm', '.pyc'}


def git(*args, cwd=ROOT):
    return subprocess.check_output(['git', *args], cwd=cwd)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path)
    args = p.parse_args()
    version = re.search(r'set\(VITA_VERSION\s+"([^"]+)"\)',
                        (ROOT/'vita/CMakeLists.txt').read_text())[1]
    target = args.output or ROOT/'dist'/f'reLCS-{version}-sources.zip'
    target.parent.mkdir(parents=True, exist_ok=True)
    names = set(git('ls-files','--cached','--others','--exclude-standard','-z').decode().split('\0'))
    gitlinks = {entry.split('\t',1)[1]: entry.split()[1]
                for entry in git('ls-files','--stage','-z').decode().split('\0')
                if entry.startswith('160000 ')}
    previous = ROOT/'SOURCE-MANIFEST.json'
    revisions = json.loads(previous.read_text()).get('vendor_revisions', {}) if previous.is_file() else {}
    manifest = {'version': version, 'vendor_revisions': revisions, 'sha256': {}}
    with ZipFile(target, 'w', ZIP_DEFLATED, compresslevel=6) as z:
        def add(name, data):
            path = Path(name)
            if (path.parts[0] in EXCLUDED or {'.git', '__pycache__'} & set(path.parts)
                    or path.suffix.lower() in PRIVATE_SUFFIXES):
                raise RuntimeError('Private/generated file in source export: '+name)
            if len(data) >= 100*1024*1024:
                raise RuntimeError('File too large for source Git hosting: '+name)
            z.writestr(name,data)
            manifest['sha256'][name] = hashlib.sha256(data).hexdigest()
        for name in sorted(names):
            if not name or name in {'.gitmodules', 'SOURCE-MANIFEST.json'}:
                continue  # vendor sources are embedded; the manifest is regenerated
            if name.startswith('vita/benchmark/'):
                continue  # console results and logs are local diagnostic inputs
            path=ROOT/name
            if path.is_file():
                add(name,path.read_bytes())
        for vendor in ['librw','ogg','opus','opusfile']:
            base=ROOT/'vendor'/vendor
            if 'vendor/'+vendor not in gitlinks:
                continue  # vendored files in an archive-based repo were added above
            revision=git('rev-parse','HEAD',cwd=base).decode().strip()
            if revision != gitlinks['vendor/'+vendor]:
                raise RuntimeError('Record the submodule revision in Git before exporting: '+vendor)
            manifest['vendor_revisions'][vendor] = revision
            # Like a fresh submodule checkout: modifications are carried by the
            # reviewed project patch, not accidentally embedded from this PC.
            with tarfile.open(fileobj=io.BytesIO(git('archive','--format=tar','HEAD',cwd=base))) as t:
                for entry in t:
                    if entry.isfile():
                        add('vendor/'+vendor+'/'+entry.name,t.extractfile(entry).read())
        z.writestr('SOURCE-MANIFEST.json',json.dumps(manifest,indent=2)+'\n')
    with ZipFile(target) as z:
        assert z.testzip() is None
        assert 'vita/boot/intro.mp4' in z.namelist()
        assert len(z.namelist()) == len(set(z.namelist()))
    target.with_suffix('.zip.sha256').write_text(
        hashlib.sha256(target.read_bytes()).hexdigest()+'  '+target.name+'\n', encoding='utf-8')
    print(f'PASS: {len(manifest["sha256"])} source/media files, pinned vendors, no local data: {target}')


if __name__ == '__main__':
    main()
