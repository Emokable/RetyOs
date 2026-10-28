"""Disable only the absent wallpaper boot gate in the container framework resources.
Repacking drops v2; the untouched manifest and v1 signer are retained. Android 7
system-directory parsing verifies the manifest and extracts the original signer.
This APK is exclusively a read-only container framework resource, not an app update.
"""
from pathlib import Path
import zipfile,struct,subprocess,json,hashlib,copy
p=Path(__file__).resolve().parents[1]
source=p/'artifacts/webview-api25/system/framework/framework-res.apk'
out=p/'artifacts/minimal-framework/framework-res.apk';out.parent.mkdir(parents=True,exist_ok=True)
with zipfile.ZipFile(source) as src:
 data=bytearray(src.read('resources.arsc'));changes=[]
 def u16(i):return struct.unpack_from('<H',data,i)[0]
 def u32(i):return struct.unpack_from('<I',data,i)[0]
 def walk(start,end,package=0):
  pos=start
  while pos<end:
   kind,header,size=struct.unpack_from('<HHI',data,pos)
   assert size>=header>=8 and pos+size<=end
   if kind==0x200:
    walk(pos+header,pos+size,u32(pos+8))
   elif kind==0x201 and package==1 and data[pos+8]==0x12:
    count=u32(pos+12);entries=u32(pos+16);index=0x4e
    if index<count:
     offset=u32(pos+header+index*4)
     if offset!=0xffffffff:
      entry=pos+entries+offset;length=u16(entry);flags=u16(entry+2)
      assert not flags&1
      value=entry+length
      assert data[value+3]==0x12
      before=u32(value+4)
      assert before in (0,0xffffffff)
      struct.pack_into('<I',data,value+4,0);changes.append({'offset':value+4,'before':before,'after':0})
   pos+=size
 walk(u16(2),len(data))
 assert changes,'config_enableWallpaperService resource not found'
 with zipfile.ZipFile(out,'w',compression=zipfile.ZIP_DEFLATED) as dst:
  for entry in src.infolist():dst.writestr(copy.copy(entry),bytes(data) if entry.filename=='resources.arsc' else src.read(entry.filename))
 with zipfile.ZipFile(out) as dst:
  for name in ['AndroidManifest.xml','META-INF/MANIFEST.MF','META-INF/CERT.SF','META-INF/CERT.RSA']:
   assert dst.read(name)==src.read(name),name
result=subprocess.run([str(p/'toolchains/android-build-tools-25/aapt.exe'),'dump','--values','resources',str(out)],capture_output=True,text=True,encoding="utf-8",errors="replace",check=True)
assert 'android:bool/config_enableWallpaperService: t=0x12 d=0x00000000' in result.stdout
(p/'reports/minimal-framework-config.json').write_text(json.dumps({'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'output_sha256':hashlib.sha256(out.read_bytes()).hexdigest(),'resource':'android:bool/config_enableWallpaperService','changes':changes,'scope':'container framework only; native Kindle unchanged'},indent=2))
print('CONTAINER_WALLPAPER_BOOT_GATE_DISABLED=1 entries='+str(len(changes)))