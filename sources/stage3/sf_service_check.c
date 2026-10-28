// Query SurfaceFlinger's registration through the isolated ABI-8 vndbinder manager.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "binder.h"

int main(int argc, char** argv) {
    const char* name = argc == 2 ? argv[1] : "SurfaceFlinger";
    if (argc > 2 || strlen(name) > 64) return 2;
    alarm(2);
    struct binder_state* bs = binder_open(128 * 1024);
    if (!bs) { puts("SF_LOOKUP_OPEN_FAIL"); return 2; }
    uint32_t buffer[128];
    struct binder_io msg, reply;
    bio_init(&msg, buffer, sizeof(buffer), 4);
    bio_put_uint32(&msg, 0);
    bio_put_string16_x(&msg, SVC_MGR_NAME);
    bio_put_string16_x(&msg, name);
    if (binder_call(bs, &msg, &reply, 0, SVC_MGR_CHECK_SERVICE) != 0) {
        puts("SF_LOOKUP_TRANSACTION_FAIL");
        binder_done(bs, &msg, &reply);
        binder_close(bs);
        return 3;
    }
    uint32_t handle = 0;
    if (reply.data_avail >= sizeof(struct flat_binder_object)) handle = bio_get_ref(&reply);
    else if (reply.data_avail >= sizeof(uint32_t)) (void)bio_get_uint32(&reply);
    binder_done(bs, &msg, &reply);
    binder_close(bs);
    if (argc == 1) printf("SF_LOOKUP handle=%u\n", handle);
    else printf("SERVICE_LOOKUP name=%s handle=%u\n", name, handle);
    return handle ? 0 : 4;
}
