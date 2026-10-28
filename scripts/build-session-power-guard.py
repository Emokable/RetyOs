from pathlib import Path
import subprocess
p=Path(__file__).resolve().parents[1]
subprocess.run([str(p/'toolchains/zig-windows-x86_64-0.13.0/zig.exe'),'cc','-target','arm-linux-musleabihf','-static','-Os','-Wall','-Wextra','-Werror',str(p/'sources/stage4/session_power_guard.c'),'-o',str(p/'artifacts/session-power-guard')],check=True)
