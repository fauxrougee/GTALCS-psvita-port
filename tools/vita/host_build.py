"""Native host tests: MSVC + ASan on Windows, GCC + ASan/UBSan elsewhere.

Never launches WSL. Compiler environment changes apply only to subprocesses.
"""
import os
from pathlib import Path
import subprocess
import shutil
import re


def build(exe, sources, includes=(), defines=()):
    exe = Path(exe)
    if os.name == 'nt':
        vcvars = Path(os.environ['VITA_TEST_VCVARS']) if os.environ.get('VITA_TEST_VCVARS') else None
        if vcvars is None:
            vs = Path(os.environ.get('ProgramFiles', 'C:/Program Files'))/'Microsoft Visual Studio'
            choices = sorted(vs.glob('20*/*/VC/Auxiliary/Build/vcvars64.bat'), reverse=True)
            if not choices:
                raise RuntimeError('Install MSVC x64 with AddressSanitizer, or set VITA_TEST_VCVARS')
            vcvars = choices[0]
        if not vcvars.is_file():
            raise RuntimeError('Native MSVC x64 tools are required for Windows host tests')
        version = os.environ.get('VITA_TEST_MSVC_VERSION')
        if not version:
            installed = list((vcvars.parents[2]/'Tools/MSVC').glob('14.*'))
            version = max(installed, key=lambda p: tuple(map(int,p.name.split('.')))).name
        if not re.fullmatch(r'\d+(?:\.\d+){1,2}', version):
            raise RuntimeError('Invalid VITA_TEST_MSVC_VERSION')
        # Explicit selection avoids an older VS default ASan runtime that may
        # not support the current Windows CRT.
        setup = subprocess.run(f'call "{vcvars}" -vcvars_ver={version} >nul && set', shell=True,
                               capture_output=True, text=True, check=True)
        env = dict(os.environ)
        for line in setup.stdout.splitlines():
            if '=' in line and not line.startswith('='):
                k, v = line.split('=', 1)
                # Windows environment names are case-insensitive.
                for old in list(env):
                    if old.lower() == k.lower():
                        del env[old]
                env[k] = v
        exe = exe.with_suffix('.exe')
        native_path = next(v for k, v in env.items() if k.lower() == 'path')
        compiler = shutil.which('cl.exe', path=native_path)
        if not compiler:
            raise RuntimeError('MSVC environment did not provide cl.exe')
        cmd = [compiler, '/nologo', '/std:c++17', '/EHsc', '/O2', '/Zi', '/MD',
               '/fsanitize=address', '/D_CRT_SECURE_NO_WARNINGS', '/W3',
               *['/D'+d for d in defines], *['/I'+str(p) for p in includes],
               *map(str, sources), '/Fe:'+str(exe), '/link', '/INCREMENTAL:NO']
    else:
        env = os.environ.copy()
        cmd = ['g++', '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-pthread',
               '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-no-pie',
               *['-D'+d for d in defines], *['-I'+str(p) for p in includes],
               *map(str, sources), '-o', str(exe)]
    compiled = subprocess.run(cmd, env=env, cwd=exe.parent, capture_output=True, text=True)
    if compiled.returncode:
        print(compiled.stdout, compiled.stderr)
        compiled.check_returncode()
    return exe, env


def run(exe, env, args=()):
    result = subprocess.run([str(exe), *args], env=env, cwd=exe.parent,
                            capture_output=True, text=True, timeout=30)
    if result.returncode:
        print(result.stdout, result.stderr)
        result.check_returncode()
    return result.stdout
