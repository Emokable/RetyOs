from pathlib import Path
import subprocess
p=Path(__file__).resolve().parents[1]
b=p/'build/session-maintenance';b.mkdir(parents=True,exist_ok=True)
o=p/'artifacts/session-maintenance';o.mkdir(parents=True,exist_ok=True)
android=p/'toolchains/android-platform-25/android.jar'
subprocess.run([str(p/'toolchains/temurin8/jdk8u504-b01/bin/javac.exe'),'-source','7','-target','7','-bootclasspath',str(android),'-d',str(b),str(p/'sources/stage4/KindleSessionMaintenance.java')],check=True)
subprocess.run(['java','-cp',str(p/'toolchains/r8-8.3.37.jar'),'com.android.tools.r8.D8','--min-api','25','--no-desugaring','--lib',str(android),'--output',str(o),str(b/'KindleSessionMaintenance.class')],check=True)

subprocess.run([str(p/"toolchains/zig-windows-x86_64-0.13.0/zig.exe"),"cc","-target","arm-linux-musleabihf","-static","-Os","-Wall","-Werror",str(p/"sources/stage4/session_run_as.c"),"-o",str(o/"session-run-as")],check=True)

subprocess.run([str(p/"toolchains/zig-windows-x86_64-0.13.0/zig.exe"),"cc","-target","arm-linux-musleabihf","-static","-Os","-Wall","-Wextra","-Werror",str(p/"sources/stage4/usb_session_guard.c"),"-o",str(p/"artifacts/usb-session-guard")],check=True)
