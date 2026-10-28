$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$Project = Split-Path $PSScriptRoot -Parent
$Ndk = Join-Path $Project 'toolchains\android-ndk-r15c'
$Compiler = Join-Path $Ndk 'toolchains\llvm\prebuilt\windows-x86_64\bin\clang++.exe'
$Linker = Join-Path $Ndk 'toolchains\arm-linux-androideabi-4.9\prebuilt\windows-x86_64\bin\arm-linux-androideabi-ld.gold.exe'
$ReadElf = Join-Path $Ndk 'toolchains\arm-linux-androideabi-4.9\prebuilt\windows-x86_64\bin\arm-linux-androideabi-readelf.exe'
$Sysroot = Join-Path $Ndk 'sysroot'
$Crt = Join-Path $Ndk 'platforms\android-24\arch-arm\usr\lib'
$Lib = Join-Path $Project 'rootfs-zygote\system\lib'
$Aosp = Join-Path $Project 'sources\aosp7\complete'
$Source = Join-Path $Project 'sources\stage3\mock_gralloc.cpp'
$Core = Join-Path $Project 'sources\stage5\hwtcon_frame_core.cpp'
$KernelInclude = Join-Path $Project 'sources\device-kernel-colorsoft\include'
$Build = Join-Path $Project 'build\stage5\hwtcon-gralloc'
$Artifact = Join-Path $Project 'artifacts\stage5\gralloc.hwtcon.so'
$Overlay = Join-Path $Project 'artifacts\stage5\hwtcon-test-overlay\system\lib\hw\gralloc.default.so'
$Log = Join-Path $Project 'reports\stage5-hwtcon-gralloc-build.txt'
New-Item -ItemType Directory -Force -Path $Build,(Split-Path $Artifact),(Split-Path $Overlay) | Out-Null
$Includes = @(
  "-I$(Join-Path $Aosp 'hardware\libhardware\include')",
  "-I$(Join-Path $Aosp 'system\core\include')",
  "-I$(Join-Path $Sysroot 'usr\include')",
  "-I$(Join-Path $Sysroot 'usr\include\arm-linux-androideabi')",
  "-I$KernelInclude",
  "-I$(Join-Path $Project 'sources\stage5')",
  "-I$(Join-Path $Project 'sources\aosp7\external\libcxx\include')",
  "-I$(Join-Path $Project 'sources\aosp7\complete\bionic\libc\include')",
  "-I$(Join-Path $Project 'sources\aosp7\complete\bionic\libm\include')",
  "-I$(Join-Path $Project 'sources\aosp7\complete\bionic\libc\kernel\uapi')",
  "-I$(Join-Path $Project 'sources\aosp7\complete\bionic\libc\kernel\uapi\asm-arm')",
  "-I$(Join-Path $Project 'sources\aosp7\complete\bionic\libc\private')"
)
$Common = @('-target','armv7a-linux-androideabi25',"--sysroot=$Sysroot",'-O2','-fPIC','-march=armv7-a','-mfloat-abi=softfp','-mfpu=neon','-std=gnu++11') + $Includes
$GrallocObject = Join-Path $Build 'mock-gralloc-hwtcon.o'
$CoreObject = Join-Path $Build 'hwtcon-frame-core.o'
$ErrorActionPreference = 'Continue'
$Output = @(& $Compiler @Common '-DKINDLE_HWTCON_BACKEND' '-c' $Source '-o' $GrallocObject 2>&1 | ForEach-Object { $_.ToString() })
if ($LASTEXITCODE -ne 0) { $Output | Set-Content -LiteralPath $Log -Encoding utf8; throw 'HWTCON gralloc compile failed.' }
$Output += @(& $Compiler @Common '-c' $Core '-o' $CoreObject 2>&1 | ForEach-Object { $_.ToString() })
if ($LASTEXITCODE -ne 0) { $Output | Set-Content -LiteralPath $Log -Encoding utf8; throw 'HWTCON frame core compile failed.' }
$LinkArgs = @('-shared','-Bsymbolic','--no-undefined','--soname=gralloc.default.so','-L',$Lib,'--rpath-link',$Lib,(Join-Path $Crt 'crtbegin_so.o'),$GrallocObject,$CoreObject,'-lcutils','-lhardware','-llog','-lc++','-lc','-lm','-ldl',(Join-Path $Crt 'crtend_so.o'),'-o',$Artifact)
$Output += @(& $Linker @LinkArgs 2>&1 | ForEach-Object { $_.ToString() })
if ($LASTEXITCODE -ne 0) { $Output | Set-Content -LiteralPath $Log -Encoding utf8; throw 'HWTCON gralloc link failed.' }
$ErrorActionPreference = 'Stop'
Copy-Item -LiteralPath $Artifact -Destination $Overlay -Force
$Output += @(& $ReadElf '-h' $Artifact 2>&1 | ForEach-Object { $_.ToString() })
$Output += @(& $ReadElf '-d' $Artifact 2>&1 | Select-String 'NEEDED|SONAME' | ForEach-Object { $_.ToString() })
$Output += 'sha256=' + (Get-FileHash -LiteralPath $Artifact -Algorithm SHA256).Hash
$Output | Set-Content -LiteralPath $Log -Encoding utf8
Get-Content -LiteralPath $Log -Tail 22
