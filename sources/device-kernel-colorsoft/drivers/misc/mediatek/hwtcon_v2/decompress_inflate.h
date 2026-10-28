/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Jiaguang Zhang <jiaguang.zhang@mediatek.com>
 */

#ifndef __DECOMPRESS_INFLATE_H__
#define __DECOMPRESS_INFLATE_H__
long hwtcon_gunzip(unsigned char *buf, long len,
		long (*fill)(void*, unsigned long),
		       long (*flush)(void*, unsigned long),
		       unsigned char *out_buf,
		       long out_len,
		       long *pos,
		       void (*error)(char *x));
#endif
