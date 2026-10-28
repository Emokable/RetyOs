from pathlib import Path
import subprocess, hashlib
p=Path(__file__).resolve().parents[1]
out=p/'artifacts/session-freezer'
subprocess.run([str(p/'toolchains/zig-windows-x86_64-0.13.0/zig.exe'),'cc','-target','arm-linux-musleabihf','-static','-O2','-Wall','-Wextra','-Werror',str(p/'sources/stage4/session_freezer.c'),'-o',str(out)],check=True)
print('SESSION_FREEZER_SHA256='+hashlib.sha256(out.read_bytes()).hexdigest())
