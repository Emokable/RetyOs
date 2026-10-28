from pathlib import Path
import hashlib
import subprocess

p = Path(__file__).resolve().parents[1]
ndk = p / 'toolchains/android-ndk-r15c'
clang = ndk / 'toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe'
zig = p / 'toolchains/zig-windows-x86_64-0.13.0/zig.exe'
libc = p / 'rootfs-min/system/lib/libc.so'
obj = p / 'build/stage4-userxattr-compat.o'
output = p / 'artifacts/libkindle-userxattr-compat.so'
output.parent.mkdir(parents=True, exist_ok=True)
subprocess.run([
    str(clang), '-target', 'armv7-none-linux-androideabi',
    '--sysroot=' + str(ndk / 'platforms/android-24/arch-arm'),
    '-march=armv7-a', '-mfloat-abi=softfp', '-std=gnu11', '-Os',
    '-fPIC', '-fno-builtin', '-Wno-unknown-attributes',
    '-c', str(p / 'sources/stage4/userxattr_compat.c'), '-o', str(obj)
], check=True)
subprocess.run([
    str(zig), 'ld.lld', '-m', 'armelf_linux_eabi', '-shared',
    '--no-undefined', '-soname', 'libkindle-userxattr-compat.so',
    str(obj), str(libc), '-o', str(output)
], check=True)
print('artifact=' + str(output))
print('sha256=' + hashlib.sha256(output.read_bytes()).hexdigest())
