"""Build every standalone runtime DEX from tracked Java source."""
from pathlib import Path
import subprocess,shutil,sys
ROOT=Path(__file__).resolve().parents[1]
javac=ROOT/'toolchains/temurin8/jdk8u504-b01/bin/javac.exe'
android=ROOT/'toolchains/android-platform-25/android.jar'
d8=ROOT/'toolchains/r8-8.3.37.jar'
for source,out in [('sources/stage3/Hello.java','artifacts/java/classes.dex'),
                   ('sources/stage3/HeadlessSystemServer.java','artifacts/systemserver-bootstrap/classes.dex'),
                   ('sources/stage4/NullTextServices.java','artifacts/textservices-null-stub.dex')]:
    build=ROOT/'build/runtime-java'/Path(source).stem
    classes=build/'classes';dex=build/'dex'
    classes.mkdir(parents=True,exist_ok=True);dex.mkdir(exist_ok=True)
    sources=[str(ROOT/source)]
    if 'NullTextServices' in source:
        stub=build/'android/os/ServiceManager.java';stub.parent.mkdir(parents=True,exist_ok=True)
        stub.write_text('package android.os; public class ServiceManager { public static void addService(String n, IBinder b) {} }\n',encoding='utf-8')
        sources.append(str(stub))
    subprocess.run([str(javac),'-encoding','UTF-8','-source','8','-target','8','-bootclasspath',str(android),'-d',str(classes),*sources],check=True)
    main=Path(source).stem
    files=[str(p) for p in classes.rglob('*.class') if p.name.startswith(main)]
    subprocess.run(['java','-cp',str(d8),'com.android.tools.r8.D8','--min-api','25','--no-desugaring','--lib',str(android),'--output',str(dex),*files],check=True)
    dest=ROOT/out;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(dex/'classes.dex',dest)
