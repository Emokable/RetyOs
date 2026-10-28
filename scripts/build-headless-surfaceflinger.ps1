$ErrorActionPreference = 'Stop'
$Project = Split-Path $PSScriptRoot -Parent
$Aosp = Join-Path $Project 'sources\aosp7\complete'
$Toolchain = Join-Path $Project 'toolchains\android-ndk-r15c\toolchains\llvm\prebuilt\windows-x86_64\bin'
$Linker = Join-Path $Project 'toolchains\android-ndk-r15c\toolchains\arm-linux-androideabi-4.9\prebuilt\windows-x86_64\bin'
$Sysroot = Join-Path $Project 'toolchains\android-ndk-r15c\sysroot'
$Crt = Join-Path $Project 'toolchains\android-ndk-r15c\platforms\android-24\arch-arm\usr\lib'
$RootLib = Join-Path $Project 'rootfs-zygote\system\lib'
$Libgcc = Join-Path $Project 'toolchains\android-ndk-r15c\toolchains\arm-linux-androideabi-4.9\prebuilt\windows-x86_64\lib\gcc\arm-linux-androideabi\4.9.x\armv7-a\libgcc.a'
$SfDir = Join-Path $Aosp 'frameworks\native\services\surfaceflinger'
$Build = Join-Path $Project 'build\headless-surfaceflinger'
$Artifacts = Join-Path $Project 'artifacts'
$Log = Join-Path $Project 'reports\headless-surfaceflinger-build.txt'
$LibOutput = Join-Path $Artifacts 'libsurfaceflinger-headless.so'
$BinOutput = Join-Path $Artifacts 'surfaceflinger-headless'
$ServiceCheckOutput = Join-Path $Artifacts 'sf-service-check'
$LogRedirectOutput = Join-Path $Artifacts 'libheadlesslog.so'
New-Item -ItemType Directory -Force -Path $Build,$Artifacts,(Split-Path $Log) | Out-Null
Set-Content -LiteralPath $Log -Value "AOSP Android 7.1 HWC1 SurfaceFlinger headless build: $(Get-Date -Format s)"
$Includes = @(
  "-I$(Join-Path $Aosp 'hardware\libhardware\include')",
  "-I$(Join-Path $Aosp 'hardware\libhardware_legacy\include')",
  "-I$(Join-Path $Aosp 'system\core\include')",
  "-I$(Join-Path $Aosp 'system\core\libpixelflinger\include')",
  "-I$(Join-Path $Aosp 'frameworks\native\include')",
  "-I$(Join-Path $Aosp 'frameworks\native\opengl\include')",
  "-I$(Join-Path $Aosp 'frameworks\base\include')",
  "-I$(Join-Path $Project 'sources\aosp7\libnativehelper\include')",
  "-I$(Join-Path $Aosp 'external\safe-iop\include')",
  "-I$(Join-Path $Project 'sources\aosp7\external\libcxx\include')",
  "-I$(Join-Path $Aosp 'bionic\libc\include')",
  "-I$(Join-Path $Aosp 'bionic\libm\include')",
  "-I$(Join-Path $Aosp 'bionic\libc\private')",
  "-I$(Join-Path $Aosp 'bionic\libc\kernel\uapi')",
  "-I$(Join-Path $Aosp 'bionic\libc\kernel\uapi\asm-arm')",
  "-I$(Join-Path $Sysroot 'usr\include')",
  "-I$(Join-Path $Sysroot 'usr\include\arm-linux-androideabi')",
  "-I$SfDir"
)
$Common = @('-target','armv7a-linux-androideabi25',"--sysroot=$Sysroot",'-nostdinc++','-D_USING_LIBCXX','-std=c++14','-fno-rtti','-fno-exceptions','-fPIC','-O2','-march=armv7-a','-mfloat-abi=softfp','-mfpu=neon','-DGL_GLEXT_PROTOTYPES','-DEGL_EGLEXT_PROTOTYPES','-DVSYNC_EVENT_PHASE_OFFSET_NS=1000000','-DSF_VSYNC_EVENT_PHASE_OFFSET_NS=1000000','-DPRESENT_TIME_OFFSET_FROM_VSYNC_NS=0','-DMAX_VIRTUAL_DISPLAY_DIMENSION=0','-Wno-attributes','-Wno-expansion-to-defined','-Wno-macro-redefined','-Wno-unknown-attributes') + $Includes
function Compile-Source([string]$Source, [string]$Object, [string]$Label) {
  $Args = @('-c') + $Common + @('-DLOG_TAG="SurfaceFlinger"',$Source,'-o',$Object)
  $Output = @(& (Join-Path $Toolchain 'clang++.exe') @Args 2>&1 | ForEach-Object { $_.ToString() })
  $Code = $LASTEXITCODE
  $Output | Add-Content -LiteralPath $Log
  if ($Code -ne 0) { throw "SurfaceFlinger source compile failed: $Label ($Code); see reports/headless-surfaceflinger-build.txt" }
}
$Sources = @(
 'Client.cpp','DisplayDevice.cpp','DispSync.cpp','EventControlThread.cpp','EventThread.cpp',
 'FenceTracker.cpp','FrameTracker.cpp','Layer.cpp','LayerDim.cpp','MessageQueue.cpp',
 'MonitoredProducer.cpp','SurfaceFlingerConsumer.cpp','Transform.cpp',
 'DisplayHardware/FramebufferSurface.cpp','DisplayHardware/HWC2.cpp','DisplayHardware/HWC2On1Adapter.cpp','DisplayHardware/HWComposer_hwc1.cpp',
 'DisplayHardware/PowerHAL.cpp','DisplayHardware/VirtualDisplaySurface.cpp','Effects/Daltonizer.cpp',
 'EventLog/EventLog.cpp','RenderEngine/Description.cpp','RenderEngine/Mesh.cpp','RenderEngine/Program.cpp',
 'RenderEngine/ProgramCache.cpp','RenderEngine/GLExtensions.cpp','RenderEngine/RenderEngine.cpp',
 'RenderEngine/Texture.cpp','RenderEngine/GLES10RenderEngine.cpp','RenderEngine/GLES11RenderEngine.cpp',
 'RenderEngine/GLES20RenderEngine.cpp','SurfaceFlinger_hwc1.cpp'
)
$Objects = @()
foreach ($Relative in $Sources) {
  $Source = Join-Path $SfDir $Relative
  $Object = Join-Path $Build (($Relative -replace '[/\\.]','_') + '.o')
  Compile-Source $Source $Object $Relative
  $Objects += $Object
}
$GpuSource = Join-Path $Project 'sources\stage3\surfaceflinger_headless_gpu_service.cpp'
$GpuObject = Join-Path $Build 'headless-gpu-service.o'
Compile-Source $GpuSource $GpuObject 'headless GPU service'
$Objects += $GpuObject
$Gold = Join-Path $Linker 'arm-linux-androideabi-ld.gold.exe'
$Libs = @('-lcutils','-llog','-ldl','-lhardware','-lutils','-lEGL','-lGLESv1_CM','-lGLESv2','-lbinder','-lui','-lgui','-lpowermanager','-lvulkan','-lc++','-lc','-lm')
$Link = @('-shared','-Bsymbolic','--no-undefined','--soname=libsurfaceflinger-headless.so','-L',$RootLib,'--rpath-link',$RootLib,(Join-Path $Crt 'crtbegin_so.o')) + $Objects + @($Libgcc) + $Libs + @((Join-Path $Crt 'crtend_so.o'),'-o',$LibOutput)
$Output = @(& $Gold @Link 2>&1 | ForEach-Object { $_.ToString() })
$Code = $LASTEXITCODE; $Output | Add-Content -LiteralPath $Log
if ($Code -ne 0) { throw "libsurfaceflinger link failed ($Code); see build log" }

