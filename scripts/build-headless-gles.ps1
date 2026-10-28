$ErrorActionPreference = 'Continue'
$Project = Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $Project

$Toolchain = Join-Path $Project 'toolchains\android-ndk-r15c\toolchains\llvm\prebuilt\windows-x86_64\bin'
$Cxx = Join-Path $Toolchain 'clang++.exe'
$Cc = Join-Path $Toolchain 'clang.exe'
$Gold = Join-Path $Project 'toolchains\android-ndk-r15c\toolchains\arm-linux-androideabi-4.9\prebuilt\windows-x86_64\bin\arm-linux-androideabi-ld.gold.exe'
$Libgcc = Join-Path $Project 'toolchains\android-ndk-r15c\toolchains\arm-linux-androideabi-4.9\prebuilt\windows-x86_64\lib\gcc\arm-linux-androideabi\4.9.x\armv7-a\libgcc.a'
$Sysroot = Join-Path $Project 'toolchains\android-ndk-r15c\sysroot'
$CrtDir = Join-Path $Project 'toolchains\android-ndk-r15c\platforms\android-24\arch-arm\usr\lib'
$RootfsLib = Join-Path $Project 'rootfs-zygote\system\lib'
$Build = Join-Path $Project 'build\headless-gles'
$Artifacts = Join-Path $Project 'artifacts'
$Log = Join-Path $Project 'reports\headless-gles-build.txt'
New-Item -ItemType Directory -Force $Build, $Artifacts | Out-Null
Set-Content -LiteralPath $Log -Value "Android 7 ARMv7 software GLES build attempt: $(Get-Date -Format s)"

$Includes = @(
    "-I$(Join-Path $Project 'sources\aosp7\complete\bionic\libc\include')",
    "-I$(Join-Path $Project 'sources\aosp7\complete\bionic\libm\include')",
    "-I$(Join-Path $Sysroot 'usr\include')",
    "-I$(Join-Path $Sysroot 'usr\include\arm-linux-androideabi')",
    "-I$(Join-Path $Project 'sources\aosp7\external\libcxx\include')",
    "-I$(Join-Path $Project 'sources\aosp7\complete\bionic\libc\kernel\uapi')",
    "-I$(Join-Path $Project 'sources\aosp7\complete\bionic\libc\kernel\uapi\asm-arm')",
    "-I$(Join-Path $Project 'sources\aosp7\complete\bionic\libc\private')",
    "-I$(Join-Path $Project 'sources\aosp7\complete\system\core\include')",
    "-I$(Join-Path $Project 'sources\aosp7\complete\system\core\libpixelflinger\include')",
    "-I$(Join-Path $Project 'sources\aosp7\complete\external\safe-iop\include')",
    "-I$(Join-Path $Project 'sources\aosp7\complete\hardware\libhardware\include')",
    "-I$(Join-Path $Project 'sources\aosp7\complete\hardware\libhardware_legacy\include')",
    "-I$(Join-Path $Project 'sources\aosp7\complete\frameworks\native\include')",
    "-I$(Join-Path $Project 'sources\aosp7\complete\frameworks\native\opengl\include')"
)
$Common = @('-target', 'armv7a-linux-androideabi25', "--sysroot=$Sysroot", '-D_USING_LIBCXX', '-fPIC', '-O2', '-march=armv7-a', '-mfloat-abi=softfp', '-mfpu=neon', '-Wno-attributes', '-Wno-expansion-to-defined', '-Wno-macro-redefined') + $Includes

function Compile-Source([string]$Compiler, [string]$Source, [string]$Object, [string[]]$Extra) {
    $Stdlib = @()
    if ($Compiler -eq $Cxx) { $Stdlib = @('-nostdinc++', '-fno-rtti', '-fno-exceptions') }
    $Args = @('-c') + $Common + $Stdlib + $Extra + @($Source, '-o', $Object)
    & $Compiler @Args 2>> $Log
    if ($LASTEXITCODE -ne 0) {
        Add-Content -LiteralPath $Log -Value "compile_failed=$Source exit=$LASTEXITCODE"
        throw "Compile failed: $Source"
    }
    Add-Content -LiteralPath $Log -Value "compiled=$Source"
}

$PixelDir = Join-Path $Project 'sources\aosp7\complete\system\core\libpixelflinger'
$PixelSources = @(
    'codeflinger/ARMAssemblerInterface.cpp', 'codeflinger/ARMAssemblerProxy.cpp',
    'codeflinger/CodeCache.cpp', 'codeflinger/GGLAssembler.cpp',
    'codeflinger/load_store.cpp', 'codeflinger/blending.cpp', 'codeflinger/texturing.cpp',
    'fixed.cpp', 'picker.cpp', 'pixelflinger.cpp', 'trap.cpp', 'scanline.cpp',
    'format.cpp', 'clear.cpp', 'raster.cpp', 'buffer.cpp', 'codeflinger/ARMAssembler.cpp'
)
$PixelObjects = @()
foreach ($Relative in $PixelSources) {
    $Source = Join-Path $PixelDir $Relative
    $Object = Join-Path $Build ('pixel-' + ($Relative -replace '[/\\.]', '_') + '.o')
    Compile-Source $Cxx $Source $Object @('-std=c++11', '-fstrict-aliasing', '-fomit-frame-pointer', '-DWITH_LIB_HARDWARE')
    $PixelObjects += $Object
}
$CSource = Join-Path $PixelDir 'codeflinger\disassem.c'
$CObject = Join-Path $Build 'pixel-disassem.o'
Compile-Source $Cc $CSource $CObject @('-std=gnu99')
$PixelObjects += $CObject
foreach ($Relative in @('col32cb16blend.S', 't32cb16blend.S')) {
    $Source = Join-Path $PixelDir $Relative
    $Object = Join-Path $Build ('pixel-' + ($Relative -replace '[/\\.]', '_') + '.o')
    Compile-Source $Cc $Source $Object @()
    $PixelObjects += $Object
}

