"""Build a bounded offline dictionary from pinned Apache-2.0 AOSP/Rime data."""
from pathlib import Path
import hashlib, collections, shutil, urllib.request
p=Path(__file__).resolve().parents[1]
source=p/'downloads/rime-pinyin-simp/pinyin_simp.dict.yaml'
revision='0c6861ef7420ee780270ca6d993d18d4101049d0'
expected={'pinyin_simp.dict.yaml':'e341598343a0f0f2035bb1aafc34a7f3bb7887deeecb3f60796262aaa2983e6b',
          'AUTHORS':'f4cff0fcbca4668ac449c24a53be547e162bc60cce63fdc5d5906801a452edc4',
          'LICENSE':'cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30'}
source.parent.mkdir(parents=True,exist_ok=True)
for name,sha in expected.items():
    file=source.parent/name
    if not file.exists():
        data=urllib.request.urlopen('https://raw.githubusercontent.com/rime/rime-pinyin-simp/'+revision+'/'+name,timeout=30).read()
        assert hashlib.sha256(data).hexdigest()==sha,name
        file.write_bytes(data)
    assert hashlib.sha256(file.read_bytes()).hexdigest()==sha,name
assert hashlib.sha256(source.read_bytes()).hexdigest()=='e341598343a0f0f2035bb1aafc34a7f3bb7887deeecb3f60796262aaa2983e6b'
by_key=collections.defaultdict(list)
for line in source.read_text(encoding='utf-8').splitlines():
    f=line.split('\t')
    if len(f)<3: continue
    try: weight=int(f[2])
    except ValueError: continue
    word,key=f[0],f[1].replace(' ','').replace("'",'')
    if not key.isascii() or not key.isalpha(): continue
    if len(word)>1 and (weight<1000 or len(word)>6): continue
    by_key[key].append((weight,word))
out=p/'sources/stage4/minimal/assets';out.mkdir(exist_ok=True)
lines=[]
for key,rows in sorted(by_key.items()):
    seen=[]
    for _,word in sorted(rows,reverse=True):
        if word not in seen: seen.append(word)
        if len(seen)>=48: break
    lines.append(key+'\t'+' '.join(seen))
(out/'pinyin.tsv').write_text('\n'.join(lines)+'\n',encoding='utf-8')
assert any('你好'==word for _,word in by_key['nihao'])
assert any('中国'==word for _,word in by_key['zhongguo'])
for name in ('LICENSE','AUTHORS'): shutil.copyfile(source.parent/name,out/('PINYIN-'+name+'.txt'))
print('PINYIN_KEYS='+str(len(lines))+' bytes='+str((out/'pinyin.tsv').stat().st_size))
