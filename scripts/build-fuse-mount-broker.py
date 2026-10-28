"""Build the narrow ARMv7 FUSE mount broker used by persistent Android sessions."""
from pathlib import Path
import hashlib
import subprocess

p = Path(__file__).resolve().parents[1]
ndk = p / 'toolchains/android-ndk-r15c'
clang = ndk / 'toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe'
zig = p / 'toolchains/zig-windows-x86_64-0.13.0/zig.exe'
crt = ndk / 'platforms/android-24/arch-arm/usr/lib'
build = p / 'build/fuse-mount-broker'
build.mkdir(parents=True, exist_ok=True)
obj = build / 'fuse_mount_broker.o'
subprocess.run([
    str(clang), '-target', 'armv7-none-linux-androideabi',
    '--sysroot=' + str(ndk / 'platforms/android-24/arch-arm'),
    '-march=armv7-a', '-mfloat-abi=softfp', '-std=gnu11', '-fPIE', '-Os',
    '-Wall', '-Wextra', '-Werror', '-I', str(p / 'sources/stage4'),
    '-c', str(p / 'sources/stage4/fuse_mount_broker.c'), '-o', str(obj)
], check=True)
output = p / 'artifacts/fuse-mount-broker'
subprocess.run([
    str(zig), 'ld.lld', '-m', 'armelf_linux_eabi', '-pie',
    '--dynamic-linker', '/system/bin/linker', '-e', '_start',
    str(crt / 'crtbegin_dynamic.o'), str(obj),
    str(p / 'rootfs-min/system/lib/libc.so'),
    str(p / 'rootfs-min/system/lib/libdl.so'),
    str(crt / 'crtend_android.o'), '-o', str(output)
], check=True)
print('artifact=' + str(output))
print('size=' + str(output.stat().st_size))
print('sha256=' + hashlib.sha256(output.read_bytes()).hexdigest())
