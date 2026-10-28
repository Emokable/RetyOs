"""Prepare Android 7 system trust anchors from a pinned Mozilla/curl bundle."""
from pathlib import Path
import hashlib,re,subprocess,json,shutil
p=Path(__file__).resolve().parents[1]
source=p/'downloads/cacert.pem'
expected='796f1389bfea13c0ed78f663b765baaff54a508e5483786bd6ece73c5e3e3cbe'
assert hashlib.sha256(source.read_bytes()).hexdigest()==expected,'unexpected CA bundle; review before updating pin'
out=p/'artifacts/ca-certificates/system/etc/security/cacerts';out.mkdir(parents=True,exist_ok=True)
certs=re.findall(rb'-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----',source.read_bytes(),re.S)
assert len(certs)>100
openssl=shutil.which('openssl') or 'C:/Program Files/Git/usr/bin/openssl.exe'
manifest={};seen=set()
for cert in certs:
 pem=cert+b'\n'
 result=subprocess.run([openssl,'x509','-subject_hash_old','-noout'],input=pem,capture_output=True,check=True)
 name=result.stdout.decode().strip();assert re.fullmatch('[0-9a-f]{8}',name)
 index=0
 while name+'.'+str(index) in seen:index+=1
 name+='.'+str(index);seen.add(name);(out/name).write_bytes(pem)
 manifest[name]=hashlib.sha256(pem).hexdigest()
# Only remove previous generated names inside this exact artifact directory.
for previous in out.iterdir():
 if previous.name not in seen and re.fullmatch('[0-9a-f]{8}\.[0-9]+',previous.name):previous.unlink()
(p/'artifacts/ca-certificates/NOTICE.txt').write_text('Mozilla CA certificate store, converted by curl. Source: https://curl.se/docs/caextract.html\nLicense: MPL 2.0 https://mozilla.org/MPL/2.0/\nPinned SHA-256: '+expected+'\n',encoding='utf-8')
(p/'reports/ca-certificates-manifest.json').write_text(json.dumps({'source':'https://curl.se/ca/cacert.pem','bundle_sha256':expected,'certificates':len(certs),'files':manifest},indent=2))
print('ANDROID_SYSTEM_TRUST_ANCHORS='+str(len(certs)))