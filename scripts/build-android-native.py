from pathlib import Path
import subprocess,os
p=Path(__file__).resolve().parents[1];ndk=p/'toolchains/android-ndk-r15c';clang=str(ndk/'toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe');zig=str(p/'toolchains/zig-windows-x86_64-0.13.0/zig.exe');lib=ndk/'platforms/android-24/arch-arm/usr/lib'
gcc=ndk/'toolchains/arm-linux-androideabi-4.9/prebuilt/windows-x86_64/lib/gcc/arm-linux-androideabi/4.9.x/armv7-a/libgcc.a'
base=[clang,'-target','armv7-none-linux-androideabi','--sysroot='+str(ndk/'platforms/android-24/arch-arm'),'-march=armv7-a','-mfloat-abi=softfp','-std=gnu11','-fPIE','-Os','-fno-builtin','-ffunction-sections','-fdata-sections','-Wno-unknown-attributes']
for path in [p/'build',p/'sources/aosp7/bionic/libc/include',p/'sources/aosp7/kernel-headers']:base+=['-I',str(path)]
base+=['-include','stdint.h','-include','stddef.h']
(p/'build/ndk-binder.c').write_bytes((p/'build/binder.c').read_text().replace('obj->hdr.type','obj->type').encode())
for name,files in [('init-client-helper',['sources/container-init/init-client-helper.c']),('sm-client-bionic',['build/ndk-binder.c','sources/sm-client.c'])]:
 objs=[]
 for n in files:
  obj=p/'build'/('ndk-'+Path(n).name+'.o');subprocess.run(base+['-c',str(p/n),'-o',str(obj)],check=True);objs.append(str(obj))
 out=p/'rootfs-min/system/bin'/name
 subprocess.run([zig,'ld.lld','-m','armelf_linux_eabi','-pie','--gc-sections','--dynamic-linker','/system/bin/linker','-e','_start',str(lib/'crtbegin_dynamic.o'),*objs,str(p/'rootfs-min/system/lib/libc.so'),str(p/'rootfs-min/system/lib/libdl.so'),str(gcc),str(lib/'crtend_android.o'),'-o',str(out)],check=True)
 print('NDK-native',name,out.stat().st_size,flush=True)
