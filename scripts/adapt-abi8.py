# ABI8 wire fields must not use ARM32 native pointer/size widths.
from pathlib import Path
import difflib
p=Path(__file__).resolve().parents[1];f=p/'build/binder.c';s=f.read_text()
s=s.replace('sizeof(size_t)','sizeof(binder_size_t)').replace('        uintptr_t buffer;','        binder_uintptr_t buffer;')
s=s.replace('    obj->handle = handle;','    obj->binder = 0;\n    obj->handle = handle;')
# Acknowledge reference and death notifications using the kernel wire widths.
a=s.index('        case BR_INCREFS:');z=s.index('        case BR_TRANSACTION: {',a)
s=s[:a]+'''        case BR_INCREFS:
        case BR_ACQUIRE:
        case BR_RELEASE:
        case BR_DECREFS: {
            if(end-ptr < sizeof(struct binder_ptr_cookie))return -1;
            if(cmd==BR_INCREFS || cmd==BR_ACQUIRE) {
                struct { uint32_t cmd; struct binder_ptr_cookie payload; } __attribute__((packed)) ack;
                ack.cmd=cmd==BR_INCREFS?BC_INCREFS_DONE:BC_ACQUIRE_DONE;
                memcpy(&ack.payload,(void*)ptr,sizeof(ack.payload));
                binder_write(bs,&ack,sizeof(ack));
            }
            ptr+=sizeof(struct binder_ptr_cookie);
            break;
        }
'''+s[z:]
s=s.replace('            death->func(bs, death->ptr);','''            struct { uint32_t cmd; binder_uintptr_t cookie; } __attribute__((packed)) ack;
            ack.cmd=BC_DEAD_BINDER_DONE;ack.cookie=(uintptr_t)death;
            death->func(bs, death->ptr);
            binder_write(bs,&ack,sizeof(ack));''')
f.write_bytes(s.encode())
for n in ['binder.c','binder.h','service_manager.c']:
 original=(p/'sources'/n).read_text();updated=(p/'build'/n).read_text()
 (p/'build'/(n+'.patch')).write_bytes(''.join(difflib.unified_diff(original.splitlines(True),updated.splitlines(True),fromfile='a/'+n,tofile='b/'+n)).encode())
