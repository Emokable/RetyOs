"""Build graphics-integration guard and a session-lifetime servicemanager."""
from pathlib import Path
import hashlib
import subprocess
import sys

p = Path(__file__).resolve().parents[1]
ndk = p / 'toolchains/android-ndk-r15c'
clang = ndk / 'toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe'
zig = p / 'toolchains/zig-windows-x86_64-0.13.0/zig.exe'
crt = ndk / 'platforms/android-24/arch-arm/usr/lib'
gcc = ndk / 'toolchains/arm-linux-androideabi-4.9/prebuilt/windows-x86_64/lib/gcc/arm-linux-androideabi/4.9.x/armv7-a/libgcc.a'
build = p / 'build/graphics-integration'
build.mkdir(parents=True, exist_ok=True)
base = [str(clang), '-target', 'armv7-none-linux-androideabi',
        '--sysroot=' + str(ndk / 'platforms/android-24/arch-arm'),
        '-march=armv7-a', '-mfloat-abi=softfp', '-std=gnu11', '-fPIE',
        '-Os', '-fno-builtin', '-Wno-unknown-attributes']
for directory in ['build', 'sources/aosp7/bionic/libc/include',
                  'sources/aosp7/system/core/include', 'sources/aosp7/kernel-headers']:
    base += ['-I', str(p / directory)]
manager = (p / 'build/service_manager.c').read_text()
assert manager.count('alarm(15);') == 1
manager_source = build / 'service_manager_session.c'
manager_text = manager.replace(
    '    alarm(15);\n    if(geteuid()!=0)return 2;\n    fprintf(stderr,"LAB ONLY: ABI8 vndbinder root-only 15s\\n");',
    '    const char *session = getenv("KINDLE_ANDROID_SESSION");\n'
    '    if (session && !strcmp(session, "1")) {\n'
    '        alarm(0);\n'
    '        fprintf(stderr,"LAB ONLY: ABI8 vndbinder session-lifetime\\n");\n'
    '    } else {\n'
    '        alarm(540);\n'
    '        fprintf(stderr,"LAB ONLY: ABI8 vndbinder diagnostic 540s\\n");\n'
    '    }\n'
    '    if(geteuid()!=0)return 2;')
assert manager_text != manager
manager_source.write_text(manager_text)
system_manager=build/'service_manager_system.c'
system_text=manager_source.read_text().replace('return u==0;', 'return u==0 || u==1000;')
system_text=system_text.replace('root-only 540s','root+system+keystore 540s')
old='static bool check_mac_perms_from_lookup(pid_t p,uid_t u,const char *x,const char *n){return u==0 || u==1000;}'
new='''static bool check_mac_perms_from_lookup(pid_t p,uid_t u,const char *x,const char *n){
 if(!strcmp(x,"find"))return true;
 if(u==1017)return !strcmp(x,"add") && !strcmp(n,"android.security.keystore");
 if(u==1000 && !strcmp(x,"add")){
  const char *protected[]={"SurfaceFlinger","batteryproperties","netd","media.audio_flinger","media.audio_policy","media.log","broadcastradio","media.sound_trigger_hw","android.security.keystore"};
  for(unsigned i=0;i<sizeof(protected)/sizeof(protected[0]);i++)if(!strcmp(n,protected[i]))return false;
 }
 return u==0 || u==1000;
}'''
assert old in system_text
system_manager.write_text(system_text.replace(old,new))
targets=[
    ('art-guard-graphics', [p / 'sources/ashmem/art_guard.c']),
    ('servicemanager-system', [p / 'build/ndk-binder.c', system_manager])
]
for name,sources in targets:
    objects = []
    for source in sources:
        obj = build / (source.stem + '.o')
        subprocess.run(base + ['-include', 'stdint.h', '-include', 'stddef.h',
                               '-c', str(source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    output = p / 'artifacts' / name
    subprocess.run([str(zig), 'ld.lld', '-m', 'armelf_linux_eabi', '-pie',
                    '--dynamic-linker', '/system/bin/linker', '-e', '_start',
                    str(crt / 'crtbegin_dynamic.o'), *objects,
                    str(p / 'rootfs-min/system/lib/libc.so'),
                    str(p / 'rootfs-min/system/lib/libdl.so'), str(gcc),
                    str(crt / 'crtend_android.o'), '-o', str(output)], check=True)
    print(name, hashlib.sha256(output.read_bytes()).hexdigest())

framework_object=build/'ashmem-framework.o'
subprocess.run(base+['-fPIC','-DASHMEM_FRAMEWORK_RWX_MAX','-c',str(p/'sources/ashmem/ashmem_memfd.c'),'-o',str(framework_object)],check=True)
framework=p/'artifacts/libkindleashmem-framework.so'
subprocess.run([str(zig),'ld.lld','-m','armelf_linux_eabi','-shared','--no-undefined',
                '-soname','libkindleashmem.so',str(framework_object),
                str(p/'rootfs-min/system/lib/libc.so'),'-o',str(framework)],check=True)
print(framework.name,hashlib.sha256(framework.read_bytes()).hexdigest())
egl_object=build/'egl-software-properties.o'
subprocess.run(base+['-fPIC','-c',str(p/'sources/stage3/egl_software_properties.c'),'-o',str(egl_object)],check=True)
egl=p/'artifacts/libegl-software-properties.so'
subprocess.run([str(zig),'ld.lld','-m','armelf_linux_eabi','-shared','--no-undefined',
                '-soname','libegl-software-properties.so',str(egl_object),
                str(p/'rootfs-min/system/lib/libc.so'),str(p/'rootfs-min/system/lib/libdl.so'),'-o',str(egl)],check=True)
print(egl.name,hashlib.sha256(egl.read_bytes()).hexdigest())
