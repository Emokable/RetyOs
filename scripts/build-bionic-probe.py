from pathlib import Path
import subprocess,os
p=Path(__file__).resolve().parents[1]
os.environ['ZIG_GLOBAL_CACHE_DIR']=str(p/'toolchains/cache-global');os.environ['ZIG_LOCAL_CACHE_DIR']=str(p/'toolchains/cache-local')
zig=str(p/'toolchains/zig-windows-x86_64-0.13.0/zig.exe')
objects=[]
for name in ['bionic-start.S','bionic-probe.c']:
 obj=p/'build'/(name+'.o');objects.append(str(obj))
 subprocess.run([zig,'cc','-target','arm-linux-musleabi','-mcpu=cortex_a7','-nostdlib','-fno-stack-protector','-fPIE','-Os','-c',str(p/'sources/stage3'/name),'-o',str(obj)],check=True)
subprocess.run([zig,'ld.lld','-m','armelf_linux_eabi','-pie','--dynamic-linker','/system/bin/linker','-e','_start',*objects,str(p/'rootfs-min/system/lib/libc.so'),str(p/'rootfs-min/system/lib/libdl.so'),'-o',str(p/'rootfs-min/system/bin/bionic-probe')],check=True)
import struct
d=(p/'rootfs-min/system/bin/bionic-probe').read_bytes();off=struct.unpack_from('<I',d,28)[0];sz,n=struct.unpack_from('<HH',d,42)
assert any(struct.unpack_from('<I',d,off+i*sz)[0]==3 for i in range(n)),'PT_INTERP missing'
print('Bionic probe: explicit Android PT_INTERP verified')
