$ErrorActionPreference = 'Stop'
$Project = Split-Path $PSScriptRoot -Parent
$Ndk = Join-Path $Project 'toolchains/android-ndk-r15c'
$ShortNdk = (New-Object -ComObject Scripting.FileSystemObject).GetFolder($Ndk).ShortPath.Replace('\','/')
$Toolchain = Join-Path $Project 'work/ndk15-space-safe.cmake'
$Original = Get-Content -Raw -LiteralPath (Join-Path $Ndk 'build/cmake/android.toolchain.cmake')
$Patched = $Original.Replace('get_filename_component(ANDROID_NDK "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)', "set(ANDROID_NDK `"$ShortNdk`")")
[IO.File]::WriteAllText($Toolchain, $Patched, (New-Object Text.UTF8Encoding($false)))
$Source = Join-Path $Project 'sources/swiftshader-2018'
$Build = Join-Path $Project 'build/swiftshader-arm-safe'
$Ninja = (Get-Command ninja.exe).Source
& cmake -S $Source -B $Build -G Ninja "-DCMAKE_TOOLCHAIN_FILE=$Toolchain" '-DANDROID_ABI=armeabi-v7a' '-DANDROID_PLATFORM=android-24' '-DANDROID_STL=c++_static' "-DCMAKE_MAKE_PROGRAM=$Ninja" '-DCMAKE_BUILD_TYPE=Release' '-DCMAKE_POLICY_VERSION_MINIMUM=3.5' '-DREACTOR_BACKEND=Subzero' '-DBUILD_SAMPLES=OFF' '-DBUILD_TESTS=OFF' '-DUSE_GROUP_SOURCES=OFF'
if ($LASTEXITCODE -ne 0) { throw 'SwiftShader configuration failed' }
& cmake --build $Build --target libEGL libGLESv2 libGLES_CM --parallel 6 *> (Join-Path $Project 'reports/swiftshader-arm-build.log')
if ($LASTEXITCODE -ne 0) { throw 'SwiftShader build failed; see reports/swiftshader-arm-build.log' }
Get-Item (Join-Path $Build 'libEGL_swiftshader.so'),(Join-Path $Build 'libGLESv2_swiftshader.so'),(Join-Path $Build 'libGLESv1_CM_swiftshader.so') | Get-FileHash -Algorithm SHA256
