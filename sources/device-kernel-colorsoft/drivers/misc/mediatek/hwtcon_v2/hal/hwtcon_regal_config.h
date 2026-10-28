/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Jiaguang Zhang <jiaguang.zhang@mediatek.com>
 */

#ifndef __HWTCON_REGAL_CONFIG_H__
#define __HWTCON_REGAL_CONFIG_H__

#include <linux/types.h>
#include <linux/mailbox/mtk-cmdq-mailbox-ext.h>
#include <linux/types.h>

enum REGAL_STATUS_ENUM {
	/* task is not regal, read from image buffer */
	REGAL_STATUS_NON_REGAL = 0,
	/* task is regal,
	 * need REGAL IP write to temp image buffer
	 */
	REGAL_STATUS_REGAL = 1,
	/* task is regal,
	 * already go through REGAL IP,
	 * read from temp image buffer
	 */
	REGAL_STATUS_REGAL_HANDLED = 2,
};

enum REGAL_MODE_ENUM {
	REGAL_MODE_REGAL = 0,
	REGAL_MODE_REGAL_D = 1,
	REGAL_MODE_NIGHT = 2,
	REGAL_MODE_DRAK = 3,
	REGAL_MODE_DARK_D = 4,
	REGAL_MODE_DARK_DU = 5,
};

void hwtcon_regal_config_regal_mode(struct cmdq_pkt *pkt,
	enum REGAL_MODE_ENUM regal_mode,
	int img_width, int img_height);


#endif				/* __HWTCON_REGAL_CONFIG_H__ */
