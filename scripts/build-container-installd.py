"""Build installd with an explicit no-SELinux path for the isolated container.

Real filesystem permissions and xattr failures are preserved. No fake labels or
successful ownership stubs are provided; host SELinux state is never changed.
"""
from pathlib import Path
import difflib
import hashlib
import subprocess

p = Path(__file__).resolve().parents[1]
aosp = p / 'sources/aosp7/complete'
source = aosp / 'frameworks/native/cmds/installd'
ndk = p / 'toolchains/android-ndk-r15c'
binpath = ndk / 'toolchains/llvm/prebuilt/windows-x86_64/bin'
gold = ndk / 'toolchains/arm-linux-androideabi-4.9/prebuilt/windows-x86_64/bin/arm-linux-androideabi-ld.gold.exe'
gcc = ndk / 'toolchains/arm-linux-androideabi-4.9/prebuilt/windows-x86_64/lib/gcc/arm-linux-androideabi/4.9.x/armv7-a/libgcc.a'
crt = ndk / 'platforms/android-24/arch-arm/usr/lib'
sysroot = ndk / 'sysroot'
build = p / 'build/container-installd'
build.mkdir(parents=True, exist_ok=True)
original = (source / 'commands.cpp').read_text()
needle = 'static int restorecon_app_data_lazy(const std::string& path, const char* seinfo, uid_t uid) {'
assert original.count(needle) == 1
patched = original.replace(needle, needle + '\n    if (is_selinux_enabled() <= 0) return 0; // Container has no SELinux label store.\n')
patched = patched.replace('arraysize(dex2oat_compiler_filter_arg)', 'sizeof(dex2oat_compiler_filter_arg)')
(build / 'commands.cpp').write_text(patched)
(p / 'reports/container-installd.patch').write_text(''.join(difflib.unified_diff(
    original.splitlines(True), patched.splitlines(True), fromfile='a/commands.cpp', tofile='b/commands.cpp')))
includes = [source, aosp / 'frameworks/native/include', aosp / 'system/core/include',
            aosp / 'system/core/logwrapper/include',
            aosp / 'system/core/base/include', p / 'sources/aosp7/external/libselinux/include',
            p / 'sources/aosp7/external/libcxx/include', aosp / 'bionic/libc/include',
            aosp / 'bionic/libm/include', aosp / 'bionic/libc/kernel/uapi',
            aosp / 'bionic/libc/kernel/uapi/asm-arm', sysroot / 'usr/include',
            sysroot / 'usr/include/arm-linux-androideabi']
common = ['-target', 'armv7a-linux-androideabi25', '--sysroot=' + str(sysroot),
          '-O2', '-fPIE', '-march=armv7-a', '-mfloat-abi=softfp', '-mfpu=neon',
          '-Wno-unknown-attributes']
for directory in includes:
    common += ['-I', str(directory)]
objects = []
log = p / 'reports/container-installd-build.txt'
with log.open('wb') as output:
    for file in [source / 'installd.cpp', build / 'commands.cpp', source / 'globals.cpp',
                 source / 'utils.cpp', aosp / 'frameworks/native/libs/diskusage/dirsize.c']:
        cpp = file.suffix == '.cpp'
        compiler = binpath / ('clang++.exe' if cpp else 'clang.exe')
        flags = ['-nostdinc++', '-std=c++14', '-fno-exceptions', '-fno-rtti', '-D_USING_LIBCXX'] if cpp else ['-std=gnu11']
        obj = build / (file.stem + '.o')
        subprocess.run([str(compiler), *common, *flags, '-c', str(file), '-o', str(obj)],
                       stdout=output, stderr=subprocess.STDOUT, check=True)
        objects.append(str(obj))
    lib = p / 'rootfs-zygote/system/lib'
    artifact = p / 'artifacts/installd-container'
    subprocess.run([str(gold), '-pie', '--no-undefined', '-dynamic-linker', '/system/bin/linker',
                    '-L', str(lib), '--rpath-link', str(lib), str(crt / 'crtbegin_dynamic.o'),
                    *objects, str(gcc), '-lbase', '-lcutils', '-llog', '-llogwrap', '-lselinux',
                    '-lc++', '-lc', '-lm', '-ldl', str(crt / 'crtend_android.o'), '-o', str(artifact)],
                   stdout=output, stderr=subprocess.STDOUT, check=True)
print(artifact.name, hashlib.sha256(artifact.read_bytes()).hexdigest())
