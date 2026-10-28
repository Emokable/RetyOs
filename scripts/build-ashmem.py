from pathlib import Path
import subprocess
p=Path(__file__).resolve().parents[1];ndk=p/'toolchains/android-ndk-r15c';clang=str(ndk/'toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe');zig=str(p/'toolchains/zig-windows-x86_64-0.13.0/zig.exe');lib=ndk/'platforms/android-24/arch-arm/usr/lib';gcc=ndk/'toolchains/arm-linux-androideabi-4.9/prebuilt/windows-x86_64/lib/gcc/arm-linux-androideabi/4.9.x/armv7-a/libgcc.a'
base=[clang,'-target','armv7-none-linux-androideabi','--sysroot='+str(ndk/'platforms/android-24/arch-arm'),'-march=armv7-a','-mfloat-abi=softfp','-std=gnu11','-Os','-fno-builtin','-Wno-unknown-attributes']
for d in [p/'sources/aosp7/bionic/libc/include',p/'sources/aosp7/system/core/include']:base+=['-I',str(d)]
test=p/'build/ashmem_test.o'
subprocess.run(base+['-fPIE','-c',str(p/'sources/ashmem/ashmem_test.c'),'-o',str(test)],check=True)
subprocess.run([zig,'ld.lld','-m','armelf_linux_eabi','-pie','--dynamic-linker','/system/bin/linker','-e','_start',str(lib/'crtbegin_dynamic.o'),str(test),str(p/'rootfs-min/system/lib/libcutils.so'),str(p/'rootfs-min/system/lib/libc.so'),str(p/'rootfs-min/system/lib/libdl.so'),str(gcc),str(lib/'crtend_android.o'),'-o',str(p/'rootfs-min/system/bin/ashmem-test')],check=True)
print('ashmem semantic test built; framework shim is built by build-graphics-guard.py')
