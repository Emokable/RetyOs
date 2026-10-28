/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Jiaguang Zhang <jiaguang.zhang@mediatek.com>
 * Author: Xin Lin <xin.lin@mediatek.com>
 */

#ifndef __HWTCON_WF_LUT_RDMA_CONFIG_H__
#define __HWTCON_WF_LUT_RDMA_CONFIG_H__
#include <linux/types.h>
#include <linux/mailbox/mtk-cmdq-mailbox-ext.h>
#include "hwtcon_hal.h"

struct rdma_global_config {
	bool ENGINE_EN;		/* bit[0] enable RDMA */
	bool MODE_SEL;		/* bit[1] 0: direct link mode 1: memory mode. */
	/* bit[4] write 1 then 0 to reset, need to poll the reset state. */
	bool SOFT_RESET;
};

void wf_lut_rdma_config_rdma(struct cmdq_pkt *pkt,
	unsigned int width,
	unsigned int height,
	unsigned int inputFormat, unsigned int mode_sel);

void wf_lut_clear_disp_rdma_irq_status(struct cmdq_pkt *pkt);
void wf_lut_rdma_config_smi_setting(struct cmdq_pkt *pkt);

#endif		/* __HWTCON_WF_LUT_RDMA_CONFIG_H__ */
