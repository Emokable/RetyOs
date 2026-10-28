/* SPDX-License-Identifier: Apache-2.0
 * Process-local emulation for Android 7 user.* xattrs on the disposable
 * tmpfs mounted as /data. Kindle's 5.15 tmpfs lacks user xattr support.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/xattr.h>
#include <unistd.h>

#define ENTRY_COUNT 512
#define PATH_CAP 512
#define NAME_CAP 64
#define VALUE_CAP 16

struct entry {
    int used;
    char path[PATH_CAP];
    char name[NAME_CAP];
    size_t size;
    unsigned char value[VALUE_CAP];
};

static struct entry entries[ENTRY_COUNT];
static volatile int table_lock;
static int serial_reported;

static void lock_table(void) {
    while (__sync_lock_test_and_set(&table_lock, 1)) { }
}

static void unlock_table(void) {
    __sync_lock_release(&table_lock);
}

static int supported_path(const char *path) {
    return path && (!strcmp(path, "/data") || !strncmp(path, "/data/", 6));
}

static int supported_name(const char *name) {
    return name && (!strcmp(name, "user.serial") ||
                    !strcmp(name, "user.default") ||
                    !strcmp(name, "user.inode_cache") ||
                    !strcmp(name, "user.inode_code_cache"));
}

static struct entry *find_entry(const char *path, const char *name) {
    for (size_t i = 0; i < ENTRY_COUNT; ++i) {
        if (entries[i].used && !strcmp(entries[i].path, path) &&
            !strcmp(entries[i].name, name)) return &entries[i];
    }
    return NULL;
}

ssize_t getxattr(const char *path, const char *name, void *value, size_t size) {
    if (!supported_path(path) || !supported_name(name))
        return syscall(__NR_getxattr, path, name, value, size);
    lock_table();
    struct entry *entry = find_entry(path, name);
    if (!entry) {
        if (!strcmp(name, "user.serial") && !serial_reported) {
            serial_reported = 1;
            fprintf(stderr, "USERXATTR_COMPAT_SERIAL_ACTIVE=1\n");
        }
        unlock_table();
        errno = ENODATA;
        return -1;
    }
    size_t needed = entry->size;
    if (value && size) {
        if (size < needed) {
            unlock_table();
            errno = ERANGE;
            return -1;
        }
        memcpy(value, entry->value, needed);
    }
    unlock_table();
    return (ssize_t)needed;
}

int setxattr(const char *path, const char *name, const void *value, size_t size, int flags) {
    if (!supported_path(path) || !supported_name(name))
        return (int)syscall(__NR_setxattr, path, name, value, size, flags);
    if (size > VALUE_CAP || (size && !value)) {
        errno = E2BIG;
        return -1;
    }
    if (strlen(path) >= PATH_CAP || strlen(name) >= NAME_CAP) {
        errno = ENAMETOOLONG;
        return -1;
    }
    lock_table();
    struct entry *entry = find_entry(path, name);
    if (entry && (flags & XATTR_CREATE)) {
        unlock_table();
        errno = EEXIST;
        return -1;
    }
    if (!entry && (flags & XATTR_REPLACE)) {
        unlock_table();
        errno = ENODATA;
        return -1;
    }
    if (!entry) {
        for (size_t i = 0; i < ENTRY_COUNT; ++i) {
            if (!entries[i].used) { entry = &entries[i]; break; }
        }
        if (!entry) {
            unlock_table();
            errno = ENOSPC;
            return -1;
        }
        entry->used = 1;
        strcpy(entry->path, path);
        strcpy(entry->name, name);
    }
    entry->size = size;
    if (size) memcpy(entry->value, value, size);
    unlock_table();
    return 0;
}
