/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Jiaguang Zhang <jiaguang.zhang@mediatek.com>
 * Author: Xin Lin <xin.lin@mediatek.com>
 */

#ifndef __HWTCON_DPI_CONFIG_H__
#define __HWTCON_DPI_CONFIG_H__
#include <linux/types.h>
#include <linux/mailbox/mtk-cmdq-mailbox-ext.h>
#include "hwtcon_epd.h"


void wf_lut_config_dpi(struct cmdq_pkt *pkt,
		struct dpi_timing_para *dpi_para, unsigned int width, unsigned int height);

void wf_lut_dpi_enable(struct cmdq_pkt *pkt);
void wf_lut_dpi_disable(struct cmdq_pkt *pkt);
void wf_lut_config_dpi_context(struct cmdq_pkt *pkt, unsigned int width, unsigned int height);
unsigned int wf_lut_dpi_get_checksum(struct cmdq_pkt *pkt);
u32 wf_lut_dpi_get_irq_status(void);
void wf_lut_dpi_clear_irq_status(struct cmdq_pkt *pkt);
void wf_lut_dpi_enable_force_output(struct cmdq_pkt *pkt, bool enable);


#endif				/* __HWTCON_DPI_CONFIG_H__ */
