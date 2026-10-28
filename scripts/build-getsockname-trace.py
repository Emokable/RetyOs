from pathlib import Path
import subprocess

p = Path(__file__).resolve().parents[1]
ndk = p / 'toolchains/android-ndk-r15c'
clang = ndk / 'toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe'
zig = p / 'toolchains/zig-windows-x86_64-0.13.0/zig.exe'
lib = ndk / 'platforms/android-24/arch-arm/usr/lib'
source = p / 'sources/ashmem/getsockname_trace.c'
obj = p / 'build/getsockname_trace.o'
out = p / 'artifacts/libgetsockname-trace.so'
out.parent.mkdir(parents=True, exist_ok=True)
subprocess.run([str(clang), '-target', 'armv7-none-linux-androideabi',
                '--sysroot=' + str(ndk / 'platforms/android-24/arch-arm'),
                '-march=armv7-a', '-mfloat-abi=softfp', '-std=gnu11', '-Os',
                '-fPIC', '-c', str(source), '-o', str(obj)], check=True)
subprocess.run([str(zig), 'ld.lld', '-m', 'armelf_linux_eabi', '-shared',
                '--no-undefined', '-soname', 'libgetsockname-trace.so',
                str(obj), str(lib / 'libdl.so'), str(p / 'rootfs-min/system/lib/libc.so'),
                '-o', str(out)], check=True)
print(out)
