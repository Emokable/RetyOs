$ErrorActionPreference = 'Stop'
$Project = Split-Path $PSScriptRoot -Parent
$Zig = Join-Path $Project 'toolchains\zig-windows-x86_64-0.13.0\zig.exe'
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $Project 'toolchains\cache-global'
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $Project 'toolchains\cache-local'
$Source = Join-Path $Project 'sources\stage5\hwtcon_frame_probe.cpp'
$HeaderDir = Join-Path $Project 'sources\device-kernel-colorsoft\include'
$Build = Join-Path $Project 'build\stage5\hwtcon-probe'
$Artifact = Join-Path $Project 'artifacts\stage5\hwtcon-frame-probe'
$Log = Join-Path $Project 'reports\stage5-hwtcon-probe-build.txt'
New-Item -ItemType Directory -Force -Path $Build,(Split-Path $Artifact) | Out-Null
$Output = @(& $Zig 'c++' '-target' 'arm-linux-musleabihf' '-mcpu=cortex_a7' '-static' '-Os' '-fno-exceptions' '-fno-rtti' '-nostdlib++' "-I$HeaderDir" $Source '-o' $Artifact 2>&1 | ForEach-Object { $_.ToString() })
if ($LASTEXITCODE -ne 0) { $Output | Set-Content -LiteralPath $Log -Encoding utf8; throw 'Static HWTCON probe build failed.' }
$ReadElf = Join-Path $Project 'toolchains\android-ndk-r15c\toolchains\arm-linux-androideabi-4.9\prebuilt\windows-x86_64\bin\arm-linux-androideabi-readelf.exe'
$Output += @(& $ReadElf '-h' $Artifact 2>&1 | ForEach-Object { $_.ToString() })
if (@(& $ReadElf '-l' $Artifact) | Select-String 'INTERP|Requesting program interpreter') { throw 'Static HWTCON probe unexpectedly needs a dynamic interpreter.' }
$Output += 'linkage=static musl; no dynamic interpreter'
$Output += 'sha256=' + (Get-FileHash -LiteralPath $Artifact -Algorithm SHA256).Hash
$Output | Set-Content -LiteralPath $Log -Encoding utf8
Get-Content -LiteralPath $Log -Tail 16
