#!/usr/bin/env python3
"""Exercise the production CMake patch step in standalone and nested sources."""
from pathlib import Path
import hashlib
import re
import subprocess
import tempfile
from build import locate

ROOT = Path(__file__).resolve().parents[2]


def main():
    cmake = (ROOT/'vita/CMakeLists.txt').read_text()
    # Run the actual configuration step, without needing a target SDK.
    step = cmake[cmake.index('set(LIBRW_PATCH '):
                 cmake.index('set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS')]
    patch = (ROOT/'vita/librw-psp2.patch').read_bytes()
    names = re.findall(r'^\+\+\+ b/(.+)$', patch.decode(), re.M)
    assert names and all('..' not in Path(n).parts and not Path(n).is_absolute() for n in names)
    with tempfile.TemporaryDirectory(prefix='relcs-patch-') as tmp:
        base = Path(tmp).resolve()
        for scenario in ['standalone', 'checkout', 'nested-checkout']:
            parent = base/scenario
            parent.mkdir()
            if scenario != 'standalone':
                subprocess.run(['git', 'init', '--quiet', str(parent)], check=True)
            root = parent/'archive' if scenario == 'nested-checkout' else parent
            root.mkdir(exist_ok=True)
            for name in names:
                dest = root/'vendor/librw'/name
                dest.parent.mkdir(parents=True, exist_ok=True)
                dest.write_bytes((ROOT/'vendor/librw'/name).read_bytes())
            (root/'vita').mkdir()
            target = root/'vita/librw-psp2.patch'
            target.write_bytes(patch)
            git = ['git', '--work-tree='+root.as_posix(), 'apply',
                   '--directory=vendor/librw', '--ignore-whitespace']
            # Works with either a pristine checkout or an already built one.
            reverse = subprocess.run(git+['--reverse', '--check', str(target)],
                                     cwd=root, capture_output=True)
            if reverse.returncode == 0:
                subprocess.run(git+['--reverse', str(target)], cwd=root, check=True)
            subprocess.run(git+['--check', str(target)], cwd=root, check=True)
            script = root/'patch.cmake'
            script.write_text(f'set(ROOT "{root.as_posix()}")\n'
                              f'set(LIBRW "{(root/"vendor/librw").as_posix()}")\n'
                              f'set(CMAKE_CURRENT_SOURCE_DIR "{(root/"vita").as_posix()}")\n'+step)

            def hashes():
                return {n: hashlib.sha256((root/'vendor/librw'/n).read_bytes()).hexdigest() for n in names}

            before = hashes()
            subprocess.run([locate('cmake'), '-P', str(script)], cwd=root, check=True)
            after = hashes()
            assert after != before, 'Configuration silently skipped the patch: '+scenario
            subprocess.run(git+['--reverse', '--check', str(target)], cwd=root, check=True)
            subprocess.run([locate('cmake'), '-P', str(script)], cwd=root, check=True)
            assert hashes() == after, 'Second configuration changed patched sources'
            print('PASS:', scenario, 'correct patch destination and idempotent configuration')


if __name__ == '__main__':
    main()
