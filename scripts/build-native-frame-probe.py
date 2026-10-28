from pathlib import Path
import hashlib, subprocess
root=Path(__file__).resolve().parents[1]
out=root/'artifacts/native-frame-probe'
subprocess.run([str(root/'toolchains/zig-windows-x86_64-0.13.0/zig.exe'),'cc',
 '-target','arm-linux-musleabihf','-static','-O2','-Wall','-Wextra','-Werror',
 str(root/'sources/stage4/native_frame_probe.c'),'-o',str(out)],check=True)
print('NATIVE_FRAME_PROBE_SHA256='+hashlib.sha256(out.read_bytes()).hexdigest())
