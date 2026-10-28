"""Build and verify a minimal, offline Android 7.1 ARM APK probe."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import zipfile
import os

p = Path(__file__).resolve().parents[1]
subprocess.run([__import__('sys').executable,str(p/'scripts/prepare-reader-pinyin.py')],check=True)
jdk8 = p / 'toolchains/temurin8/jdk8u504-b01/bin'
javac = jdk8 / 'javac.exe' if (jdk8 / 'javac.exe').is_file() else 'javac'
tools_archive = p / 'downloads/build-tools_r25.0.3-windows.zip'
platform_archive = p / 'downloads/platform-25_r03.zip'
signer_archive = p / 'downloads/build-tools_r37_windows.zip'
build_tools_source = p / 'downloads/extracted-build-tools-25.0.3/android-7.1.1'
platform_source = p / 'downloads/extracted-platform-25/android-7.1.1'
signer_source = p / 'downloads/extracted-build-tools-37/android-37.0/lib/apksigner.jar'
build_tools = p / 'toolchains/android-build-tools-25'
platform = p / 'toolchains/android-platform-25'
signer_dir = p / 'toolchains/android-apksigner-37'
expected_archives = {
    tools_archive: (51337442, '1edcb109ae5133aebfed573cf0bc84e0c353c28d'),
    platform_archive: (85424763, '00c2c5765e8988504be10a1eb66ed71fcdbd7fe8'),
    signer_archive: (61077164, 'e9c97e26b5b5678002e9d3fed632c841ae62d99f'),
}
for archive, (size, sha1) in expected_archives.items():
    if not archive.is_file() or archive.stat().st_size != size or hashlib.sha1(archive.read_bytes()).hexdigest() != sha1:
        raise SystemExit(f'Official Android SDK package integrity check failed: {archive.name}')

build_tools.mkdir(parents=True, exist_ok=True)
platform.mkdir(parents=True, exist_ok=True)
signer_dir.mkdir(parents=True, exist_ok=True)
for name in ('aapt.exe', 'zipalign.exe'):
    shutil.copy2(build_tools_source / name, build_tools / name)
signer = signer_dir / 'apksigner.jar'
shutil.copy2(signer_source, signer)
shutil.copy2(platform_source / 'android.jar', platform / 'android.jar')
aapt = build_tools / 'aapt.exe'
zipalign = build_tools / 'zipalign.exe'
android_jar = platform / 'android.jar'

out = p / 'artifacts/apk-probe'
classes = p / 'build/apk-probe/classes'
dex_dir = p / 'build/apk-probe/dex'
for directory in (out, classes, dex_dir):
    directory.mkdir(parents=True, exist_ok=True)
java_source = p / 'sources/stage4/minimal/ApkProbeActivity.java'
for old_class in (classes/'org/kindlelab/apkprobe').glob('*.class'):
    old_class.unlink()
manifest = p / 'sources/stage4/AndroidManifest.xml'
subprocess.run([str(javac), '-encoding', 'UTF-8', '-Xlint:-options', '-source', '7', '-target', '7', '-bootclasspath', str(android_jar),
                '-d', str(classes), *[str(f) for f in sorted(java_source.parent.glob('*.java'))]], check=True)
subprocess.run(['java', '-cp', str(p / 'toolchains/r8-8.3.37.jar'),
                'com.android.tools.r8.D8', '--min-api', '25', '--no-desugaring',
                '--lib', str(android_jar), '--output', str(dex_dir),
                *[str(class_file) for class_file in sorted(classes.rglob('*.class'))]], check=True)
unsigned = out / 'apk-probe-unsigned.apk'
subprocess.run([str(aapt), 'package', '-f', '-M', str(manifest), '-I', str(android_jar),
                '-S', str(p/'sources/stage4/minimal/res'), '-A', str(p/'sources/stage4/minimal/assets'), '-F', str(unsigned)], check=True)
with zipfile.ZipFile(unsigned, 'a', compression=zipfile.ZIP_DEFLATED) as apk:
    apk.write(dex_dir / 'classes.dex', 'classes.dex')
aligned = out / 'apk-probe-aligned.apk'
subprocess.run([str(zipalign), '-f', '4', str(unsigned), str(aligned)], check=True)

external_key = os.environ.get('KINDLE_ANDROID_APK_KEY')
external_cert = os.environ.get('KINDLE_ANDROID_APK_CERT')
if bool(external_key) != bool(external_cert):
    raise SystemExit('Set both KINDLE_ANDROID_APK_KEY and KINDLE_ANDROID_APK_CERT')
private_key = Path(external_key) if external_key else p / 'toolchains/apk-probe-test-key.pk8'
certificate = Path(external_cert) if external_cert else p / 'toolchains/apk-probe-test-cert.der'
if external_key and (not private_key.is_file() or not certificate.is_file()):
    raise SystemExit('Explicit APK signing key/certificate not found')
openssl = shutil.which('openssl') or str(Path('C:/Program Files/Git/usr/bin/openssl.exe'))
if not Path(openssl).is_file():
    raise SystemExit('OpenSSL is required to create a local-only test signing key')
if not private_key.exists() or not certificate.exists():
    key_pem = p / 'work/apk-probe-test-key.pem'
    cert_pem = p / 'work/apk-probe-test-cert.pem'
    key_pem.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([openssl, 'genpkey', '-quiet', '-algorithm', 'RSA',
                    '-pkeyopt', 'rsa_keygen_bits:2048', '-out', str(key_pem)], check=True)
    subprocess.run([openssl, 'req', '-new', '-x509', '-key', str(key_pem),
                    '-out', str(cert_pem), '-days', '3650',
                    '-subj', '/CN=Kindle Local APK Probe'], check=True)
    subprocess.run([openssl, 'pkcs8', '-topk8', '-nocrypt', '-in', str(key_pem),
                    '-outform', 'DER', '-out', str(private_key)], check=True)
    subprocess.run([openssl, 'x509', '-in', str(cert_pem), '-outform', 'DER',
                    '-out', str(certificate)], check=True)
    key_pem.unlink()
    cert_pem.unlink()
signed = out / 'kindle-apk-probe.apk'
if signed.exists():
    signed.unlink()
subprocess.run(['java', '--add-opens=java.base/java.io=ALL-UNNAMED', '-cp', str(signer),
                'com.android.apksigner.ApkSignerTool', 'sign',
                '--key', str(private_key), '--cert', str(certificate), '--out', str(signed),
                '--min-sdk-version', '25', str(aligned)], check=True)
subprocess.run(['java', '--add-opens=java.base/java.io=ALL-UNNAMED', '-cp', str(signer),
                'com.android.apksigner.ApkSignerTool',
                'verify', '--verbose', str(signed)], check=True)
with (out / 'aapt-badging.txt').open('w', encoding='utf-8') as badging:
    subprocess.run([str(aapt), 'dump', 'badging', str(signed)], check=True, stdout=badging)
report = {
    'status': 'built-and-locally-verified',
    'package': 'org.kindlelab.apkprobe',
    'activity': 'org.kindlelab.apkprobe.ApkProbeActivity',
    'min_sdk': 25,
    'target_sdk': 25,
    'permissions': ['android.permission.READ_EXTERNAL_STORAGE', 'android.permission.WRITE_EXTERNAL_STORAGE'],
    'android_build_tools': '25.0.3 (aapt, zipalign), 37.0.0 (apksigner)',
    'android_platform': '25 / Android 7.1.1',
    'archives': {archive.name: {'size': size, 'sha1': sha1}
                 for archive, (size, sha1) in expected_archives.items()},
    'apk': str(signed),
    'apk_size': signed.stat().st_size,
    'apk_sha256': hashlib.sha256(signed.read_bytes()).hexdigest(),
    'signing_certificate_sha256': hashlib.sha256(certificate.read_bytes()).hexdigest(),
    'private_test_key_stays_local': True,
    'install_or_launch_tested': False,
}
(out / 'build-report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
print(json.dumps(report, indent=2))

