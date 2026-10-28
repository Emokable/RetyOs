from pathlib import Path
import hashlib
import subprocess

p = Path(__file__).resolve().parents[1]
zig = p / 'toolchains/zig-windows-x86_64-0.13.0/zig.exe'
source = p / 'sources/stage3/isolated_network_tool_noop.c'
output = p / 'artifacts/isolated-network-tool-noop'
output.parent.mkdir(parents=True, exist_ok=True)
subprocess.run([
    str(zig), 'cc', '-target', 'arm-linux-musleabi', '-mcpu=cortex_a7',
    '-static', '-nostdlib', '-fno-stack-protector', '-fno-builtin',
    '-Wl,-e,_start', str(source), '-o', str(output),
], check=True)
data = output.read_bytes()
if data[:4] != b'\x7fELF' or data[4] != 1 or data[5] != 1:
    raise SystemExit('expected a 32-bit little-endian ARM ELF')
print(f'isolated-network-tool-noop bytes={len(data)} sha256={hashlib.sha256(data).hexdigest()}')
