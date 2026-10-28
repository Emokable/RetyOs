from pathlib import Path
import json,subprocess,difflib,shutil,os
p=Path(__file__).resolve().parents[1];src=p/'sources/aosp7/system/core/init';out=p/'build/container-init';out.mkdir(parents=True,exist_ok=True)
for n in ['action.cpp','init_parser.cpp','parser.cpp','service.cpp','util.cpp']:
 s=(src/n).read_text()
 if n=='service.cpp':
  a=s.index('    std::string scon;');b=s.index('    NOTICE("Starting service',a)
  s=s[:a]+'    std::string scon; // No SELinux policy operations in container prototype.\n'+s[b:]
  s=s.replace('            ZapStdio();','            /* Keep inherited test log descriptors. */')
  s=s.replace('    Service* svc = FindServiceByPid(pid);','    Service* svc = FindServiceByPid(pid);\n    extern void lab_child_exit(const char*,int);\n    if(svc)lab_child_exit(svc->name().c_str(),status);')
  s=s.replace('        ERROR("waitpid failed: %s\\n", strerror(errno));','        if(errno!=ECHILD)ERROR("waitpid failed: %s\\n", strerror(errno));')
  import re
  for option in ['console','critical','group','ioprio','keycodes','onrestart','seclabel','socket','user','writepid','setenv']:
   s=re.sub(r'^\s*\{"'+option+r'"[^\n]*\n','\n',s,flags=re.M)
  s=s.replace('bool Service::Start() {','bool Service::Start() {\n    if(args_.empty() || (args_[0]!="/system/bin/servicemanager-bionic" && args_[0]!="/system/bin/sm-client-bionic" && args_[0]!="/system/bin/init-client-helper"))return false;')
 if n=='parser.cpp':s=s.replace('    va_list ap;','    extern void lab_parse_error();lab_parse_error();\n    va_list ap;',1)
 (out/n).write_bytes(s.encode())
 (out/(n+'.patch')).write_bytes(''.join(difflib.unified_diff((src/n).read_text().splitlines(True),s.splitlines(True),fromfile='a/'+n,tofile='b/'+n)).encode())
ndk=p/'toolchains/android-ndk-r15c'
base=[str(ndk/'toolchains/llvm/prebuilt/windows-x86_64/bin/clang++.exe'),'-target','armv7-none-linux-androideabi','--sysroot='+str(ndk/'platforms/android-24/arch-arm'),'-march=armv7-a','-mfloat-abi=softfp','-std=c++14','-fPIE','-fno-exceptions','-fno-rtti','-Os','-nostdinc++','-Wno-unknown-attributes','-ffunction-sections','-fdata-sections']
for d in [src,p/'sources/aosp7/bionic/libc/include',p/'sources/aosp7/external/libcxx/include',p/'sources/aosp7/system/core/include',p/'sources/aosp7/system/core/base/include',p/'sources/aosp7/external/libselinux/include']:
 base+=['-I',str(d)]
objects=[];logs=[]
for f in [out/n for n in ['action.cpp','init_parser.cpp','parser.cpp','service.cpp','util.cpp']]+[p/'sources/container-init/container_main.cpp']:
 obj=out/(f.name+'.o');r=subprocess.run(base+['-c',str(f),'-o',str(obj)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT);logs.append(r.stdout.decode(errors='replace'))
 if r.returncode:
  (p/'reports/init-build.log').write_text('\n'.join(logs),encoding='utf-8');print(logs[-1]);raise SystemExit(r.returncode)
 objects.append(str(obj));print('compiled',f.name,flush=True)
ndk=p/'toolchains/android-ndk-r15c';lib=ndk/'platforms/android-24/arch-arm/usr/lib';gcc=list((ndk/'toolchains/arm-linux-androideabi-4.9/prebuilt/windows-x86_64/lib/gcc/arm-linux-androideabi').rglob('libgcc.a'))
zig=str(p/'toolchains/zig-windows-x86_64-0.13.0/zig.exe');dest=p/'rootfs-min/system/bin/init-container'
libs=[str(p/'rootfs-min/system/lib'/n) for n in ['libbase.so','libcutils.so','libselinux.so','liblog.so','libc++.so','libdl.so','libc.so','libm.so']]
args=[zig,'ld.lld','-m','armelf_linux_eabi','-pie','--gc-sections','--dynamic-linker','/system/bin/linker','-e','_start',str(lib/'crtbegin_dynamic.o'),*objects,*libs,str(gcc[0]),str(lib/'crtend_android.o'),'-o',str(dest)]
r=subprocess.run(args,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);logs.append(r.stdout.decode(errors='replace'));(p/'reports/init-build.log').write_text('\n'.join(logs),encoding='utf-8');print(logs[-1]);raise SystemExit(r.returncode)
