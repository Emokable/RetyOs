from pathlib import Path
import json,subprocess,re,shutil
p=Path(__file__).resolve().parents[1];ndk=p/'toolchains/android-ndk-r15c';src=p/'sources/aosp7/frameworks/native/libs/binder';out=p/'build/libbinder';out.mkdir(parents=True,exist_ok=True)
# Preserve the original kernel header; this is the historical include alias.
(p/'sources/aosp7/kernel-headers/linux/binder.h').write_bytes(b'#include <linux/android/binder.h>\n')
base=[str(ndk/'toolchains/llvm/prebuilt/windows-x86_64/bin/clang++.exe'),'-target','armv7-none-linux-androideabi','--sysroot='+str(ndk/'platforms/android-24/arch-arm'),'-march=armv7-a','-mfloat-abi=softfp','-std=c++14','-fPIC','-fno-exceptions','-fno-rtti','-Os','-nostdinc++','-Wno-unknown-attributes','-ffunction-sections','-fdata-sections','-DANDROID','-D_USING_LIBCXX']
base+=['-DNDEBUG']
for d in [p/'sources/aosp7/libnativehelper/include',p/'sources/aosp7/frameworks/native/include',p/'sources/aosp7/bionic/libc/include',p/'sources/aosp7/external/libcxx/include',p/'sources/aosp7/system/core/include',p/'sources/aosp7/system/core/base/include',p/'sources/aosp7/kernel-headers']:
 base+=['-I',str(d)]
mk=(src/'Android.mk').read_text();section=mk[mk.index('sources :='):mk.index('LOCAL_PATH:=')];names=re.findall(r'\b\w+\.cpp',section);objects=[];logs=[]
for name in names:
 obj=out/(name+'.o');r=subprocess.run(base+['-c',str(src/name),'-o',str(obj)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT);logs.append(r.stdout.decode(errors='replace'))
 if r.returncode:
  (p/'reports/libbinder-build.log').write_text('\n'.join(logs),encoding='utf-8');print(logs[-1]);raise SystemExit(r.returncode)
 objects.append(str(obj));print('compiled',name,flush=True)
zig=str(p/'toolchains/zig-windows-x86_64-0.13.0/zig.exe');gcc=ndk/'toolchains/arm-linux-androideabi-4.9/prebuilt/windows-x86_64/lib/gcc/arm-linux-androideabi/4.9.x/armv7-a/libgcc.a'
libs=[str(p/'rootfs-min/system/lib'/n) for n in ['libutils.so','libcutils.so','liblog.so','libc++.so','libdl.so','libc.so','libm.so']]
r=subprocess.run([zig,'ld.lld','-m','armelf_linux_eabi','-shared','--no-undefined','--gc-sections','-soname','libbinder.so',*objects,*libs,str(gcc),'-o',str(p/'rootfs-min/system/lib/libbinder.so')],stdout=subprocess.PIPE,stderr=subprocess.STDOUT);logs.append(r.stdout.decode(errors='replace'));(p/'reports/libbinder-build.log').write_text('\n'.join(logs),encoding='utf-8');print(logs[-1]);
if r.returncode:raise SystemExit(r.returncode)


obj=out/'libbinder-client.o'
subprocess.run(base+['-c',str(p/'sources/ashmem/libbinder_client.cpp'),'-o',str(obj)],check=True)
lib=ndk/'platforms/android-24/arch-arm/usr/lib'
subprocess.run([zig,'ld.lld','-m','armelf_linux_eabi','-pie','--dynamic-linker','/system/bin/linker','-e','_start',str(lib/'crtbegin_dynamic.o'),str(obj),str(p/'rootfs-min/system/lib/libbinder.so'),*libs,str(gcc),str(lib/'crtend_android.o'),'-o',str(p/'rootfs-min/system/bin/libbinder-client')],check=True)
(p/'rootfs-min/dev/binder').write_bytes(b'')
print('Full libbinder ABI8 library and C++ client built')
