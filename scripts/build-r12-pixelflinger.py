"""Build an isolated PixelFlinger candidate; no deployment or global artifact replacement."""
from pathlib import Path
import subprocess,hashlib,json,difflib
p=Path(__file__).resolve().parents[1];a=p/'sources/aosp7/complete';n=p/'toolchains/android-ndk-r15c'
sysroot=n/'sysroot';tools=n/'toolchains/llvm/prebuilt/windows-x86_64/bin';gccdir=n/'toolchains/arm-linux-androideabi-4.9/prebuilt/windows-x86_64'
crt=n/'platforms/android-24/arch-arm/usr/lib';lib=p/'rootfs-zygote/system/lib'
build=p/'build/r12-pixelflinger';build.mkdir(exist_ok=True);out=p/'artifacts/r12-pixelflinger';out.mkdir(exist_ok=True)
pixel=a/'system/core/libpixelflinger';original=(pixel/'scanline.cpp').read_text(encoding='utf8')
s=original.replace('#include <string.h>','#include <string.h>\n#include <arm_neon.h>')
s=s.replace('static void scanline_t32cb16(context_t* c);','static void scanline_t32cb16(context_t* c);\nstatic void scanline_rgbx_rgba_clamp(context_t* c);')
anchor='#if DEBUG_NEEDS\n'
assert s.count(anchor)==1
selection='''    static const needs_filter_t rgbx_rgba = {
        { 0x03010101, 0x00000077, { 0x00000002, 0x00000000 } },
        { 0xFFFFFFFF, 0xFFFFFFFF, { 0xFFFFFFFF, 0x0000003F } }
    };
    if(c->state.needs.match(rgbx_rgba)){
        c->scanline=scanline_rgbx_rgba_clamp;
        c->init_y=init_y;c->step_y=step_y__generic;return;
    }
'''
s=s.replace(anchor,selection+anchor)
anchor='static void scanline_t32cb16_clamp(context_t* c)\n{'
assert s.count(anchor)==1
s=s.replace(anchor,(p/'sources/stage3/pixelflinger_rgbx_rgba_fastpath.inc').read_text(encoding='utf8')+'\n'+anchor)
(build/'scanline.cpp').write_bytes(s.encode())
(out/'fastpath.patch').write_text(''.join(difflib.unified_diff(original.splitlines(True),s.splitlines(True),fromfile='a/scanline.cpp',tofile='b/scanline.cpp')),encoding='utf8')
common=['-target','armv7a-linux-androideabi25','--sysroot='+str(sysroot),'-D_USING_LIBCXX','-fPIC','-O2','-march=armv7-a','-mfloat-abi=softfp','-mfpu=neon','-Wno-attributes','-Wno-expansion-to-defined','-Wno-macro-redefined']
for d in [a/'bionic/libc/include',a/'bionic/libm/include',sysroot/'usr/include',sysroot/'usr/include/arm-linux-androideabi',p/'sources/aosp7/external/libcxx/include',a/'bionic/libc/kernel/uapi',a/'bionic/libc/kernel/uapi/asm-arm',a/'bionic/libc/private',a/'system/core/include',pixel/'include',a/'external/safe-iop/include',a/'hardware/libhardware/include',a/'hardware/libhardware_legacy/include',a/'frameworks/native/include',a/'frameworks/native/opengl/include',pixel]:common+=['-I',str(d)]
names=['codeflinger/ARMAssemblerInterface.cpp','codeflinger/ARMAssemblerProxy.cpp','codeflinger/CodeCache.cpp','codeflinger/GGLAssembler.cpp','codeflinger/load_store.cpp','codeflinger/blending.cpp','codeflinger/texturing.cpp','fixed.cpp','picker.cpp','pixelflinger.cpp','trap.cpp','scanline.cpp','format.cpp','clear.cpp','raster.cpp','buffer.cpp','codeflinger/ARMAssembler.cpp','codeflinger/disassem.c','col32cb16blend.S','t32cb16blend.S']
objects=[]
with (p/'reports/r12-pixelflinger-build.txt').open('wb') as log:
 for name in names:
  source=build/'scanline.cpp' if name=='scanline.cpp' else pixel/name
  obj=build/(name.replace('/','_')+'.o');objects.append(str(obj))
  cpp=name.endswith('.cpp');extra=['-nostdinc++','-fno-rtti','-fno-exceptions','-std=c++11','-fstrict-aliasing','-fomit-frame-pointer','-DWITH_LIB_HARDWARE'] if cpp else []
  subprocess.run([str(tools/('clang++.exe' if cpp else 'clang.exe')),*common,*extra,'-c',str(source),'-o',str(obj)],stdout=log,stderr=subprocess.STDOUT,check=True)
 gcc=gccdir/'lib/gcc/arm-linux-androideabi/4.9.x/armv7-a/libgcc.a'
 target=out/'libpixelflinger-headless.so'
 subprocess.run([str(gccdir/'bin/arm-linux-androideabi-ld.gold.exe'),'-shared','-Bsymbolic','--no-undefined','-soname','libpixelflinger-headless.so','-L',str(lib),'--rpath-link',str(lib),str(crt/'crtbegin_so.o'),*objects,str(gcc),'-lcutils','-llog','-lutils','-lhardware_legacy','-lhardware','-lc++','-lc','-lm','-ldl',str(crt/'crtend_so.o'),'-o',str(target)],stdout=log,stderr=subprocess.STDOUT,check=True)
print(target,hashlib.sha256(target.read_bytes()).hexdigest())
bench=p/'sources/stage3/pixelflinger_rgbx_benchmark.cpp';obj=build/'benchmark.o'
subprocess.run([str(tools/'clang++.exe'),*common,'-nostdinc++','-fno-rtti','-fno-exceptions','-std=c++11','-c',str(bench),'-o',str(obj)],check=True)
subprocess.run([str(gccdir/'bin/arm-linux-androideabi-ld.gold.exe'),'-pie','--no-undefined','-dynamic-linker','/system/bin/linker','-L',str(lib),'--rpath-link',str(lib),str(crt/'crtbegin_dynamic.o'),str(obj),str(gcc),'-lc++','-lc','-ldl','-lm',str(crt/'crtend_android.o'),'-o',str(out/'pixel-benchmark')],check=True)
