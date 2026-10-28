"""Validate built archive structure, inventory, architecture and dependencies."""
from pathlib import Path,PurePosixPath
import hashlib,json,sys,tarfile
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from elf_local import inspect

def verify():
    from tools.build_system import action_lock
    with action_lock():return _verify()
def _verify():
    inventory=json.loads((ROOT/'out/runtime-manifest.json').read_text())
    files={};errors=[];elfs={};available=set();runner=''
    with tarfile.open(ROOT/'out/kindle-android/payload.tar.gz') as t:
        seen=set()
        for m in t:
            p=PurePosixPath(m.name)
            if p.is_absolute() or '..' in p.parts:errors.append('unsafe archive path: '+m.name)
            if m.name in seen:errors.append('duplicate archive member: '+m.name)
            seen.add(m.name)
            if '/WeRead/' in m.name or (m.isfile() and m.name.startswith('rootfs/data/')):errors.append('user app/data in package: '+m.name)
            if not m.isfile():continue
            data=t.extractfile(m).read();sha=hashlib.sha256(data).hexdigest()
            if m.name=='run.sh':runner=data.decode('utf-8')
            entry=inventory.get(m.name)
            if not entry or sha!=entry['sha256'] or len(data)!=entry['bytes']:errors.append('runtime manifest mismatch: '+m.name)
            files[m.name]=sha;available.add(p.name)
            if data.startswith(b'\x7fELF'):
                try:elf=inspect(data)
                except Exception as e:errors.append('invalid ELF: '+m.name+' '+str(e));continue
                elfs[m.name]=elf
                if elf['machine']!=40:errors.append('not ARM32: '+m.name)
                if elf['interpreter'] not in (None,'/system/bin/linker'):errors.append('unexpected ELF interpreter: '+m.name)
                if elf.get('soname'):available.add(elf['soname'])
    if set(files)!=set(inventory):errors.append('runtime inventory member set differs')
    if 'GENERATED_SYSTEM_LAYOUT_CLEANUP_BEGIN' not in runner:
        errors.append('runtime cleanup was not generated from the package layout')
    for name in inventory:
        if name.startswith('rootfs/system/') and ('rm -f "$stage"/'+name+'\n') not in runner:
            errors.append('runtime cleanup missing '+name)
    missing={name:sorted(set(v['needed'])-available) for name,v in elfs.items() if set(v['needed'])-available}
    if missing:errors.append('unresolved DT_NEEDED: '+json.dumps(missing))
    build=json.loads((ROOT/'out/build-manifest.json').read_text())
    mode=ROOT/'out/kindle-android/native-framework.mode'
    if not mode.is_file() or mode.read_text().strip()!='preserve':
        errors.append('fresh installation must explicitly preserve native framework processes')
    host_elfs={}
    for f in (ROOT/'out/kindle-android').iterdir():
        if not f.is_file():continue
        with f.open('rb') as stream:magic=stream.read(4)
        if magic!=b'\x7fELF':continue
        elf=inspect(f.read_bytes());host_elfs[f.name]=elf
        if elf['machine']!=40:errors.append('host helper is not ARM32: '+f.name)
    for name,expected in build['files'].items():
        f=ROOT/'out'/name
        if not f.is_file() or hashlib.sha256(f.read_bytes()).hexdigest()!=expected:errors.append('install manifest mismatch: '+name)
    for f in (ROOT/'out/kindle-android').glob('*.sha256'):
        data=f.read_bytes()
        if b'\r' in data or not data.endswith(b'\n'):
            errors.append('checksum sidecar must use Unix LF: '+f.name)
            continue
        for line in data.decode('ascii').splitlines():
            expected,name=line.split('  ',1)
            target=f.parent/name
            if target.parent!=f.parent or not target.is_file() or hashlib.sha256(target.read_bytes()).hexdigest()!=expected:
                errors.append('invalid checksum sidecar: '+f.name)
    with tarfile.open(ROOT/'out/kindle-android-install.tar') as t:
        for m in t:
            if PurePosixPath(m.name).parts[0] not in ('kindle-android','documents','licenses','runtime-manifest.json','build-manifest.json'):
                errors.append('unexpected install entry: '+m.name)
            if not m.isfile():continue
            expected=build['files'].get(m.name)
            if m.name=='build-manifest.json':expected=hashlib.sha256((ROOT/'out/build-manifest.json').read_bytes()).hexdigest()
            if not expected or hashlib.sha256(t.extractfile(m).read()).hexdigest()!=expected:errors.append('install archive mismatch: '+m.name)
    from tools.build_system import STEPS,HOST_ARTIFACTS
    components=json.loads((ROOT/'config/build-components.json').read_text())
    if set(components)!=set(STEPS):errors.append('build component usage does not cover the exact build steps')
    packaged_inputs={v['input'] for v in inventory.values() if v['origin']=='build'}|{source for _,source in HOST_ARTIFACTS}
    for step,component in components.items():
        output=component['output']
        if not (ROOT/output).is_file():errors.append('build component output missing: '+step)
        consumer=component.get('consumedBy')
        if consumer:
            if consumer not in STEPS or Path(output).name not in (ROOT/'scripts'/consumer).read_text(encoding='utf-8-sig'):
                errors.append('build intermediate consumer invalid: '+step)
        elif output not in packaged_inputs:
            errors.append('build output has no installer consumer: '+step)
    steps=build.get('steps',[])
    if len(steps)!=len(STEPS) or {x['script'] for x in steps}!=set(STEPS) or any(x['returncode'] for x in steps):
        errors.append('installer does not record a complete successful build')
    result=dict(status='passed' if not errors else 'failed',buildSteps=len(steps),successfulBuildSteps=sum(x['returncode']==0 for x in steps),
        runtimeFiles=len(files),arm32ElfFiles=len(elfs),hostArm32ElfFiles=len(host_elfs),buildComponentsChecked=len(components),missingDependencies=missing,errors=errors,
        userAppsIncluded=False,userDataIncluded=False,deviceTested=False,
        runtimeBytes=sum(x['bytes'] for x in inventory.values()),
        installerBytes=(ROOT/'out/kindle-android-install.tar').stat().st_size,
        installerSha256=hashlib.sha256((ROOT/'out/kindle-android-install.tar').read_bytes()).hexdigest())
    (ROOT/'out/verification.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result,indent=2))
    if errors:raise SystemExit(1)
    return result

if __name__=='__main__':verify()
