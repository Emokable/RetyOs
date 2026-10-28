/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Jiaguang Zhang <jiaguang.zhang@mediatek.com>
 */

#ifndef __HWTCON_FILE_H__
#define __HWTCON_FILE_H__

#include <linux/fs.h>
#include <linux/file.h>
#include <linux/uaccess.h>
#include <linux/types.h>

enum FS_OPEN_MODE_ENUM {
	FS_MODE_READ_ONLY = O_RDONLY,
	FS_MODE_RW = O_RDWR | O_CREAT | O_TRUNC,
	FS_MODE_RW_APPEND = O_RDWR | O_CREAT | O_APPEND,
};

struct fs_struct {
#ifdef CONFIG_SET_FS
	mm_segment_t fs;
#endif
	struct file *fp;
	int (*fs_create)(struct fs_struct *file, const char *fileName,
		enum FS_OPEN_MODE_ENUM open_mode);
	int (*fs_read)(struct fs_struct *file, char *buffer, u32 size);
	void (*fs_write)(struct fs_struct *file, char *buffer, u32 size);
	int (*fs_get_size)(struct fs_struct *file, const char *fileName);
	void (*fs_close)(struct fs_struct *file);
};

void init_fs_struct(struct fs_struct *file);
int hwtcon_file_save_buffer(char *buffer, u32 size, const char *file_name);
int hwtcon_file_save_buffer_append(char *buffer, u32 size,
		const char *file_name);
int hwtcon_file_printf(const char *file_name, char *fmt, ...);
int hwtcon_file_read_buffer(char *file_name, char *buffer, int buffer_size);
int hwtcon_file_get_size(char *file_name);
/* unzip data */
long hwtcon_file_unzip_buffer(char *zip_buffer,
	char *unzip_buffer,
	int zip_length,
	int unzip_max_length);

#endif
