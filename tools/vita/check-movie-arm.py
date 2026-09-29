#!/usr/bin/env python3
"""Execute the release Vita ELF's real ARM decoder with Unicorn, for every frame.

No replacement LZ4 or CRC implementation is used. CPU/memory are emulated;
the Vita's memcpy/memmove/memset imports use equivalent memory operations.
this does not simulate Vita display/audio drivers or predict device performance.
Requires Python unicorn and pyelftools. Run on Windows (also works on Linux).
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import time
import warnings
warnings.filterwarnings('ignore',category=UserWarning,module='unicorn')
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,
    UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC,UC_ARM_REG_C1_C0_2,UC_ARM_REG_FPEXC)

ROOT=Path(__file__).resolve().parents[2]
RAW=640*544*4
SRC,DST,STACK,RETURN,PLANES=0x10000000,0x20000000,0x30000000,0x40000000,0x50000000


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--limit',type=int,default=0)
    p.add_argument('--media',type=Path,default=ROOT/'build/vita-intro/intro.vtm')
    p.add_argument('--elf',type=Path,default=ROOT/'build/vita-intro/relcs_intro')
    args=p.parse_args()
    manifest=json.loads(args.media.with_suffix('.json').read_text())
    cpu=Uc(UC_ARCH_ARM,UC_MODE_ARM)
    elfpath=args.elf
    with elfpath.open('rb') as f:
        elf=ELFFile(f)
        for segment in elf.iter_segments():
            if segment['p_type']!='PT_LOAD': continue
            base=segment['p_vaddr']&~4095
            length=(segment['p_vaddr']+segment['p_memsz']-base+4095)&~4095
            cpu.mem_map(base,length)
            cpu.mem_write(segment['p_vaddr'],segment.data())
        symbols={s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
    for base,size in [(SRC,2*1024*1024),(DST,2*1024*1024),(STACK,65536),(RETURN,4096),(PLANES,2*1024*1024)]:
        cpu.mem_map(base,size)
    cpu.reg_write(UC_ARM_REG_C1_C0_2,0xf00000)
    cpu.reg_write(UC_ARM_REG_FPEXC,0x40000000)
    def clib(uc,address,size,name):
        dest,src,length=[uc.reg_read(r) for r in [UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2]]
        assert length<=RAW
        data=bytes([src&255])*length if name=='sceClibMemset' else bytes(uc.mem_read(src,length))
        uc.mem_write(dest,data)
        uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
    for name in ['sceClibMemcpy','sceClibMemmove','sceClibMemset']:
        cpu.hook_add(UC_HOOK_CODE,clib,name,begin=symbols[name],end=symbols[name])
    def call(name,*values):
        for reg,value in zip([UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3],values):
            cpu.reg_write(reg,value)
        cpu.reg_write(UC_ARM_REG_SP,STACK+65536-16)
        cpu.reg_write(UC_ARM_REG_LR,RETURN|1)
        try:
            cpu.emu_start(symbols[name],RETURN,timeout=5000000)
        except Exception:
            pc=cpu.reg_read(UC_ARM_REG_PC)
            print(f'ARM fault: {name} at {pc:08x}, bytes={bytes(cpu.mem_read(pc,8)).hex()}',flush=True)
            raise
        assert cpu.reg_read(UC_ARM_REG_PC)==RETURN, f'ARM call did not finish: {name}'
        assert cpu.reg_read(UC_ARM_REG_SP)==STACK+65536-16
        return cpu.reg_read(UC_ARM_REG_R0)
    start=time.monotonic()
    with args.media.open('rb') as movie:
        movie.seek(80)
        index=[struct.unpack('<QII',movie.read(16)) for _ in range(manifest['frames'])]
        limit=args.limit or len(index)
        for n,(offset,size,crc) in enumerate(index[:limit]):
            movie.seek(offset); data=movie.read(size)
            cpu.mem_write(SRC,data)
            assert call('crc32',0,SRC,size)==crc, f'ARM CRC frame {n}'
            cpu.mem_write(DST,b'\xa5'*64)
            cpu.mem_write(DST+64+RAW,b'\x5a'*64)
            if manifest['format']=='VITAMV02':
                assert call('LZ4_decompress_safe',SRC,PLANES,size,RAW*3//4)==RAW*3//4, f'ARM decode frame {n}'
                call('IntroExpandColorPlanes',PLANES,DST+64,RAW//4)
            else:
                assert call('LZ4_decompress_safe',SRC,DST+64,size,RAW)==RAW, f'ARM decode frame {n}'
            rgba=cpu.mem_read(DST+64,RAW)
            assert hashlib.sha256(rgba).hexdigest()==manifest['frame_sha256'][n], f'ARM pixels frame {n}'
            assert cpu.mem_read(DST,64)==b'\xa5'*64 and cpu.mem_read(DST+64+RAW,64)==b'\x5a'*64
            if n%250==0: print(f'Actual Vita ARM decoder: {n+1}/{limit}',flush=True)
    print(f'PASS: release ARM CRC/LZ4/colour machine code decoded {limit} frames with exact RGBA hashes and intact bounds ({time.monotonic()-start:.1f}s emulation).')
    print('Vita ELF SHA256:',hashlib.sha256(elfpath.read_bytes()).hexdigest())


if __name__=='__main__':
    main()
