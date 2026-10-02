#!/usr/bin/env python3
"""Build the Vita release natively with CMake/Ninja. The full intro is default.

--benchmark builds the separate reLCS Benchmark app (docs/BENCHMARK.md)."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
BENCH_TITLE_ID = 'RLCSBENCH'   # vita/CMakeLists.txt, VITA_BENCHMARK


def locate(name):
    found = shutil.which(name)
    if found:
        return found
    if os.name == 'nt':
        vs = Path(os.environ.get('ProgramFiles', 'C:/Program Files'))/'Microsoft Visual Studio'
        relative = 'Common7/IDE/CommonExtensions/Microsoft/CMake/'
        suffix = {'cmake': relative+'CMake/bin/cmake.exe', 'ninja': relative+'Ninja/ninja.exe'}[name]
        for path in sorted(vs.glob('20*/*/'+suffix), reverse=True):
            return str(path)
    raise SystemExit(f'{name} is required (install it or add it to PATH)')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--no-intro', action='store_true', help='Small test VPK without movie/launcher')
    p.add_argument('--benchmark', action='store_true',
                   help='reLCS Benchmark VPK (build/vita-bench, TITLE_ID '+BENCH_TITLE_ID+', no intro)')
    p.add_argument('--sdk', type=Path, default=os.environ.get('VITASDK'))
    p.add_argument('--build-dir', type=Path)
    p.add_argument('--output-dir', type=Path, default=ROOT/'dist')
    p.add_argument('--jobs', '-j', type=int, default=4)
    args = p.parse_args()
    if args.jobs < 1:
        p.error('--jobs must be positive')
    intro = not (args.no_intro or args.benchmark)
    sdk = args.sdk or (ROOT/'sdk/windows/vitasdk' if os.name == 'nt' else Path.home()/'vitasdk')
    sdk = sdk.resolve()
    suffix = '.exe' if os.name == 'nt' else ''
    if not (sdk/'bin'/('arm-vita-eabi-gcc'+suffix)).is_file():
        raise SystemExit('Set VITASDK or --sdk to an installed VitaSDK; see vita/README.md')
    if not (ROOT/'vendor/librw/rw.h').is_file():
        raise SystemExit('librw is missing: git submodule update --init vendor/librw')
    modules = ['PIL', 'elftools'] + (['numpy', 'lz4.block'] if intro else [])
    for module in modules:
        try:
            __import__(module)
        except ImportError:
            raise SystemExit('Install host requirements: python -m pip install -r tools/vita/requirements.txt')
    if intro and (not shutil.which('ffmpeg') or not shutil.which('ffprobe')):
        raise SystemExit('ffmpeg and ffprobe are required for the intro and must be in PATH')
    missing = [name for name in ['vitaGL','openal','mpg123','vitashark','SceShaccCgExt','mathneon',
                                'taihen_stub','kubridge_stub','z']
               if not (sdk/'arm-vita-eabi/lib'/('lib'+name+'.a')).is_file()]
    if missing:
        raise SystemExit('Missing VitaSDK target libraries: '+', '.join(missing)+'; see vita/README.md')
    if intro:
        missing=[name for name in ['curl','mbedtls','mbedx509','mbedcrypto','zstd']
                 if not (sdk/'arm-vita-eabi/lib'/('lib'+name+'.a')).is_file()]
        if missing:
            raise SystemExit('Install updater dependencies: vdpm install curl-mbedtls mbedtls zstd')
    env = os.environ.copy()
    env['VITASDK'] = sdk.as_posix()
    env['PATH'] = str(sdk/'bin') + os.pathsep + env['PATH']
    kind = 'vita-bench' if args.benchmark else 'vita-intro' if intro else 'vita-no-intro'
    build = (args.build_dir or ROOT/'build'/kind).resolve()
    cmake, ninja = locate('cmake'), locate('ninja')
    subprocess.run([cmake,'-S',str(ROOT/'vita'),'-B',str(build),'-G','Ninja',
                    '-DCMAKE_BUILD_TYPE=Release','-DCMAKE_MAKE_PROGRAM='+ninja,
                    '-DCMAKE_NINJA_FORCE_RESPONSE_FILE=ON','-DPython3_EXECUTABLE='+sys.executable,
                    '-DVITA_WITH_INTRO='+('ON' if intro else 'OFF'),
                    '-DVITA_BENCHMARK='+('ON' if args.benchmark else 'OFF')], env=env, check=True)
    subprocess.run([cmake,'--build',str(build),'--parallel',str(args.jobs)],env=env,check=True)
    subprocess.run([sys.executable,str(ROOT/'tools/vita/check-vita-elf.py'),str(build),
                    *(['--intro'] if intro else [])],check=True)
    version = re.search(r'set\(VITA_VERSION\s+"([^"]+)"\)',(ROOT/'vita/CMakeLists.txt').read_text())[1]
    art = ['--title-id',BENCH_TITLE_ID,'--sce-sys',str(ROOT/'vita/bench/sce_sys')] if args.benchmark else []
    subprocess.run([sys.executable,str(ROOT/'tools/vita/package-release.py'),'--build-dir',str(build),
                    '--version',version,'--verify-only',*(['--intro'] if intro else []),*art],check=True)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    name = 'reLCS-'+version+('-benchmark' if args.benchmark else '-intro-complete' if intro else '-sans-intro')
    outputs = {'reLCS.vpk': name+'.vpk', 'reLCS': name+'-game.elf', 'release.json': name+'.json'}
    if intro:
        outputs['relcs_intro'] = name+'-launcher.elf'
        outputs['relcs_update'] = name+'-updater.elf'
        outputs['update.txt'] = 'update.txt'
    sums = []
    for src, dst in outputs.items():
        target = args.output_dir/dst
        shutil.copy2(build/src, target)
        sums.append(hashlib.sha256(target.read_bytes()).hexdigest()+'  '+dst)
    (args.output_dir/(name+'.sha256')).write_text('\n'.join(sums)+'\n',encoding='utf-8')
    print('Ready:', (args.output_dir/(name+'.vpk')).resolve())


if __name__ == '__main__':
    main()
