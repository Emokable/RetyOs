/* SPDX-License-Identifier: Apache-2.0 */
#ifndef KINDLELAB_FUSE_MOUNT_PROTOCOL_H
#define KINDLELAB_FUSE_MOUNT_PROTOCOL_H

#include <stdint.h>

#define KINDLELAB_FUSE_SOCKET "/dev/socket/kindlelab-fuse-mount"
#define KINDLELAB_FUSE_MAGIC 0x4b464d31U
#define KINDLELAB_FUSE_DEST_MAX 128

struct kindlelab_fuse_request {
    uint32_t magic;
    uint32_t uid;
    uint32_t gid;
    char destination[KINDLELAB_FUSE_DEST_MAX];
};

#endif
