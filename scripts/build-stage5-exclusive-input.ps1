$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$Project = Split-Path $PSScriptRoot -Parent
$Aosp = Join-Path $Project 'sources\aosp7\complete'
$Toolchain = Join-Path $Project 'toolchains\android-ndk-r15c\toolchains\llvm\prebuilt\windows-x86_64\bin'
$Linker = Join-Path $Project 'toolchains\android-ndk-r15c\toolchains\arm-linux-androideabi-4.9\prebuilt\windows-x86_64\bin'
$Sysroot = Join-Path $Project 'toolchains\android-ndk-r15c\sysroot'
$Crt = Join-Path $Project 'toolchains\android-ndk-r15c\platforms\android-24\arch-arm\usr\lib'
$RootLib = Join-Path $Project 'rootfs-zygote\system\lib'
$Build = Join-Path $Project 'build\stage5-exclusive-input'
$Artifacts = Join-Path $Project 'artifacts'
$Log = Join-Path $Project 'reports\stage5-exclusive-input-build.txt'
$OutputLib = Join-Path $Artifacts 'libinputflinger-exclusive-touch.so'
New-Item -ItemType Directory -Force -Path $Build,(Join-Path $Build 'include\openssl'),$Artifacts,(Split-Path $Log) | Out-Null

# Android 7's libcrypto exports the OpenSSL 1.0.2 SHA-1 API. The source bundle
# omits its public header, so provide only the stable 1.0.2 declarations/layout
# needed to compile EventHub; no shim code is linked into the resulting library.
$ShaHeader = @'
#ifndef OPENSSL_SHA_H
#define OPENSSL_SHA_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SHA_DIGEST_LENGTH 20
typedef struct SHAstate_st { uint32_t h0,h1,h2,h3,h4; uint32_t Nl,Nh; uint32_t data[16]; unsigned int num, md_len; } SHA_CTX;
int SHA1_Init(SHA_CTX*); int SHA1_Update(SHA_CTX*, const void*, size_t); int SHA1_Final(unsigned char*, SHA_CTX*);
#ifdef __cplusplus
}
#endif
#endif
'@
Set-Content -LiteralPath (Join-Path $Build 'include\openssl\sha.h') -Value $ShaHeader -Encoding ascii
Set-Content -LiteralPath $Log -Value "AOSP 7.1 isolated libinputflinger touch-grab build: $(Get-Date -Format s)"
function Invoke-Captured([string]$Executable, [string[]]$Arguments, [string]$Label) {
  $Stdout = Join-Path $Build "$Label.stdout.txt"
  $Stderr = Join-Path $Build "$Label.stderr.txt"
  $Quoted = $Arguments | ForEach-Object { if ($_ -match '\s') { '"' + ($_ -replace '"','\"') + '"' } else { $_ } }
  $Process = Start-Process -FilePath $Executable -ArgumentList $Quoted -NoNewWindow -Wait -PassThru -RedirectStandardOutput $Stdout -RedirectStandardError $Stderr
  if (Test-Path $Stdout) { Get-Content -LiteralPath $Stdout | Add-Content -LiteralPath $Log }
  if (Test-Path $Stderr) { Get-Content -LiteralPath $Stderr | Add-Content -LiteralPath $Log }
  return $Process.ExitCode
}

$Includes = @(
  "-I$(Join-Path $Build 'include')",
  "-I$(Join-Path $Project 'sources\aosp7\external\libcxx\include')",
  "-I$(Join-Path $Aosp 'frameworks\native\services\inputflinger')",
  "-I$(Join-Path $Aosp 'frameworks\native\include')",
  "-I$(Join-Path $Aosp 'frameworks\native\libs\input\include')",
  "-I$(Join-Path $Aosp 'system\core\include')",
  "-I$(Join-Path $Aosp 'system\core\base\include')",
  "-I$(Join-Path $Aosp 'hardware\libhardware\include')",
  "-I$(Join-Path $Aosp 'hardware\libhardware_legacy\include')",
  "-I$(Join-Path $Aosp 'bionic\libc\include')",
  "-I$(Join-Path $Aosp 'bionic\libc\kernel\uapi')",
  "-I$(Join-Path $Aosp 'bionic\libc\kernel\uapi\asm-arm')",
  "-I$(Join-Path $Aosp 'bionic\libm\include')",
  "-I$(Join-Path $Aosp 'bionic\libc\private')",
  "-I$(Join-Path $Project 'sources\aosp7\libnativehelper\include')",
  "-I$(Join-Path $Project 'sources\aosp7\kernel-headers')",
  "-I$(Join-Path $Sysroot 'usr\include')",
  "-I$(Join-Path $Sysroot 'usr\include\arm-linux-androideabi')"
)
$Common = @('-target','armv7a-linux-androideabi25',"--sysroot=$Sysroot",'-nostdinc++','-D_USING_LIBCXX','-std=c++14','-fno-rtti','-fno-exceptions','-fPIC','-O2','-march=armv7-a','-mfloat-abi=softfp','-mfpu=neon','-Wno-attributes','-Wno-expansion-to-defined','-Wno-macro-redefined','-Wno-unknown-attributes') + $Includes
$Sources = @('EventHub.cpp','InputApplication.cpp','InputDispatcher.cpp','InputListener.cpp','InputManager.cpp','InputReader.cpp','InputWindow.cpp')
$Objects = @()
foreach ($Name in $Sources) {
  $Source = Join-Path $Aosp "frameworks\native\services\inputflinger\$Name"
  $Object = Join-Path $Build ($Name -replace '\.cpp$','.o')
  $CompileCode = Invoke-Captured (Join-Path $Toolchain 'clang++.exe') (@('-c') + $Common + @('-Wno-unused-parameter',$Source,'-o',$Object)) ("compile-" + ($Name -replace '\.cpp$',''))
  if ($CompileCode -ne 0) { throw "libinputflinger compile failed for $Name ($CompileCode); see reports/stage5-exclusive-input-build.txt" }
  $Objects += $Object
}
$Gold = Join-Path $Linker 'arm-linux-androideabi-ld.gold.exe'
$Libraries = @('-lbinder','-lcrypto','-lcutils','-linput','-llog','-lutils','-lui','-lhardware_legacy','-lc++','-lc','-lm')
$Link = @('-shared','-Bsymbolic','--no-undefined','--soname=libinputflinger.so','-L',$RootLib,'--rpath-link',$RootLib,(Join-Path $Crt 'crtbegin_so.o')) + $Objects + $Libraries + @((Join-Path $Crt 'crtend_so.o'),'-o',$OutputLib)
$LinkCode = Invoke-Captured $Gold $Link 'link'
if ($LinkCode -ne 0) { throw "libinputflinger link failed ($LinkCode); see reports/stage5-exclusive-input-build.txt" }
$Readelf = Join-Path $Linker 'arm-linux-androideabi-readelf.exe'
$ElfInfo = @(& $Readelf '-h' $OutputLib 2>&1 | ForEach-Object { $_.ToString() })
if (($ElfInfo -join "`n") -notmatch 'Class:\s+ELF32' -or ($ElfInfo -join "`n") -notmatch 'Machine:\s+ARM') { throw 'Built libinputflinger is not ARM32 ELF.' }
$Hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $OutputLib).Hash.ToLowerInvariant()
"sha256=$Hash" | Add-Content -LiteralPath $Log
Get-Item -LiteralPath $OutputLib | Select-Object FullName,Length
"sha256=$Hash"
