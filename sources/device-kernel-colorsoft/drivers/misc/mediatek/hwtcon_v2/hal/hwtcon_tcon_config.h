/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Jiaguang Zhang <jiaguang.zhang@mediatek.com>
 * Author: Xin Lin <xin.lin@mediatek.com>
 */

#ifndef __HWTCON_TCON_CONFIG_H__
#define __HWTCON_TCON_CONFIG_H__
#include <linux/types.h>
#include "hwtcon_epd.h"
#include <linux/mailbox/mtk-cmdq-mailbox-ext.h>
#include "hwtcon_hal.h"
#define HWTCON_TCON_PINMUX_REGISTER_BIT_NUM		5
#define HWTCON_TCON_PINMUX_ONE_BIT_OFFSET		6

struct tcon_pin_swap_para {
	unsigned int sdec;
	unsigned int sdoe;
	unsigned int gdclk;
	unsigned int sdle;
	unsigned int gdoe;
};

void tcon_config_global_register(struct cmdq_pkt *pkt);
void tcon_config_pin(struct cmdq_pkt *pkt);
void tcon_setting(struct cmdq_pkt *pkt);
void tcon_config_swtcon_pin(struct cmdq_pkt *pkt);
void tcon_disable(struct cmdq_pkt *pkt);

#endif				/* __HWTCON_TCON_CONFIG_H__ */
