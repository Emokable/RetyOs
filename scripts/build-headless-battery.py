"""Build explicit virtual battery backend and a general ABI8 service checker."""
from pathlib import Path
import subprocess
import hashlib

p = Path(__file__).resolve().parents[1]
ndk = p/'toolchains/android-ndk-r15c'
aosp = p/'sources/aosp7/complete'
compiler = ndk/'toolchains/llvm/prebuilt/windows-x86_64/bin'
linker = ndk/'toolchains/arm-linux-androideabi-4.9/prebuilt/windows-x86_64/bin'
crt = ndk/'platforms/android-24/arch-arm/usr/lib'
sysroot = ndk/'sysroot'
build = p/'build/headless-battery'
build.mkdir(parents=True, exist_ok=True)
base = ['-target','armv7a-linux-androideabi25','--sysroot='+str(sysroot),
        '-fPIE','-O2','-march=armv7-a','-mfloat-abi=softfp','-Wno-unknown-attributes']
for include in [p/'build',p/'sources/aosp7/kernel-headers',p/'sources/aosp7/libnativehelper/include',aosp/'frameworks/native/include',
                aosp/'system/core/include',p/'sources/aosp7/external/libcxx/include',
                aosp/'bionic/libc/include',aosp/'bionic/libc/kernel/uapi',
                aosp/'bionic/libc/kernel/uapi/asm-arm',sysroot/'usr/include',
                sysroot/'usr/include/arm-linux-androideabi']:
    base += ['-I',str(include)]
src = aosp/'frameworks/native/services/batteryservice'
targets = {
 'headless-battery': [p/'sources/stage3/headless_battery.cpp',
    *[src/name for name in ['BatteryProperties.cpp','BatteryProperty.cpp',
                           'IBatteryPropertiesListener.cpp','IBatteryPropertiesRegistrar.cpp']]],
 'service-check': [p/'sources/stage3/sf_service_check.c',p/'build/ndk-binder.c']}
lib = p/'rootfs-zygote/system/lib'
with (p/'reports/headless-battery-build.txt').open('wb') as log:
 for target,sources in targets.items():
    objects=[]
    for file in sources:
        cpp=file.suffix=='.cpp'
        flags=['-nostdinc++','-std=c++14','-fno-rtti','-fno-exceptions','-D_USING_LIBCXX'] if cpp else []
        obj=build/(file.stem+'.o')
        subprocess.run([str(compiler/('clang++.exe' if cpp else 'clang.exe')),*base,*flags,
                        '-c',str(file),'-o',str(obj)],stdout=log,stderr=subprocess.STDOUT,check=True)
        objects.append(str(obj))
    artifact=p/'artifacts'/target
    subprocess.run([str(linker/'arm-linux-androideabi-ld.gold.exe'),'-pie','--no-undefined',
       '-dynamic-linker','/system/bin/linker','-L',str(lib),'--rpath-link',str(lib),
       str(crt/'crtbegin_dynamic.o'),*objects,'-lbinder','-lutils','-llog','-lc++','-lc','-ldl',
       str(crt/'crtend_android.o'),'-o',str(artifact)],stdout=log,stderr=subprocess.STDOUT,check=True)
    print(target,hashlib.sha256(artifact.read_bytes()).hexdigest())
