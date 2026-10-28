$ErrorActionPreference = 'Stop'
$Project = Split-Path $PSScriptRoot -Parent
$Toolchain = Join-Path $Project 'toolchains\android-ndk-r15c\toolchains\llvm\prebuilt\windows-x86_64\bin'
$Linker = Join-Path $Project 'toolchains\android-ndk-r15c\toolchains\arm-linux-androideabi-4.9\prebuilt\windows-x86_64\bin'
$Sysroot = Join-Path $Project 'toolchains\android-ndk-r15c\sysroot'
$Crt = Join-Path $Project 'toolchains\android-ndk-r15c\platforms\android-24\arch-arm\usr\lib'
$Lib = Join-Path $Project 'rootfs-zygote\system\lib'
$Hardware = Join-Path $Project 'sources\aosp7\complete\hardware\libhardware'
$Aosp = Join-Path $Project 'sources\aosp7\complete'
$Build = Join-Path $Project 'build\mock-hwcomposer'
$Artifact = Join-Path $Project 'artifacts\hwcomposer-headless.so'
$Overlay = Join-Path $Project 'artifacts\egl-headless-overlay\system\lib\hw\hwcomposer.default.so'
$Log = Join-Path $Project 'reports\mock-hwcomposer-build.txt'
New-Item -ItemType Directory -Force -Path $Build,(Split-Path $Artifact),(Split-Path $Overlay),(Split-Path $Log) | Out-Null
$Source = Join-Path $Project 'sources\stage3\mock_hwcomposer.cpp'
$Includes = @(
  "-I$(Join-Path $Hardware 'include')",
  "-I$(Join-Path $Aosp 'system\core\include')",
  "-I$(Join-Path $Aosp 'frameworks\native\opengl\include')",
  "-I$(Join-Path $Sysroot 'usr\include')",
  "-I$(Join-Path $Sysroot 'usr\include\arm-linux-androideabi')"
)
$Object = Join-Path $Build 'mock-hwcomposer.o'
$CompileArgs = @('-c','-target','armv7a-linux-androideabi25',"--sysroot=$Sysroot",'-std=gnu++11','-fPIC','-O2','-march=armv7-a','-mfloat-abi=softfp','-mfpu=neon') + $Includes + @($Source,'-o',$Object)
$Output = @(& (Join-Path $Toolchain 'clang++.exe') @CompileArgs 2>&1 | ForEach-Object { $_.ToString() })
$Code = $LASTEXITCODE
$Output | Set-Content -LiteralPath $Log -Encoding utf8
if ($Code -ne 0) { throw "mock HWC compile failed ($Code); see reports/mock-hwcomposer-build.txt" }
$LinkArgs = @('-shared','-Bsymbolic','--no-undefined','--soname=hwcomposer.default.so','-L',$Lib,'--rpath-link',$Lib,(Join-Path $Crt 'crtbegin_so.o'),$Object,'-lhardware','-lEGL','-lc',(Join-Path $Crt 'crtend_so.o'),'-o',$Artifact)
$Output = @(& (Join-Path $Linker 'arm-linux-androideabi-ld.gold.exe') @LinkArgs 2>&1 | ForEach-Object { $_.ToString() })
$Code = $LASTEXITCODE
$Output | Add-Content -LiteralPath $Log -Encoding utf8
if ($Code -ne 0) { throw "mock HWC link failed ($Code); see reports/mock-hwcomposer-build.txt" }
Copy-Item -LiteralPath $Artifact -Destination $Overlay -Force
$ReadElf = Join-Path $Linker 'arm-linux-androideabi-readelf.exe'
(& $ReadElf -h $Artifact) + @(& $ReadElf -d $Artifact | Select-String 'NEEDED|SONAME') | Add-Content -LiteralPath $Log
Get-FileHash $Artifact -Algorithm SHA256 | Format-List | Out-String | Add-Content -LiteralPath $Log
Get-Content -LiteralPath $Log -Tail 24
