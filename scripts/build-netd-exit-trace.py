"""Build a disposable ARMv7 preload that logs the caller of netd exit()."""
from pathlib import Path
import hashlib
import subprocess

p = Path(__file__).resolve().parents[1]
ndk = p / 'toolchains/android-ndk-r15c'
compiler = ndk / 'toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe'
linker = ndk / 'toolchains/arm-linux-androideabi-4.9/prebuilt/windows-x86_64/bin/arm-linux-androideabi-ld.gold.exe'
crt = ndk / 'platforms/android-24/arch-arm/usr/lib'
sysroot = ndk / 'sysroot'
build = p / 'build/netd-exit-trace'
build.mkdir(parents=True, exist_ok=True)
obj = build / 'netd_exit_trace.o'
subprocess.run([
    str(compiler), '-target', 'armv7a-linux-androideabi25',
    '--sysroot=' + str(sysroot),
    '-isystem', str(ndk / 'platforms/android-24/arch-arm/usr/include'),
    '-fPIC', '-O2', '-march=armv7-a',
    '-mfloat-abi=softfp', '-Wno-unknown-attributes', '-c',
    str(p / 'sources/stage3/netd_exit_trace.c'), '-o', str(obj)
], check=True)
output = p / 'artifacts/libnetd-exit-trace.so'
subprocess.run([
    str(linker), '-shared', '--no-undefined',
    '-soname', 'libnetd-exit-trace.so', '-L',
    str(p / 'rootfs-min/system/lib'), '--rpath-link',
    str(p / 'rootfs-min/system/lib'), str(crt / 'crtbegin_so.o'),
    str(obj), '-ldl', '-lc', str(crt / 'crtend_so.o'), '-o', str(output)
], check=True)
data = output.read_bytes()
if data[:4] != b'\x7fELF' or data[4] != 1 or data[5] != 1:
    raise SystemExit('expected a 32-bit little-endian ARM ELF')
print(f'{output.name} bytes={len(data)} sha256={hashlib.sha256(data).hexdigest()}')
