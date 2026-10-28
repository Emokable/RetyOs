"""All input paths are contained in this repository or explicitly installed tools.

The official API25 image supplies the unchanged Android runtime. Every local
native/Java modification is rebuilt; no captured device payload is an input.
"""
from pathlib import Path,PurePosixPath
import hashlib,json,os,shutil,subprocess,sys,tarfile,urllib.request,zipfile,io,gzip,time,stat
from contextlib import contextmanager

ROOT=Path(__file__).resolve().parents[1]
HOST_ARTIFACTS=[('gralloc.hwtcon.so','artifacts/stage5/gralloc.hwtcon.so'),('session-freezer','artifacts/session-freezer'),('session-power-guard','artifacts/session-power-guard'),('session-preflight','artifacts/session-preflight'),('usb-session-guard','artifacts/usb-session-guard'),('native-frame-probe','artifacts/native-frame-probe'),('native-frame-state','artifacts/native-frame-state'),('session-controller','artifacts/session-controller'),('slirp4netns','downloads/slirp4netns-v1.3.4-armv7l')]
def digest(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as f:
        for b in iter(lambda:f.read(4*1024*1024),b''):h.update(b)
    return h.hexdigest()
def load(name):return json.loads((ROOT/name).read_text(encoding='utf-8-sig'))
def record(name,value):
    p=ROOT/name;p.parent.mkdir(parents=True,exist_ok=True)
    p.write_text(json.dumps(value,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
def dirs():
    for n in ['downloads','toolchains','build','artifacts','reports','work','out']:(ROOT/n).mkdir(exist_ok=True)
@contextmanager
def action_lock():
    dirs()
    import msvcrt
    with (ROOT/'reports/build.lock').open('a+b') as f:
        if f.tell()==0:f.write(b'0');f.flush()
        f.seek(0)
        try:msvcrt.locking(f.fileno(),msvcrt.LK_NBLCK,1)
        except OSError:raise RuntimeError('Another preparation/build/package operation is running.')
        try:yield
        finally:f.seek(0);msvcrt.locking(f.fileno(),msvcrt.LK_UNLCK,1)
def safe_path(root,name):
    p=(root/name).resolve();p.relative_to(root.resolve());return p
def unpack_zip(path,dest):
    with zipfile.ZipFile(path) as z:
        for info in z.infolist():
            target=safe_path(dest,info.filename)
            if info.is_dir():target.mkdir(parents=True,exist_ok=True)
            else:
                target.parent.mkdir(parents=True,exist_ok=True)
                with z.open(info) as src,target.open('wb') as out:shutil.copyfileobj(src,out)
def bootstrap(cache,java8_home):
    dirs();lock=load('dependencies.lock.json');cache=Path(cache).resolve() if cache else None
    for item in lock['inputs']:
        target=ROOT/item['path'];target.parent.mkdir(parents=True,exist_ok=True)
        if not target.exists() or digest(target)!=item['sha256']:
            cached=cache/item['path'] if cache else None
            tmp=target.with_name(target.name+'.partial')
            if cached and cached.is_file():shutil.copyfile(cached,tmp)
            else:
                print('DOWNLOAD '+item['url'],flush=True)
                with urllib.request.urlopen(item['url'],timeout=90) as src,tmp.open('wb') as out:shutil.copyfileobj(src,out,4*1024*1024)
            if tmp.stat().st_size!=item['bytes'] or digest(tmp)!=item['sha256']:raise RuntimeError('Input checksum mismatch: '+item['path'])
            tmp.replace(target)
        print('VERIFIED '+item['path'],flush=True)
        kind=item['kind']
        if kind in ('ndk','zig'):unpack_zip(target,ROOT/'toolchains')
        elif kind in ('build25','build37','platform25'):
            dest=ROOT/'downloads'/dict(build25='extracted-build-tools-25.0.3',build37='extracted-build-tools-37',platform25='extracted-platform-25')[kind]
            unpack_zip(target,dest)
        elif kind=='rust':
            dest=ROOT/'toolchains/rust-1.99.1'
            with tarfile.open(target) as t:
                top=t.getmembers()[0].name.split('/')[0]
                components=t.extractfile(top+'/components').read().decode().splitlines()
                for m in t:
                    for c in components:
                        prefix=top+'/'+c+'/'
                        if not m.name.startswith(prefix):continue
                        rel=m.name[len(prefix):]
                        if not rel or rel=='manifest.in':break
                        out=safe_path(dest,rel)
                        if m.isdir():out.mkdir(parents=True,exist_ok=True)
                        elif m.isfile():
                            out.parent.mkdir(parents=True,exist_ok=True)
                            with t.extractfile(m) as src,out.open('wb') as dst:shutil.copyfileobj(src,dst)
                        break
    jdk=Path(java8_home).resolve()
    if not (jdk/'bin/javac.exe').is_file():raise RuntimeError('JDK8 home must contain bin/javac.exe')
    shutil.copytree(jdk,ROOT/'toolchains/temurin8/jdk8u504-b01',dirs_exist_ok=True)
    subprocess.run([sys.executable,'-m','pip','install','--target',str(ROOT/'toolchains/python-libs'),'-r',str(ROOT/'requirements.txt')],check=True)
    doctor()
def doctor():
    if os.name!='nt':raise SystemExit('Current native build recipes require Windows x86_64 and PowerShell 7.')
    checks={}
    for name in ['java','javac','pwsh','cmake','ninja']:
        checks[name]=shutil.which(name)
    checks['openssl']=shutil.which('openssl') or ('C:/Program Files/Git/usr/bin/openssl.exe' if Path('C:/Program Files/Git/usr/bin/openssl.exe').is_file() else None)
    for rel in ['toolchains/android-ndk-r15c/toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe','toolchains/zig-windows-x86_64-0.13.0/zig.exe','toolchains/temurin8/jdk8u504-b01/bin/javac.exe','toolchains/rust-1.99.1/bin/cargo.exe','toolchains/r8-8.3.37.jar','toolchains/apktool-3.0.3.jar']:
        checks[rel]=str(ROOT/rel) if (ROOT/rel).is_file() else None
    record('reports/toolcheck.json',checks)
    missing=[k for k,v in checks.items() if not v]
    if missing:raise SystemExit('Missing build dependencies: '+', '.join(missing))
    print('Build dependencies available.',flush=True)
def prepare():
    with action_lock():_prepare()
def _prepare():
    dirs();doctor()
    for name in ('build-steps.json','build-context.json','build-success.json'):
        (ROOT/'reports'/name).unlink(missing_ok=True)
    api=next(i for i in load('dependencies.lock.json')['inputs'] if i['kind']=='api25')
    archive=ROOT/api['path']
    if not archive.is_file() or digest(archive)!=api['sha256']:raise RuntimeError('Run bootstrap first; API25 archive missing or differs from lock.')
    image=ROOT/'images/api25';image.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(archive) as z:
        for n in ['system.img','ramdisk.img','NOTICE.txt','build.prop','source.properties']:
            with z.open('armeabi-v7a/'+n) as src,(image/n).open('wb') as out:shutil.copyfileobj(src,out,4*1024*1024)
    sys.path.insert(0,str(ROOT/'toolchains/python-libs'))
    from dissect.extfs import ExtFS
    # Fresh link-time runtime libraries; they are not copied from prior builds.
    with (image/'system.img').open('rb') as f:
        fs=ExtFS(f)
        paths={e['source'] for e in load('config/runtime-layout.json')['entries'] if e.get('origin')=='api25'}
        def walk(path):
            node=fs.get(path)
            if node.filetype==stat.S_IFDIR:
                for n in node.listdir():
                    if n not in ('.','..'):yield from walk(path.rstrip('/')+'/'+n)
            else:yield path
        for path in ['/lib','/framework']:
            paths.update(walk(path))
        for path in sorted(paths):
            data=fs.get(path).open().read()
            for tree in ('rootfs-min','rootfs-art','rootfs-zygote'):
                out=ROOT/tree/'system'/path.lstrip('/');out.parent.mkdir(parents=True,exist_ok=True);out.write_bytes(data)
        for tree in ('rootfs-min','rootfs-art','rootfs-zygote'):
            (ROOT/tree/'system/bin').mkdir(parents=True,exist_ok=True)
            for name in ('dev','proc','tests','tmp'):(ROOT/tree/name).mkdir(exist_ok=True)
    shutil.copyfile(ROOT/'config/cacert.pem',ROOT/'downloads/cacert.pem')
    shutil.copytree(ROOT/'third_party/rime-pinyin-simp',ROOT/'downloads/rime-pinyin-simp',dirs_exist_ok=True)
    for src,dest in [('downloads/extracted-platform-25/android-7.1.1','toolchains/android-platform-25'),('downloads/extracted-build-tools-25.0.3/android-7.1.1','toolchains/android-build-tools-25')]:
        (ROOT/dest).mkdir(parents=True,exist_ok=True)
        for n in ('android.jar','aapt.exe','zipalign.exe'):
            if (ROOT/src/n).is_file():shutil.copy2(ROOT/src/n,ROOT/dest/n)
    print('Prepared clean Android base and link libraries.',flush=True)

STEPS=[
'prepare.py','adapt-abi8.py','build-android-native.py','build-bionic-probe.py','build-ashmem.py','build-container-init.py','build-libbinder.py',
'build-container-installd.py','build-headless-battery.py','build-graphics-guard.py',
'build-headless-gles.ps1','build-mock-hwcomposer.ps1','build-headless-surfaceflinger.ps1','build-r12-pixelflinger.py',
'build-null-audio.py','build-swiftshader-arm.ps1','prepare-webview-api25.py','build-apk-probe.py','prepare-minimal-framework-config.py',
'prepare-ca-certificates.py','build-netd-exit-trace.py','build-system-controls.py','build-zygote-namespace-enter.py','build-apk-probe-runner.py',
'build-stage5-hwtcon-gralloc.ps1','build-stage5-exclusive-input.ps1','build-stage5-hwtcon-probe.ps1','build-fuse-mount-broker.py','build-sdcard.py',
'build-getsockname-trace.py','build-userxattr-compat.py','build-session-maintenance.py','build-alarm-fallback-fix.py','build-package-service-dump.py',
'build-session-freezer.py','build-session-power-guard.py','build-session-preflight.py','build-native-frame-probe.py','build-session-controller.py',
'build-isolated-network-tool-shim.py','build-runtime-java.py']
def compile_all(resume=False):
    with action_lock():_compile_all(resume)
def _compile_all(resume=False):
    dirs();doctor();state=ROOT/'reports/build-steps.json'
    (ROOT/'reports/build-success.json').unlink(missing_ok=True)
    history=json.loads(state.read_text()) if resume and state.exists() else []
    context={k:v for k,v in source_hashes().items() if k.startswith(('sources/','scripts/')) or k in ('dependencies.lock.json','requirements.txt')}
    context_path=ROOT/'reports/build-context.json'
    if resume and history and (not context_path.exists() or json.loads(context_path.read_text())!=context):
        print('Build inputs changed; restarting compilation.',flush=True)
        history=[]
    record('reports/build-context.json',context)
    done={x['script'] for x in history if x['returncode']==0}
    for n,name in enumerate(STEPS,1):
        if name in done:continue
        print(f'BUILD {n}/{len(STEPS)} {name}',flush=True)
        script=ROOT/'scripts'/name
        cmd=[shutil.which('pwsh'),'-NoProfile','-ExecutionPolicy','Bypass','-File',str(script)] if script.suffix=='.ps1' else [sys.executable,str(script)]
        log=ROOT/'reports'/f'build-{n:02d}-{name}.log';start=time.monotonic()
        with log.open('wb') as out:r=subprocess.run(cmd,cwd=ROOT,stdout=out,stderr=subprocess.STDOUT)
        history=[x for x in history if x['script']!=name]
        history.append(dict(script=name,returncode=r.returncode,seconds=round(time.monotonic()-start,3),log=log.relative_to(ROOT).as_posix()))
        record('reports/build-steps.json',history)
        if r.returncode:raise SystemExit('Build failed; see '+str(log))
        if name=='build-libbinder.py':shutil.copy2(ROOT/'rootfs-min/system/lib/libbinder.so',ROOT/'rootfs-zygote/system/lib/libbinder.so')
    record('reports/build-success.json',dict(source_sha256=source_hashes(),steps=history))
def source_hashes():
    files=[p for folder in ('sources','scripts','runtime','device','config','tools') for p in sorted((ROOT/folder).rglob('*')) if p.is_file() and '__pycache__' not in p.parts and not p.relative_to(ROOT).as_posix().startswith(('sources/swiftshader-2018/out/','sources/session-controller/target/'))]
    files += [ROOT/n for n in ('build.py','dependencies.lock.json','requirements.txt')]
    return {p.relative_to(ROOT).as_posix():digest(p) for p in files}
def add_tar(t,name,data,mode=0o644,kind='file',link=''):
    i=tarfile.TarInfo(name);i.mode=mode;i.uid=i.gid=i.mtime=0;i.uname=i.gname=''
    if kind=='dir':i.type=tarfile.DIRTYPE
    elif kind=='symlink':i.type=tarfile.SYMTYPE;i.linkname=link
    elif kind=='hardlink':i.type=tarfile.LNKTYPE;i.linkname=link
    else:i.size=len(data)
    t.addfile(i,io.BytesIO(data) if kind=='file' else None)
def render_runner(data,entries):
    text=data.decode('utf-8')
    anchor=' rm -f "$stage"/rootfs/art.marker'
    if text.count(anchor)!=1:raise RuntimeError('Runtime cleanup anchor changed.')
    paths=[e['path'] for e in entries if e['path'].startswith('rootfs/system/') and e['type']!='dir']
    parents=set()
    for name in paths:
        p=PurePosixPath(name).parent
        while str(p)!='rootfs/system':parents.add(str(p));p=p.parent
    lines=[' # GENERATED_SYSTEM_LAYOUT_CLEANUP_BEGIN']
    for name in sorted(paths):
        if any(c in name for c in ('"','$','`','\\','\n')):raise RuntimeError('Unsafe runtime path '+name)
        lines.append(' rm -f "$stage"/'+name)
    for name in sorted(parents,key=lambda p:(p.count('/'),p),reverse=True):
        lines.append(' rmdir "$stage"/'+name+' 2>/dev/null || [ ! -d "$stage"/'+name+' ]')
    lines.append(' # GENERATED_SYSTEM_LAYOUT_CLEANUP_END')
    return text.replace(anchor,'\n'.join(lines)+'\n'+anchor,1).encode('utf-8')
def package():
    with action_lock():_package()
def _package():
    dirs();success=load('reports/build-success.json')
    if success['source_sha256']!=source_hashes():raise RuntimeError('Sources changed since build; rebuild before packaging.')
    # Recreate the generated staging tree so removed integrations cannot leak
    # back into an installer through files left by an earlier package run.
    staging=ROOT/'out'
    if staging.is_symlink() or staging.resolve()!=ROOT.resolve()/'out':
        raise RuntimeError('Unsafe package output directory.')
    shutil.rmtree(staging)
    staging.mkdir()
    out=ROOT/'out/kindle-android';out.mkdir(parents=True,exist_ok=True)
    entries=load('config/runtime-layout.json')['entries'];inventory={}
    def make(path,selected):
        with path.open('wb') as raw,gzip.GzipFile(fileobj=raw,mode='wb',filename='',mtime=0) as gz,tarfile.open(fileobj=gz,mode='w') as t:
            for e in selected:
                if e['type']!='file':add_tar(t,e['path'],b'',e['mode'],e['type'],e['link']);continue
                f=ROOT/e['source'] if e['origin']!='api25' else ROOT/'rootfs-art/system'/e['source'].lstrip('/')
                data=f.read_bytes()
                if e['path']=='run.sh':data=render_runner(data,entries)
                if e['origin']=='api25' and hashlib.sha256(data).hexdigest()!=e['sha256']:raise RuntimeError('Base runtime mismatch '+e['path'])
                inventory[e['path']]=dict(bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),origin=e['origin'],input=e['source'])
                add_tar(t,e['path'],data,e['mode'])
    make(out/'payload.tar.gz',entries)
    make(out/'app-graphics-overlay.tar.gz',[e for e in entries if e.get('origin')=='build' and e['path'].startswith('rootfs/system/')])
    for p in (ROOT/'device/kindle-android').rglob('*'):
        if p.is_file():
            d=out/p.relative_to(ROOT/'device/kindle-android');d.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,d)
    for name,src in HOST_ARTIFACTS:shutil.copyfile(ROOT/src,out/name)
    for name in ('payload.tar.gz','app-graphics-overlay.tar.gz','gralloc.hwtcon.so','session-freezer','session-power-guard','session-preflight','usb-session-guard','native-frame-probe','native-frame-state','session-controller'):
        side='payload.sha256' if name=='payload.tar.gz' else name+'.sha256'
        (out/side).write_bytes((digest(out/name)+'  '+name+'\n').encode('ascii'))
    docs=ROOT/'out/documents';docs.mkdir(exist_ok=True)
    for n in ('start-android.sh','stop-android.sh','install-android.sh'):shutil.copyfile(ROOT/'device'/n,docs/n)
    notices=ROOT/'out/licenses';notices.mkdir(exist_ok=True)
    for name,src in [('PROJECT-MIT.txt','LICENSE'),('THIRD_PARTY_NOTICES.md','THIRD_PARTY_NOTICES.md'),
                     ('Apache-2.0.txt','LICENSES/Apache-2.0.txt'),('GPL-2.0.txt','LICENSES/GPL-2.0.txt'),
                     ('Android-API25-NOTICE.txt','images/api25/NOTICE.txt'),
                     ('SwiftShader-LICENSE.txt','sources/swiftshader-2018/LICENSE.txt'),
                     ('Rime-LICENSE.txt','third_party/rime-pinyin-simp/LICENSE'),
                     ('Rime-AUTHORS.txt','third_party/rime-pinyin-simp/AUTHORS')]:
        shutil.copyfile(ROOT/src,notices/name)
    for tree in ('sources/swiftshader-2018/third_party','toolchains/rust-1.99.1/share/doc'):
        for p in (ROOT/tree).rglob('*'):
            if p.is_file() and p.name.upper() in ('LICENSE','LICENSE.TXT','COPYING','COPYING.TXT','COPYRIGHT','LICENSE-MIT','LICENSE-APACHE'):
                name=p.relative_to(ROOT/tree).as_posix().replace('/','-')
                shutil.copyfile(p,notices/(('SwiftShader-' if tree.startswith('sources') else 'Rust-')+name))
    record('out/runtime-manifest.json',inventory)
    record('out/build-manifest.json',dict(status='built-not-device-tested',androidBase='API25 ARMv7 r18 prebuilt',source_sha256=success['source_sha256'],steps=success['steps'],inputs=load('dependencies.lock.json'),files={p.relative_to(ROOT/'out').as_posix():digest(p) for p in sorted((ROOT/'out').rglob('*')) if p.is_file() and p.name not in ('build-manifest.json','kindle-android-install.tar','verification.json')}))
    with tarfile.open(ROOT/'out/kindle-android-install.tar','w') as t:
        for p in sorted((ROOT/'out').rglob('*')):
            if p.is_file() and p.name not in ('kindle-android-install.tar','verification.json'):add_tar(t,p.relative_to(ROOT/'out').as_posix(),p.read_bytes(),0o755 if p.suffix=='.sh' or not p.suffix else 0o644)
    print('PACKAGE '+str(ROOT/'out/kindle-android-install.tar'),flush=True)
