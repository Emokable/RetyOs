"""Cross-compile AOSP's default null audio HAL locally; never contact a device."""
from pathlib import Path
import hashlib
import json
import subprocess

p = Path(__file__).resolve().parents[1]
ndk = p/'toolchains/android-ndk-r15c'
build = p/'build/null-audio'
build.mkdir(parents=True, exist_ok=True)
source = p/'sources/local-audio/audio_hw.c'
manifest = json.loads((source.parent/'manifest.json').read_text())
compiler = ndk/'toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe'
linker = ndk/'toolchains/arm-linux-androideabi-4.9/prebuilt/windows-x86_64/bin/arm-linux-androideabi-ld.gold.exe'
crt = ndk/'platforms/android-24/arch-arm/usr/lib'
sysroot = ndk/'sysroot'
obj = build/'audio_hw.o'
output = p/'artifacts/audio.primary.default.so'
includes = [source.parent/'include',p/'sources/aosp7/complete/hardware/libhardware/include',
            p/'sources/aosp7/complete/system/core/include',sysroot/'usr/include',
            sysroot/'usr/include/arm-linux-androideabi']
args = [str(compiler),'-target','armv7a-linux-androideabi25','--sysroot='+str(sysroot),
        '-std=gnu11','-fPIC','-O2','-march=armv7-a','-mfloat-abi=softfp',
        '-Wno-unknown-attributes','-Wno-unused-parameter']
for include in includes:args += ['-I',str(include)]
with (p/'reports/null-audio-build.txt').open('wb') as log:
    subprocess.run(args+['-c',str(source),'-o',str(obj)],stdout=log,stderr=subprocess.STDOUT,check=True)
    libs=p/'rootfs-zygote/system/lib'
    subprocess.run([str(linker),'-shared','--no-undefined','-soname','audio.primary.default.so',
                    '-L',str(libs),'--rpath-link',str(libs),str(crt/'crtbegin_so.o'),str(obj),
                    '-lcutils','-llog','-lc','-lm',str(crt/'crtend_so.o'),'-o',str(output)],
                   stdout=log,stderr=subprocess.STDOUT,check=True)
print(output.name,hashlib.sha256(output.read_bytes()).hexdigest())
