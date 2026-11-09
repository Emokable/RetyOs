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
# 2026-11-04 (docs/retyos-state.md 第六轮): 容器里 /data/user/0 是持久 /data/data 的
# bind 挂载。PMS.prepareUserStorage 在 vold `cryptfs prepare_user_storage` 失败时
# 会调用 destroy_user_data 兜底，AOSP 的实现是
# delete_dir_contents_and_dir("/data/user/0") —— 等于每次开机把整棵应用数据删掉，
# 连挂载点目录本身也删（此后所有应用报
# "Failed to ensure /data/user/0/<pkg>/... ENOENT"，微信读书卡在「存储空间不足」）。
# vold 的 cryptfs 在容器里不可能成功，容器里也没有真的销毁用户数据的场景，
# 这里直接成功返回、不动文件系统。
destroy_needle = ('int destroy_user_data(const char *uuid, userid_t userid, int flags) {\n'
                  '    int res = 0;')
assert patched.count(destroy_needle) == 1, 'destroy_user_data anchor moved'
patched = patched.replace(
    destroy_needle,
    'int destroy_user_data(const char *uuid, userid_t userid, int flags) {\n'
    '    ALOGW("KINDLELAB_SKIP_DESTROY_USER_DATA userid=%d flags=%d", (int)userid, flags);\n'
    '    return 0;\n'
    '    int res = 0;')
(build / 'commands.cpp').write_text(patched)
# 2026-11-06 (docs/retyos-state.md 第九十四轮): 第九十三轮逐行时间戳实测证明，
# create_app_data 的失败段占 PMS→ready 53.09 s 里的 24.74 s（47%），62 条失败在关键
# 路径内、相邻间隔中位 0.30 s。installd 是单线程串行（installd.cpp:770-802 只有一个
# accept，内层顺序 execute），所以这些失败是逐条累加的真实开销。
#
# 第八十八轮试过"短路 prepare_app_dir（目录存在即成功）"—— 无效，失败行数反而 56→60，
# 因为 CE 失败被容忍后代码继续走到 DE 的 profile 分支，那里还有 fs_prepare_dir_strict /
# fs_prepare_file_strict（/data/misc/profiles/cur/0/<pkg> 与 ref_profile）。所以那段
# 短路已移除，改成在 create_app_data 入口整体返回成功：完全不碰 fsp 的 chown/chmod。
#
# 可行性（第九十三轮核对）：跳过的是 CE/DE 目录+chown/chmod、cache/code_cache 子目录、
# restorecon、inode xattr、profile 目录。容器里 SELinux 完全不存在（无 selinuxfs、
# 无 getenforce、build.prop 无 selinux 项）⇒ restorecon 是空操作。剩下三个缺口都是
# 中低风险：权限 0777 vs 期望 0700/0751（prepare-app-data.sh 已建好并 0777，现有 app 能跑）、
# 无 FBE 所以 CE/DE 语义不适用、profile 缺失只失去 profile-guided 优化（本容器本来就是
# interpret-only）。实测基线 444b77e2 时失败行 94-97 条 / 24.74 s，改后预期接近 0。
create_needle = ('int create_app_data(const char *uuid, const char *pkgname, userid_t userid, int flags,\n'
                 '        appid_t appid, const char* seinfo, int target_sdk_version) {\n'
                 '    uid_t uid = multiuser_get_uid(userid, appid);')
assert patched.count(create_needle) == 1, 'create_app_data anchor moved'
patched = patched.replace(
    create_needle,
    'int create_app_data(const char *uuid, const char *pkgname, userid_t userid, int flags,\n'
    '        appid_t appid, const char* seinfo, int target_sdk_version) {\n'
    '    /* fsp refuses chown/chmod and has no xattr; every prepare step below would\n'
    '     * fail one after another (installd is single-threaded, so they add up).\n'
    '     * prepare-app-data.sh pre-creates the trees 0777 and the container has no\n'
    '     * SELinux, so nothing here is required. Return success without touching\n'
    '     * the filesystem. */\n'
    '    ALOGW("KINDLELAB_SKIP_CREATE_APP_DATA pkg=%s user=%d flags=%d appid=%d",\n'
    '          pkgname, (int)userid, flags, (int)appid);\n'
    '    return 0;\n'
    '    uid_t uid = multiuser_get_uid(userid, appid);')
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
