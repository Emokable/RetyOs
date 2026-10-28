"""Build Android 7.1's matching sdcard FUSE daemon for the isolated rootfs."""
from pathlib import Path
import hashlib, subprocess

p=Path(__file__).resolve().parents[1]
aosp=p/'sources/aosp7/complete'
ndk=p/'toolchains/android-ndk-r15c'
binpath=ndk/'toolchains/llvm/prebuilt/windows-x86_64/bin'
gold=ndk/'toolchains/arm-linux-androideabi-4.9/prebuilt/windows-x86_64/bin/arm-linux-androideabi-ld.gold.exe'
gcc=ndk/'toolchains/arm-linux-androideabi-4.9/prebuilt/windows-x86_64/lib/gcc/arm-linux-androideabi/4.9.x/armv7-a/libgcc.a'
crt=ndk/'platforms/android-24/arch-arm/usr/lib'
sysroot=ndk/'sysroot'
src=aosp/'system/core/sdcard/sdcard.c'
build=p/'build/sdcard';build.mkdir(parents=True,exist_ok=True)
includes=[aosp/'system/core/include',aosp/'system/core/libpackagelistparser/include',p/'sources/stage4',aosp/'bionic/libc/include',aosp/'bionic/libc/kernel/uapi',aosp/'bionic/libc/kernel/uapi/asm-arm',sysroot/'usr/include',sysroot/'usr/include/arm-linux-androideabi']
args=[str(binpath/'clang.exe'),'-target','armv7a-linux-androideabi25','--sysroot='+str(sysroot),'-std=gnu11','-O2','-fPIE','-march=armv7-a','-mfloat-abi=softfp','-mfpu=neon','-Wno-unknown-attributes']
for d in includes:args += ['-I',str(d)]
obj=build/'sdcard.o'
subprocess.run(args+['-c',str(src),'-o',str(obj)],check=True)
lib=p/'rootfs-zygote/system/lib'
out=p/'artifacts/sdcard-android7'
subprocess.run([str(gold),'-pie','--no-undefined','-dynamic-linker','/system/bin/linker','-L',str(lib),'--rpath-link',str(lib),str(crt/'crtbegin_dynamic.o'),str(obj),str(gcc),'-lpackagelistparser','-lcutils','-llog','-lc','-lm','-ldl',str(crt/'crtend_android.o'),'-o',str(out)],check=True)
print('artifact='+str(out));print('size='+str(out.stat().st_size));print('sha256='+hashlib.sha256(out.read_bytes()).hexdigest())