$MainSource = Join-Path $SfDir 'main_surfaceflinger.cpp'
$MainObject = Join-Path $Build 'main_surfaceflinger.o'
Compile-Source $MainSource $MainObject 'main_surfaceflinger.cpp'
$PropertySource = Join-Path $Project 'sources\stage3\headless_property_stubs.c'
$PropertyObject = Join-Path $Build 'headless-property-stubs.o'
$KernelIncludes = @("-I$(Join-Path $Aosp 'bionic\libc\kernel\uapi')","-I$(Join-Path $Aosp 'bionic\libc\kernel\uapi\asm-arm')")
$CcArgs = @('-c','-target','armv7a-linux-androideabi25',"--sysroot=$Sysroot") + $KernelIncludes + @('-fPIE','-O2','-march=armv7-a','-mfloat-abi=softfp','-mfpu=neon',$PropertySource,'-o',$PropertyObject)
$Output = @(& (Join-Path $Toolchain 'clang.exe') @CcArgs 2>&1 | ForEach-Object { $_.ToString() })
$Code = $LASTEXITCODE; $Output | Add-Content -LiteralPath $Log
if ($Code -ne 0) { throw "Headless property stubs compile failed ($Code); see build log" }
$ExeLink = @('-pie','--no-undefined','--export-dynamic','-dynamic-linker','/system/bin/linker','-L',$RootLib,'-L',$Artifacts,'--rpath-link',$RootLib,(Join-Path $Crt 'crtbegin_dynamic.o'),$MainObject,$PropertyObject,'-lsurfaceflinger-headless','-lsigchain','-lcutils','-llog','-lbinder','-lutils','-ldl','-lc++','-lc',(Join-Path $Crt 'crtend_android.o'),'-o',$BinOutput)
$Output = @(& $Gold @ExeLink 2>&1 | ForEach-Object { $_.ToString() })
$Code = $LASTEXITCODE; $Output | Add-Content -LiteralPath $Log
if ($Code -ne 0) { throw "SurfaceFlinger executable link failed ($Code); see build log" }
$BinderSource = Join-Path $Project 'build\ndk-binder.c'
$CheckSource = Join-Path $Project 'sources\stage3\sf_service_check.c'
$BinderObject = Join-Path $Build 'sf-binder.o'
$CheckObject = Join-Path $Build 'sf-service-check.o'
$CheckIncludes = @(
  "-I$(Join-Path $Project 'build')",
  "-I$(Join-Path $Project 'sources\aosp7\kernel-headers')",
  "-I$(Join-Path $Aosp 'bionic\libc\include')",
  "-I$(Join-Path $Aosp 'bionic\libc\kernel\uapi')",
  "-I$(Join-Path $Aosp 'bionic\libc\kernel\uapi\asm-arm')",
  "-I$(Join-Path $Sysroot 'usr\include')"
)
foreach ($Input in @(@($BinderSource,$BinderObject),@($CheckSource,$CheckObject))) {
  $Source = $Input[0]; $Object = $Input[1]
  $CompileArgs = @('-c','-target','armv7a-linux-androideabi25',"--sysroot=$Sysroot") + $CheckIncludes + @('-fPIE','-O2','-march=armv7-a','-mfloat-abi=softfp','-mfpu=neon',$Source,'-o',$Object)
  $Output = @(& (Join-Path $Toolchain 'clang.exe') @CompileArgs 2>&1 | ForEach-Object { $_.ToString() })
  $Code = $LASTEXITCODE; $Output | Add-Content -LiteralPath $Log
  if ($Code -ne 0) { throw "SurfaceFlinger service-check compile failed: $Source ($Code); see build log" }
}
$CheckLink = @('-pie','-dynamic-linker','/system/bin/linker','-L',$RootLib,'--rpath-link',$RootLib,(Join-Path $Crt 'crtbegin_dynamic.o'),$BinderObject,$CheckObject,'-lc','-ldl',(Join-Path $Crt 'crtend_android.o'),'-o',$ServiceCheckOutput)
$Output = @(& $Gold @CheckLink 2>&1 | ForEach-Object { $_.ToString() })
$Code = $LASTEXITCODE; $Output | Add-Content -LiteralPath $Log
if ($Code -ne 0) { throw "SurfaceFlinger service-check link failed ($Code); see build log" }
$LogSource = Join-Path $Project 'sources\stage3\headless_log_redirect.c'
$LogObject = Join-Path $Build 'headless-log-redirect.o'
$LogArgs = @('-c','-fPIC','-target','armv7a-linux-androideabi25',"--sysroot=$Sysroot", "-I$(Join-Path $Sysroot 'usr\include\arm-linux-androideabi')",'-O2','-march=armv7-a','-mfloat-abi=softfp','-mfpu=neon',$LogSource,'-o',$LogObject)
$Output = @(& (Join-Path $Toolchain 'clang.exe') @LogArgs 2>&1 | ForEach-Object { $_.ToString() })
$Code = $LASTEXITCODE; $Output | Add-Content -LiteralPath $Log
if ($Code -ne 0) { throw "Headless log redirect compile failed ($Code); see build log" }
$LogLink = @('-shared','-Bsymbolic','--no-undefined','-soname','libheadlesslog.so','-L',$RootLib,'--rpath-link',$RootLib,(Join-Path $Crt 'crtbegin_so.o'),$LogObject,'-ldl','-lc',(Join-Path $Crt 'crtend_so.o'),'-o',$LogRedirectOutput)
$Output = @(& $Gold @LogLink 2>&1 | ForEach-Object { $_.ToString() })
$Code = $LASTEXITCODE; $Output | Add-Content -LiteralPath $Log
if ($Code -ne 0) { throw "Headless log redirect link failed ($Code); see build log" }
$ReadElf = Join-Path $Linker 'arm-linux-androideabi-readelf.exe'
@(& $ReadElf -h $LibOutput | Select-String 'Class:|Machine:|Flags:') + @(& $ReadElf -d $LibOutput | Select-String 'NEEDED|SONAME') + @(& $ReadElf -h $BinOutput | Select-String 'Class:|Machine:|Flags:') + @(& $ReadElf -d $BinOutput | Select-String 'NEEDED') + @(& $ReadElf -h $ServiceCheckOutput | Select-String 'Class:|Machine:|Flags:') + @(& $ReadElf -d $ServiceCheckOutput | Select-String 'NEEDED') | Add-Content -LiteralPath $Log
Get-FileHash $LibOutput,$BinOutput,$ServiceCheckOutput,$LogRedirectOutput -Algorithm SHA256 | Format-Table Path,Hash -AutoSize | Out-String | Add-Content -LiteralPath $Log
Get-Content -LiteralPath $Log -Tail 60
