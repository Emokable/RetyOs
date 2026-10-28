"""Build a tiny root-only Android package-service dump helper."""
from pathlib import Path
import shutil
import subprocess
import zipfile

root = Path(__file__).resolve().parents[1]
jdk = root / 'toolchains/temurin8/jdk8u504-b01/bin'
javac = jdk / 'javac.exe' if (jdk / 'javac.exe').is_file() else 'javac'
android_jar = root / 'toolchains/android-platform-25/android.jar'
source = root / 'sources/stage4/PackageServiceDump.java'
classes = root / 'build/package-service-dump/classes'
dex_dir = root / 'build/package-service-dump/dex'
artifact = root / 'artifacts/package-service-dump.jar'
for path in (classes, dex_dir, artifact.parent):
    path.mkdir(parents=True, exist_ok=True)
for path in classes.rglob('*.class'):
    path.unlink()
for path in dex_dir.glob('*.dex'):
    path.unlink()
subprocess.run([str(javac), '-encoding', 'UTF-8', '-Xlint:-options', '-source', '7',
                '-target', '7', '-bootclasspath', str(android_jar), '-d', str(classes),
                str(source)], check=True)
subprocess.run(['java', '-cp', str(root / 'toolchains/r8-8.3.37.jar'),
                'com.android.tools.r8.D8', '--min-api', '25', '--no-desugaring',
                '--lib', str(android_jar), '--output', str(dex_dir),
                *[str(path) for path in sorted(classes.rglob('*.class'))]], check=True)
temporary = artifact.with_suffix('.jar.tmp')
with zipfile.ZipFile(temporary, 'w', compression=zipfile.ZIP_STORED) as archive:
    archive.write(dex_dir / 'classes.dex', 'classes.dex')
temporary.replace(artifact)
print('artifact=' + str(artifact))
print('bytes=' + str(artifact.stat().st_size))
