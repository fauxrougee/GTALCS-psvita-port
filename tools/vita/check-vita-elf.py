#!/usr/bin/env python3
"""Check ARM ELF/Sony ELF load segments after native compilation/conversion."""
from pathlib import Path
import sys
from elftools.elf.elffile import ELFFile

build = Path(sys.argv[1])
names = ['reLCS', 'reLCS.velf']
if '--intro' in sys.argv[2:]:
    names += ['relcs_intro', 'relcs_intro.velf']
for name in names:
    path=build/name
    with path.open('rb') as stream:
        elf=ELFFile(stream)
        assert elf.elfclass==32 and elf.little_endian and elf['e_machine']=='EM_ARM', name
        loads=sorted((s for s in elf.iter_segments() if s['p_type']=='PT_LOAD'), key=lambda s:s['p_vaddr'])
        assert len(loads)>=2, name
        for segment in loads:
            assert segment['p_memsz']>=segment['p_filesz']>0, name
            assert segment['p_offset']+segment['p_filesz']<=path.stat().st_size, name
        for prev,next_segment in zip(loads,loads[1:]):
            assert prev['p_vaddr']+prev['p_memsz']<=next_segment['p_vaddr'], \
                f'{name}: overlapping load segments after SCE metadata insertion'
        assert any(s['p_flags']&1 for s in loads) and any(s['p_flags']&2 for s in loads), name
    print(f'PASS: {name}, ARM32 little-endian, {len(loads)} load segments, no overlap')
