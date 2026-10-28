"""Build one replacement AlarmHandler class for the no-kernel-alarm fallback.

The API-25 source image contains an odex-only services.jar. Extract its embedded
DEX, disassemble it, replace the broken PendingIntent-only fallback with the
normal delivery routine, and assemble a one-class jar prepended to the system
server classpath. All other services retain their original odex; their embedded
DEX contains quickened instructions and cannot be blindly reassembled.
"""
from pathlib import Path
import hashlib
import struct
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
ODEX = ROOT / 'rootfs-art/system/framework/oat/arm/services.odex'
APKTOOL = ROOT / 'toolchains/apktool-3.0.3.jar'
OUTPUT = ROOT / 'artifacts/alarm-fallback/kindle-alarm-fallback.jar'
CLASS = 'smali/com/android/server/AlarmManagerService$AlarmHandler.smali'
OLD = '''    invoke-static {v0}, Lcom/android/server/AlarmManagerService;->-wrap7(Lcom/android/server/AlarmManagerService;)V
    :try_end_0'''
NEW = '''    invoke-static {v0}, Lcom/android/server/AlarmManagerService;->-wrap7(Lcom/android/server/AlarmManagerService;)V

    # Dispatch both PendingIntent and IAlarmListener alarms while holding mLock.
    const-string v10, "KindleAlarmFix"
    const-string v11, "ALARM_FALLBACK_LISTENER_SAFE=1"
    invoke-static {v10, v11}, Landroid/util/Log;->i(Ljava/lang/String;Ljava/lang/String;)I
    iget-object v0, p0, Lcom/android/server/AlarmManagerService$AlarmHandler;->this$0:Lcom/android/server/AlarmManagerService;
    invoke-virtual {v0, v1, v2, v3}, Lcom/android/server/AlarmManagerService;->deliverAlarmsLocked(Ljava/util/ArrayList;J)V
    :try_end_0'''
AFTER = '''    monitor-exit v9

    .line 2622'''
REPLACEMENT = '''    monitor-exit v9
    goto :goto_0

    .line 2622'''
SCHEDULE = '''
# Alarm fallback supplies elapsedRealtime; Handler queues use uptimeMillis.
.method public sendMessageAtTime(Landroid/os/Message;J)Z
    .locals 4
    iget v0, p1, Landroid/os/Message;->what:I
    const/4 v1, 0x1
    if-ne v0, v1, :schedule
    invoke-static {}, Landroid/os/SystemClock;->elapsedRealtime()J
    move-result-wide v0
    invoke-static {}, Landroid/os/SystemClock;->uptimeMillis()J
    move-result-wide v2
    sub-long/2addr v0, v2
    sub-long/2addr p2, v0
    :schedule
    invoke-super {p0, p1, p2, p3}, Landroid/os/Handler;->sendMessageAtTime(Landroid/os/Message;J)Z
    move-result v0
    return v0
.end method
'''


def main():
    data = ODEX.read_bytes()
    offset = data.find(b'dex\n')
    if offset < 0 or data.find(b'dex\n', offset + 1) >= 0:
        raise SystemExit('Expected exactly one embedded services DEX')
    size = struct.unpack_from('<I', data, offset + 32)[0]
    dex = data[offset:offset + size]
    if len(dex) != size or not dex.startswith(b'dex\n037\0'):
        raise SystemExit('Unexpected services DEX layout')
    with tempfile.TemporaryDirectory(prefix='a7-alarm-', dir=ROOT/'work') as scratch:
        tmp = Path(scratch)
        source = tmp / 'services-original.jar'
        with zipfile.ZipFile(source, 'w', zipfile.ZIP_DEFLATED) as archive:
            archive.writestr('classes.dex', dex)
        tree = tmp / 'decoded'
        subprocess.run(['java', '-jar', str(APKTOOL), 'd', '-f', str(source),
                        '-o', str(tree)], check=True, cwd=ROOT)
        path = tree / CLASS
        text = path.read_text(encoding='utf-8')
        if text.count(OLD) != 1 or text.count(AFTER) != 1:
            raise SystemExit('AlarmHandler changed; refusing blind patch')
        text = text.replace(OLD, NEW).replace(AFTER, REPLACEMENT) + SCHEDULE
        path.write_text(text, encoding='utf-8')
        if 'disallowed odex opcode' in text:
            raise SystemExit('AlarmHandler contains quickened opcodes; refusing invalid DEX')
        # Other services contain quickened instructions. Preserve their original
        # odex; prepend only this unquickened replacement class to the classpath.
        for other in (tree/'smali').rglob('*.smali'):
            if other != path:
                other.unlink()
        OUTPUT.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(['java', '-jar', str(APKTOOL), 'b', str(tree),
                        '-o', str(OUTPUT)], check=True, cwd=ROOT)
    with zipfile.ZipFile(OUTPUT) as archive:
        rebuilt = archive.read('classes.dex')
    if struct.unpack_from('<I', rebuilt, 96)[0] != 1:
        raise SystemExit('Class count changed unexpectedly')
    print('ALARM_FALLBACK_FIXED_JAR=' + str(OUTPUT))
    print('ALARM_FALLBACK_DEX_SHA256=' + hashlib.sha256(rebuilt).hexdigest())


if __name__ == '__main__':
    main()
