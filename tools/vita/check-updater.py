#!/usr/bin/env python3
"""Run the production update parser and streaming extractor under host ASan.

Windows needs VitaSDK headers and Git for Windows' native zlib1.dll.
Linux needs zlib development headers. No Vita calls or WSL are used.
"""
import argparse
import hashlib
import importlib.util
import os
from pathlib import Path
import shutil
import struct
import tempfile
from zipfile import ZipFile, ZipInfo, ZIP_DEFLATED, ZIP_STORED
from host_build import build,run

ROOT=Path(__file__).resolve().parents[2]
HEADER=ROOT/'vita/updater'

TEST=r'''
#include "core.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
using namespace Update;
std::string text(const char *p) { std::ifstream f(p,std::ios::binary); return {std::istreambuf_iterator<char>(f),{}}; }
bool directory(const char *p) { std::error_code ec; std::filesystem::create_directories(p,ec); return !ec; }
bool noDirectory(const char*) { return false; }
bool cancel(uint64_t,uint64_t) { return false; }
void policy(const std::string &valid) {
    Manifest m; std::string error;
    assert(ParseManifest(valid,m,error)); assert(Version(m.version)==119);
    assert(Version("01.09")<Version("01.10") && Version("02.00")>Version("01.99"));
    assert(Version("1.19")==-1 && Version("01.1x")==-1 && Version("-1.19")==-1);
    assert(LaunchUpdate("-update") && LaunchUpdate("psla:-update"));
    assert(!LaunchUpdate("-updater") && !LaunchUpdate("foo -update") && !LaunchUpdate(nullptr));
    for(auto bad:{"../eboot.bin","boot/../eboot.bin","/eboot.bin","boot//evil","ux0:data/evil",
        "boot\\evil","boot/./intro.vtm","boot/intro.vtm/","userfiles/slot1.b","boot/evil\n"}) assert(!SafePath(bad));
    for(auto good:{"eboot.bin","game.bin","sce_sys/param.sfo","boot/intro.vtm","updater/eboot.bin"}) assert(SafePath(good));
    for(size_t n=0;n<valid.size();++n) assert(!ParseManifest(valid.substr(0,n),m,error));
    for(auto addition:{"\n","version=01.19\n","junk"}) assert(!ParseManifest(valid+addition,m,error));
    auto corrupt=[&](const std::string &find,const std::string &replacement) {
        auto s=valid; auto pos=s.find(find); assert(pos!=std::string::npos); s.replace(pos,find.size(),replacement);
        assert(!ParseManifest(s,m,error));
    };
    corrupt("https://","http://"); corrupt("fauxrougee/","somebody/");
    corrupt("01.19-intro-complete.vpk","01.19-sans-intro.vpk");
    corrupt("version=01.19","version=01.20"); corrupt("size=","size=-");
    corrupt("unpacked_size=","unpacked_size=999999999999"); corrupt("sha256=","sha256=G");
    auto nul=valid; nul[0]=0; assert(!ParseManifest(nul,m,error));
}
int main(int argc,char **argv) {
    assert(argc==5);
    std::string metadata=text(argv[2]),mode=argv[4],error;
    Manifest m; assert(ParseManifest(metadata,m,error));
    if(mode=="policy") { policy(metadata); puts("PASS: release policy, versions, URLs and launch mode"); return 0; }
    std::vector<Entry> entries;
    if(mode=="reject") { assert(!ReadArchive(argv[1],m,entries,error)); puts(error.c_str()); return 0; }
    assert(ReadArchive(argv[1],m,entries,error));
    assert(directory(argv[3]));
    bool ok=ExtractArchive(argv[1],argv[3],m,mode=="mkdir"?noDirectory:directory,
                           mode=="cancel"?cancel:nullptr,error);
    if(mode=="extract") {
        if(!ok) fprintf(stderr,"Extraction: %s\n",error.c_str());
        assert(ok);
        auto raw=text((std::string(argv[3])+"/sce_sys/param.sfo").c_str());
        std::vector<unsigned char> data(raw.begin(),raw.end());
        assert(SfoVersion(data)==m.version && ValidateSfo(data,m.version));
        for(size_t i=0;i<data.size();++i) { auto copy=data; copy[i]^=0xff; SfoVersion(copy); ValidateSfo(copy,m.version); }
        for(size_t n=0;n<data.size();++n) { auto copy=data; copy.resize(n); assert(!ValidateSfo(copy,m.version)); }
        // Arbitrary malformed SFO inputs must remain bounded.
        uint32_t seed=1234;
        for(int i=0;i<2000;++i) {
            std::vector<unsigned char> fuzz(i%512);
            for(auto &c:fuzz) { seed=seed*1664525+1013904223; c=seed>>24; }
            SfoVersion(fuzz); ValidateSfo(fuzz,m.version);
        }
    } else assert(!ok);
    puts(ok?"PASS: streamed extraction and bounded SFO reads":error.c_str());
}
'''

