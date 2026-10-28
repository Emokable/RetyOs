"""Build/test the portable state logic, then cross-build the ARM32 host controller."""
from pathlib import Path
import hashlib,json,os,subprocess,struct
ROOT=Path(__file__).resolve().parents[1]
RUST=ROOT/'toolchains/rust-1.99.1'
ZIG=ROOT/'toolchains/zig-windows-x86_64-0.13.0/zig.exe'
work=ROOT/'build/session-controller';work.mkdir(parents=True,exist_ok=True)
for name,target in [('arm-linker','arm-linux-musleabihf'),('host-linker','x86_64-windows-gnu')]:
    (work/(name+'.cmd')).write_bytes(f'@echo off\r\n"{ZIG}" cc -target {target} %*\r\n'.encode())
env=os.environ.copy()
env.update(RUSTC=str(RUST/'bin/rustc.exe'),CARGO_HOME=str(ROOT/'toolchains/cargo-local'),
    CARGO_TARGET_DIR=str(ROOT/'build/session-controller-target'),
    CARGO_TARGET_ARMV7_UNKNOWN_LINUX_MUSLEABIHF_LINKER=str(work/'arm-linker.cmd'),
    CARGO_TARGET_X86_64_PC_WINDOWS_GNU_LINKER=str(RUST/'lib/rustlib/x86_64-pc-windows-gnu/bin/self-contained/x86_64-w64-mingw32-gcc.exe'))
env['PATH']=str(RUST/'bin')+os.pathsep+str(RUST/'lib/rustlib/x86_64-pc-windows-gnu/bin/self-contained')+os.pathsep+env['PATH']
manifest=ROOT/'sources/session-controller/Cargo.toml'
cargo=str(RUST/'bin/cargo.exe')
subprocess.run([str(ZIG),'cc','-target','arm-linux-musleabihf','-static','-O2',
                '-Wall','-Wextra','-Werror',str(ROOT/'sources/stage4/native_frame_state.c'),
                '-o',str(ROOT/'artifacts/native-frame-state')],check=True)
subprocess.run([cargo,'test','--offline','--manifest-path',str(manifest)],env=env,check=True)
cross_env=env.copy()
cross_env['CARGO_TARGET_ARMV7_UNKNOWN_LINUX_MUSLEABIHF_LINKER']=str(RUST/'lib/rustlib/x86_64-pc-windows-gnu/bin/rust-lld.exe')
cross_env['CARGO_ENCODED_RUSTFLAGS']='-C\x1flinker-flavor=ld.lld'
subprocess.run([cargo,'build','--offline','--release','--target','armv7-unknown-linux-musleabihf','--manifest-path',str(manifest)],env=cross_env,check=True)
binary=ROOT/'build/session-controller-target/armv7-unknown-linux-musleabihf/release/kindle-session-controller'
out=ROOT/'artifacts/session-controller';out.write_bytes(binary.read_bytes())
elf=out.read_bytes()
assert elf[:6]==b'\x7fELF\x01\x01' and struct.unpack_from('<H',elf,18)[0]==40,'expected ARM32 little-endian ELF'
phoff=struct.unpack_from('<I',elf,28)[0];phsize,phnum=struct.unpack_from('<HH',elf,42)
types=[struct.unpack_from('<I',elf,phoff+i*phsize)[0] for i in range(phnum)]
assert 3 not in types and 2 not in types,'controller must have no dynamic interpreter or dynamic segment'
meta={'status':'built-local-not-deployed','sha256':hashlib.sha256(out.read_bytes()).hexdigest(),
      'bytes':out.stat().st_size,'target':'armv7-unknown-linux-musleabihf','rust':'1.99.1',
      'dependencies':[],'source':'sources/session-controller','deviceSelfTest':False,
      'elf':'ELF32 ARM little-endian, no PT_INTERP/PT_DYNAMIC'}
meta['sourceSha256']={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest()
    for p in sorted((ROOT/'sources/session-controller/src').rglob('*.rs'))}
meta['frameHelperSha256']=hashlib.sha256((ROOT/'artifacts/native-frame-state').read_bytes()).hexdigest()
(ROOT/'reports/session-controller-build-20261026.json').write_text(json.dumps(meta,indent=2)+'\n',encoding='utf-8')
print(json.dumps(meta,indent=2))
