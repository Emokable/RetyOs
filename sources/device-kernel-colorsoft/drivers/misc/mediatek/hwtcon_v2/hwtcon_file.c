// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Jiaguang Zhang <jiaguang.zhang@mediatek.com>
 */

#include <linux/zlib.h>
#include <linux/slab.h>
#include <linux/statfs.h>

#include "hwtcon_file.h"
#include "hwtcon_def.h"
#include "hwtcon_fb.h"
#include "decompress_inflate.h"

/*internal use*/
static int fs_create(struct fs_struct *file, const char *fileName,
		enum FS_OPEN_MODE_ENUM open_mode);
static int fs_read(struct fs_struct *file, char *buffer, u32 size);
static void fs_write(struct fs_struct *file, char *buffer, u32 size);
static void fs_close(struct fs_struct *file);
static int fs_get_size(struct fs_struct *file, const char *fileName);

void init_fs_struct(struct fs_struct *file)
{
	file->fp = NULL;
	file->fs_create = fs_create;
	file->fs_read = fs_read;
	file->fs_write = fs_write;
	file->fs_close = fs_close;
	file->fs_get_size = fs_get_size;
}

static int fs_create(struct fs_struct *file, const char *fileName,
	enum FS_OPEN_MODE_ENUM open_mode)
{
	if (fileName == NULL || *fileName == '\0') {
		TCON_ERR("illegal fileName");
		return HWTCON_STATUS_INVALID_PARAM;
	}
#ifdef CONFIG_SET_FS
	file->fs = get_fs();
	set_fs(KERNEL_DS);
#endif

	if (open_mode == FS_MODE_READ_ONLY)
		file->fp = filp_open(fileName, open_mode, 0444);
	else
		file->fp = filp_open(fileName, open_mode, 0644);

	if (IS_ERR(file->fp)) {
		TCON_ERR("create file[%s] error, fp[%p]", fileName, file->fp);
#ifdef CONFIG_SET_FS
		set_fs(file->fs);
#endif
		return HWTCON_STATUS_OPEN_FILE_FAIL;
	}
	return 0;
}

static void fs_write(struct fs_struct *file, char *buffer, u32 size)
{
#if 0
	vfs_write(file->fp, buffer, size, &file->fp->f_pos);
#else
	kernel_write(file->fp, buffer, size, &file->fp->f_pos);
#endif
}

static int fs_read(struct fs_struct *file, char *buffer, u32 size)
{
#if 0
	return vfs_read(file->fp, buffer, size, &file->fp->f_pos);
#else
	return kernel_read(file->fp, buffer, size, &file->fp->f_pos);
#endif
}

static void fs_close(struct fs_struct *file)
{
	if (file->fp)
		filp_close(file->fp, NULL);

#ifdef CONFIG_SET_FS
	set_fs(file->fs);
#endif
}

static int fs_get_size(struct fs_struct *file, const char *fileName)
{
#if 0
	struct kstat stat = { 0 };

	int status = vfs_stat(fileName, &stat);

	TCON_ERR("fileName:%s status:%d get file size:%lld", fileName, status, stat.size);
	return stat.size;
#else
	struct inode *node = file_inode(file->fp);

	return i_size_read(node);
#endif
}

int hwtcon_file_save_buffer(char *buffer, u32 size, const char *file_name)
{
	struct fs_struct file;
	int status = 0;

	TCON_LOG("dump file:%s with size:%d", file_name, size);
	init_fs_struct(&file);
	status = fs_create(&file, file_name, FS_MODE_RW);
	if (status != 0)
		return status;
	fs_write(&file, buffer, size);
	fs_close(&file);
	return 0;
}

int hwtcon_file_save_buffer_append(char *buffer, u32 size,
		const char *file_name)
{
	struct fs_struct file;
	int status = 0;

	init_fs_struct(&file);
	status = fs_create(&file, file_name, FS_MODE_RW_APPEND);
	if (status != 0)
		return status;
	fs_write(&file, buffer, size);
	fs_close(&file);
	return 0;
}


int hwtcon_file_printf(const char *file_name, char *fmt, ...)
{
	char buffer[512] = { 0 };
	va_list args;
	int ret = 0;

	va_start(args, fmt);
	ret = vsnprintf(buffer, sizeof(buffer), fmt, args);
	if (ret < 0 || ret >= sizeof(buffer)) {
		TCON_ERR("[ERR] write Buffer name wrong\n");
		va_end(args);
		return -1;
	}
	va_end(args);

	return hwtcon_file_save_buffer_append(buffer, strlen(buffer), file_name);
}

int hwtcon_file_read_buffer(char *file_name, char *buffer, int buffer_size)
{
	struct fs_struct file;
	int status = 0;
	int read_size = 0;

	init_fs_struct(&file);
	status = fs_create(&file, file_name, FS_MODE_READ_ONLY);
	if (status != 0)
		return status;
	read_size = fs_read(&file, buffer, buffer_size);
	if (read_size != buffer_size) {
		TCON_ERR("read size:%d not match buffer size:%d",
			read_size,
			buffer_size);
	}
	fs_close(&file);

	return 0;

}
EXPORT_SYMBOL(hwtcon_file_read_buffer);

int hwtcon_file_get_size(char *file_name)
{
	struct fs_struct file;
	int status = 0;
	int file_size = 0;

	init_fs_struct(&file);
	status = fs_create(&file, file_name, FS_MODE_READ_ONLY);
	if (status != 0)
		return 0;

	/* get file size */
	file_size = fs_get_size(&file, file_name);

	fs_close(&file);
	return file_size;
}
EXPORT_SYMBOL(hwtcon_file_get_size);

/* unzip data */
long hwtcon_file_unzip_buffer(char *zip_buffer,
	char *unzip_buffer,
	int zip_length,
	int unzip_max_length)
{
	return hwtcon_gunzip(zip_buffer, zip_length,
			NULL, NULL, unzip_buffer, unzip_max_length, NULL, NULL);
}
EXPORT_SYMBOL(hwtcon_file_unzip_buffer);

MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);