# Import only the four native zlib exports used by production core.cpp.
WINDOWS_ZLIB=r'''
#include <windows.h>
#include <zlib.h>
#include <cassert>
static HMODULE library=LoadLibraryA(ZLIB_PATH);
template<class T> T function(const char *name) { assert(library); auto f=GetProcAddress(library,name); assert(f); return reinterpret_cast<T>(f); }
extern "C" {
uLong ZEXPORT crc32(uLong crc,const Bytef *buf,uInt len) { return function<decltype(&crc32)>("crc32")(crc,buf,len); }
int ZEXPORT inflateInit2_(z_streamp p,int w,const char *v,int s) { return function<decltype(&inflateInit2_)>("inflateInit2_")(p,w,v,s); }
int ZEXPORT inflate(z_streamp p,int flush) { return function<decltype(&inflate)>("inflate")(p,flush); }
int ZEXPORT inflateEnd(z_streamp p) { return function<decltype(&inflateEnd)>("inflateEnd")(p); }
}
'''


def make_sfo(title='RELCS0001',version='01.19',memory=12):
    fields=[('APP_VER',0x204,(version+'\0').encode()),('ATTRIBUTE2',0x404,struct.pack('<I',memory)),
            ('TITLE_ID',0x204,(title+'\0').encode())]
    keys=b''; values=b''; entries=b''
    for name,fmt,value in fields:
        entries+=struct.pack('<HHIII',len(keys),fmt,len(value),len(value),len(values))
        keys+=name.encode()+b'\0'; values+=value
    key_start=20+len(entries); value_start=key_start+len(keys)
    return struct.pack('<5I',0x46535000,0x101,key_start,value_start,len(fields))+entries+keys+values


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sdk',type=Path,default=os.environ.get('VITASDK',ROOT/'sdk/windows/vitasdk'))
    p.add_argument('--vpk',type=Path,help='Also extract and compare a complete built VPK')
    args=p.parse_args()
    with tempfile.TemporaryDirectory(prefix='relcs-update-') as folder:
        temp=Path(folder); source=temp/'test.cpp'; source.write_text(TEST)
        sources=[source,HEADER/'core.cpp']; includes=[HEADER]; defines=[]; libraries=[]
        if os.name=='nt':
            dll=Path(os.environ.get('ProgramFiles','C:/Program Files'))/'Git/mingw64/bin/zlib1.dll'
            if not dll.is_file(): raise SystemExit('Install Git for Windows for native zlib host tests')
            wrapper=temp/'zlib.cpp'; wrapper.write_text(WINDOWS_ZLIB)
            headers=temp/'zlib'; headers.mkdir()
            for name in ['zlib.h','zconf.h']:
                shutil.copy2(args.sdk.resolve()/'arm-vita-eabi/include'/name,headers/name)
            conf=headers/'zconf.h'
            conf.write_text(conf.read_text().replace('#if 1     /* was set to #if 1 by ./configure */',
                                                    '#if 0     /* Windows host has no unistd.h */'))
            sources.append(wrapper); includes.append(headers)
            defines.append('ZLIB_PATH="'+dll.as_posix()+'"')
        else: libraries=['-lz']
        exe,env=build(temp/'test',sources,includes,defines,libraries)
        cases=[]
        def fixture(name,files,compression=ZIP_DEFLATED):
            path=temp/(name+'.vpk')
            with ZipFile(path,'w',compression) as z:
                for filename,data in files:
                    info=filename if isinstance(filename,ZipInfo) else ZipInfo(filename)
                    info.compress_type=compression
                    z.writestr(info,data)
            for filename,_ in files:
                if isinstance(filename,str) and '\\' in filename:
                    path.write_bytes(path.read_bytes().replace(filename.replace('\\','/').encode(),filename.encode()))
            return path
        files=[('eboot.bin',b'SELF'),('game.bin',b'GAME'),('boot/intro.vtm',b'frame'*100000),
               ('sce_sys/param.sfo',make_sfo()),('sce_sys/package/head.bin',b'HEAD')]
        def check(path,mode,unpacked=None,version='01.19'):
            with ZipFile(path) as z: total=sum(i.file_size for i in z.infolist())
            metadata=temp/(path.stem+'.txt')
            metadata.write_text('RELCS-UPDATE-1\n'+f'version={version}\nsize={path.stat().st_size}\n'
                f'unpacked_size={total if unpacked is None else unpacked}\n'
                f'sha256={hashlib.sha256(path.read_bytes()).hexdigest()}\n'
                'url=https://github.com/fauxrougee/GTALCS-psvita-port/releases/download/'
                f'v{version}/reLCS-{version}-intro-complete.vpk\n',newline='\n')
            # Fixtures need to meet the same minimum package size as production.
            assert path.stat().st_size>=1024
            target=temp/(path.stem+'-'+mode)
            output=run(exe,env,[str(path),str(metadata),str(target),mode]); print(path.stem,mode,output.strip())
            if mode=='extract':
                with ZipFile(path) as z:
                    for info in z.infolist(): assert (target/info.filename).read_bytes()==z.read(info)
        good=fixture('deflate',files)
        for mode in ['policy','extract','cancel','mkdir']: check(good,mode)
        check(fixture('stored',files,ZIP_STORED),'extract')
        for name in ['../outside','boot/../outside','boot//outside','boot/./outside','ux0:data/outside','boot\\outside']:
            check(fixture('unsafe'+str(len(cases)),files+[(name,b'padding'*100)]),'reject'); cases.append(name)
        check(fixture('duplicate',files+[files[0]]),'reject')
        symlink=ZipInfo('boot/link'); symlink.create_system=3; symlink.external_attr=0o120777<<16
        check(fixture('symlink',files+[(symlink,b'../somewhere')]),'reject')
        check(fixture('missing-game',files[:1]+files[2:]),'reject')
        check(good,'reject',1024)
        check(fixture('wrong-game',files[:3]+[('sce_sys/param.sfo',make_sfo(title='OTHER0001'))]+files[4:]),'sfo')
        check(fixture('wrong-version',files[:3]+[('sce_sys/param.sfo',make_sfo(version='01.20'))]+files[4:]),'sfo')
        check(fixture('wrong-memory',files[:3]+[('sce_sys/param.sfo',make_sfo(memory=0))]+files[4:]),'sfo')
        # Corrupt local compressed data while preserving a readable directory.
        corrupt=temp/'corrupt.vpk'; raw=bytearray(good.read_bytes())
        with ZipFile(good) as z:
            info=z.getinfo('boot/intro.vtm'); off=info.header_offset
            data_start=off+30+struct.unpack_from('<H',raw,off+26)[0]+struct.unpack_from('<H',raw,off+28)[0]
        raw[data_start+20]^=0x80; corrupt.write_bytes(raw); check(corrupt,'corrupt')
        # Local filenames, sizes and central ranges must agree.
        for label,offset,contents in [('local-name',30,b'x'),('local-size',18,b'\xff'*4),('method',8,b'\x63\0')]:
            path=temp/(label+'.vpk'); raw=bytearray(good.read_bytes()); raw[offset:offset+len(contents)]=contents
            path.write_bytes(raw); check(path,'reject')
        if args.vpk:
            spec=importlib.util.spec_from_file_location('package_release',ROOT/'tools/vita/package-release.py')
            package=importlib.util.module_from_spec(spec); spec.loader.exec_module(package)
            with ZipFile(args.vpk) as z:
                version=package.sfo_values(z.read('sce_sys/param.sfo'))['APP_VER']
            check(args.vpk.resolve(),'extract',version=version)
    print('PASS: native updater tests (ASan); no console installation was performed.')


if __name__=='__main__': main()