$LibPixel = Join-Path $Artifacts 'libpixelflinger-headless.so'
$LinkArgs = @('-shared', '-Bsymbolic', '--soname=libpixelflinger-headless.so', '-L', $RootfsLib, '--rpath-link', $RootfsLib, (Join-Path $CrtDir 'crtbegin_so.o')) + $PixelObjects + @($Libgcc, '-lcutils', '-llog', '-lutils', '-lhardware_legacy', '-lhardware', '-lc++', '-lc', '-lm', '-ldl', (Join-Path $CrtDir 'crtend_so.o'), '-o', $LibPixel)
& $Gold @LinkArgs 2>> $Log
if ($LASTEXITCODE -ne 0) {
    Add-Content -LiteralPath $Log -Value "link_failed=libpixelflinger exit=$LASTEXITCODE"
    throw 'libpixelflinger link failed; inspect reports/headless-gles-build.txt'
}
Add-Content -LiteralPath $Log -Value "linked=$LibPixel"

$AgLDir = Join-Path $Project 'sources\aosp7\complete\frameworks\native\opengl\libagl'
$AgSources = @(
    'egl.cpp', 'state.cpp', 'texture.cpp', 'Tokenizer.cpp', 'TokenManager.cpp',
    'TextureObjectManager.cpp', 'BufferObjectManager.cpp', 'array.cpp', 'fp.cpp',
    'light.cpp', 'matrix.cpp', 'mipmap.cpp', 'primitives.cpp', 'vertex.cpp'
)
$AgObjects = @()
foreach ($Relative in $AgSources) {
    $Source = Join-Path $AgLDir $Relative
    $Object = Join-Path $Build ('agl-' + ($Relative -replace '[/\\.]', '_') + '.o')
    # The AOSP EGL loader resolves eglGetDisplay and eglGetProcAddress with dlsym.
    # Keep the driver entry points in .dynsym; hidden visibility makes them local
    # and leaves the loader with a null function table.
    Compile-Source $Cxx $Source $Object @('-std=c++11', '-marm', '-DLOG_TAG="libagl"', '-DGL_GLEXT_PROTOTYPES', '-DEGL_EGLEXT_PROTOTYPES')
    $AgObjects += $Object
}
foreach ($Relative in @('fixed_asm.S', 'iterators.S')) {
    $Source = Join-Path $AgLDir $Relative
    $Object = Join-Path $Build ('agl-' + ($Relative -replace '[/\\.]', '_') + '.o')
    Compile-Source $Cc $Source $Object @('-marm')
    $AgObjects += $Object
}

$LibAg = Join-Path $Artifacts 'libGLES_android-headless.so'
$AgLinkArgs = @('-shared', '-Bsymbolic', '--soname=libGLES_android-headless.so', '-L', $RootfsLib, '-L', $Artifacts, '--rpath-link', $RootfsLib, (Join-Path $CrtDir 'crtbegin_so.o')) + $AgObjects + @('-lcutils', '-lhardware', '-lutils', '-llog', '-lpixelflinger-headless', '-lETC1', '-lui', '-lc++', '-lc', '-lm', '-ldl', (Join-Path $CrtDir 'crtend_so.o'), '-o', $LibAg)
& $Gold @AgLinkArgs 2>> $Log
if ($LASTEXITCODE -ne 0) {
    Add-Content -LiteralPath $Log -Value "link_failed=libGLES_android exit=$LASTEXITCODE"
    throw 'libGLES_android link failed; inspect reports/headless-gles-build.txt'
}
Add-Content -LiteralPath $Log -Value "linked=$LibAg"
$OverlayLib = Join-Path $Artifacts 'egl-headless-overlay\system\lib'
New-Item -ItemType Directory -Force -Path (Join-Path $OverlayLib 'egl') | Out-Null
Copy-Item -LiteralPath $LibPixel -Destination (Join-Path $OverlayLib 'libpixelflinger-headless.so') -Force
Copy-Item -LiteralPath $LibAg -Destination (Join-Path $OverlayLib 'egl\libGLES_android.so') -Force
Add-Content -LiteralPath $Log -Value "staged=$OverlayLib"
Get-FileHash $LibPixel, $LibAg -Algorithm SHA256 | Format-Table -AutoSize | Out-String | Add-Content -LiteralPath $Log
Get-Content -LiteralPath $Log
