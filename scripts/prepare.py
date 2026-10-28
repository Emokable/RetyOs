from pathlib import Path
import difflib,hashlib,json
p=Path(__file__).resolve().parents[1];s=p/'sources';b=p/'build';b.mkdir(exist_ok=True)
(p/'reports/source-sha256.json').write_text(json.dumps({f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in s.iterdir() if f.is_file()},indent=2))
for n in ['binder.c','binder.h','service_manager.c']:
 t=(s/n).read_text()
 if n=='binder.c':t=t.replace('open("/dev/binder",','open("/dev/vndbinder",').replace('#include <cutils/log.h>','#include "lab.h"')
 if n=='binder.h':t=t.replace('#include <linux/binder.h>','#include <linux/android/binder.h>\n_Static_assert(BINDER_CURRENT_PROTOCOL_VERSION==8,"ABI8");\n_Static_assert(sizeof(struct binder_write_read)==48,"bwr");\n_Static_assert(sizeof(struct binder_transaction_data)==64,"tx");\n_Static_assert(sizeof(struct flat_binder_object)==24,"obj");')
 if n=='service_manager.c':
  for h in ['cutils/multiuser.h','private/android_filesystem_config.h','selinux/android.h','selinux/avc.h']:t=t.replace('#include <'+h+'>','')
  t=t.replace('#include <cutils/log.h>','#include "lab.h"')
  a=t.index('static int selinux_enabled;');z=t.index('static int svc_can_register',a)
  t=t[:a]+'static bool check_mac_perms_from_getcon(pid_t p,uid_t u,const char *x){return u==0;}\nstatic bool check_mac_perms_from_lookup(pid_t p,uid_t u,const char *x,const char *n){return u==0;}\n'+t[z:]
  a=t.index('    if (sehandle &&');z=t.index('    switch(txn->code)',a);t=t[:a]+t[z:]
  a=t.index('static int audit_callback');z=t.index('int main()',a);t=t[:a]+t[z:]
  t=t.replace('    bs = binder_open','    alarm(15);\n    if(geteuid()!=0)return 2;\n    fprintf(stderr,"LAB ONLY: ABI8 vndbinder root-only 15s\\n");\n    bs = binder_open')
  a=t.index('    selinux_enabled =');z=t.index('    binder_loop(',a);t=t[:a]+t[z:]
 if n=='binder.c':
  t=t.replace('obj->type','obj->hdr.type')
  t=t.replace('    writebuf.cmd = BC_TRANSACTION;', '    memset(&writebuf,0,sizeof(writebuf));\n    memset(&bwr,0,sizeof(bwr));\n    writebuf.cmd = BC_TRANSACTION;')
 (b/n).write_bytes(t.encode())
 (b/(n+'.patch')).write_bytes(''.join(difflib.unified_diff((s/n).read_text().splitlines(True),t.splitlines(True),fromfile='a/'+n,tofile='b/'+n)).encode())
(b/'lab.h').write_bytes(b'#include <stdbool.h>\n#include <stdint.h>\n#include <unistd.h>\n#ifndef __unused\n#define __unused __attribute__((unused))\n#endif\n#define AID_USER 100000\n#define AID_APP 10000\n#define AID_ISOLATED_START 99000\n#define AID_ISOLATED_END 99999\n#define multiuser_get_app_id(uid) ((uid)%100000)\n#define ALOGE(...) fprintf(stderr,__VA_ARGS__)\n#define ALOGI(...) fprintf(stderr,__VA_ARGS__)\n#define ALOGW(...) fprintf(stderr,__VA_ARGS__)\n')
