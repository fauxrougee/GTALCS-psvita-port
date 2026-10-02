#!/usr/bin/env python3
"""Generate the homebrew package header used by the Vita promoter.

SPDX-License-Identifier: GPL-3.0-or-later
Header layout and fake-package authentication follow VitaShell's
package_installer.c, Copyright (C) 2015-2018 TheFloW.
The template and license are included in vita/updater/.
"""
import argparse
import hashlib
import importlib.util
from pathlib import Path
import struct

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('package_release',ROOT/'tools/vita/package-release.py')
package=importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


def authentication(data):
    sha=hashlib.sha1(data).digest()
    block=bytearray(64)
    block[0:8]=block[8:16]=sha[4:12]
    block[16:20]=sha[12:16]
    block[20:24]=bytes([sha[16],sha[1],sha[2],sha[3]])
    block[24:32]=block[16:24]
    return hashlib.sha1(block).digest()[:16]


def make_head(sfo,template):
    values=package.sfo_values(sfo)
    title=values['TITLE_ID']
    assert len(title)==9 and title.isascii() and title.isalnum() and title.upper()==title
    content=values.get('CONTENT_ID') or f'EP9000-{title}_00-0000000000000000'
    encoded=content.encode('ascii')
    assert len(encoded)==36 and content[7:16]==title
    head=bytearray(template)
    assert len(head)==1072
    head[0x30:0x60]=encoded+b'\0'*(48-len(encoded))
    def word(offset):
        return struct.unpack_from('>I',head,offset)[0]
    def sign(start,length,out):
        assert 0<=start<=start+length<=len(head) and 0<=out<=out+16<=len(head)
        head[out:out+16]=authentication(head[start:start+length])
    sign(0,word(0xD0),word(0xD0))
    sign(word(8),word(0x10)-64,word(0xD4))
    sign(0,word(0xE8),word(0xE8))
    return bytes(head)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sfo',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_bytes(make_head(a.sfo.read_bytes(),(ROOT/'vita/updater/head-template.bin').read_bytes()))
