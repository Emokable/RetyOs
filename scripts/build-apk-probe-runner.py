"""Build the bounded in-namespace APK install/launch/uninstall coordinator."""
from pathlib import Path
import hashlib
import subprocess

p = Path(__file__).resolve().parents[1]
zig = p / 'toolchains/zig-windows-x86_64-0.13.0/zig.exe'
source = p / 'sources/stage4/apk_probe_runner.c'
out = p / 'artifacts/apk-probe-runner'
out.parent.mkdir(parents=True, exist_ok=True)
subprocess.run([str(zig), 'cc', '-target', 'arm-linux-musleabihf', '-static', '-Os',
                '-Wall', '-Wextra', '-Werror', str(source), '-o', str(out)], check=True)
print('artifact=' + str(out))
print('sha256=' + hashlib.sha256(out.read_bytes()).hexdigest())
