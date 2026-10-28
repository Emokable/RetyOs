"""Build only the controls classes; hidden API declarations never enter the DEX."""
from pathlib import Path
import subprocess,hashlib
p=Path(__file__).resolve().parents[1]
classes=p/'build/system-controls/classes'; classes.mkdir(parents=True,exist_ok=True)
# Avoid stale anonymous classes after controls are removed or reordered.
for stale in classes.glob('KindleSystemControls*.class'): stale.unlink()
out=p/'artifacts/system-controls'; out.mkdir(parents=True,exist_ok=True)
javac=p/'toolchains/temurin8/jdk8u504-b01/bin/javac.exe'
android=p/'toolchains/android-platform-25/android.jar'
sources=[p/'sources/stage4/KindleSystemControls.java',*sorted((p/'sources/stage4/compile-stubs').rglob('*.java'))]
subprocess.run([str(javac),'-encoding','UTF-8','-source','7','-target','7','-bootclasspath',str(android),'-d',str(classes),*[str(f) for f in sources]],check=True)
subprocess.run(['java','-cp',str(p/'toolchains/r8-8.3.37.jar'),'com.android.tools.r8.D8',
    '--min-api','25','--no-desugaring','--lib',str(android),'--output',str(out),
    *[str(f) for f in sorted(classes.glob('KindleSystemControls*.class'))]],check=True)
print('SYSTEM_CONTROLS_DEX_SHA256='+hashlib.sha256((out/'classes.dex').read_bytes()).hexdigest())
subprocess.run([str(p/'toolchains/zig-windows-x86_64-0.13.0/zig.exe'),'cc','-target','arm-linux-musleabihf','-static','-Os','-Wall','-Wextra','-Werror',str(p/'sources/stage4/controls_client.c'),'-o',str(out/'controls-client')],check=True)
