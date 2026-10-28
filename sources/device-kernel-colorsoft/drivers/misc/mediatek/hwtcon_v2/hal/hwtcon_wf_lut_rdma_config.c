// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Jiaguang Zhang <jiaguang.zhang@mediatek.com>
 * Author: Xin Lin <xin.lin@mediatek.com>
 */

#include "hwtcon_wf_lut_rdma_config.h"
#include "hwtcon_hal.h"
#include "hwtcon_epd.h"


void wf_lut_rdma_config_smi_setting(struct cmdq_pkt *pkt)
{
#ifndef HWTCON_M4U_SUPPORT
	u32 i = 0;

	/* smi common */
	pp_write(pkt, 0x140021a0, 0x1);
	pp_write(pkt, 0x140021c0, 0x0);
	pp_write(pkt, 0x140021a0, 0x0);
	pp_write(pkt, 0x140021a4, 0x1);
	pp_write(pkt, 0x14002220, 0x4444);

	/* SMI LARB0 */
	pp_write(pkt, 0x14003400, 0x1);
	pp_write(pkt, 0x14003400, 0x0);
	pp_write(pkt, 0x14003404, 0x1);

#if 0
	/* memset 0x14003380 ~ 0x140033e0 to 0 */
	for (i = 0x14003380; i < 0x140033e0; i = i + 4)
		pp_write(i, 0x0);

	/* memset 0x14003f80 ~ 0x14003fe0 to 0 */
	for (i = 0x14003f80; i < 0x14003fe0; i = i + 4)
		pp_write(i, 0x0);
#else
	for (i = 0; i < 10; i++) {
		pp_write(pkt, 0x14003380 + i * 4, 0);
		pp_write(pkt, 0x14003f80 + i * 4, 0);
	}
#endif

	/* SMI LARB1 */
	pp_write(pkt, 0x15002400, 0x1);
	pp_write(pkt, 0x15002400, 0x0);
	pp_write(pkt, 0x15002404, 0x1);

	/* memset 0x15002f80 ~ 0x15002fe0 to 0 */
	for (i = 0; i < 18; i++) {
		pp_write(pkt, 0x15002380 + i * 4, 0);
		pp_write(pkt, 0x15002f80 + i * 4, 0);
	}
#endif
}


void wf_lut_rdma_config_rdma(struct cmdq_pkt *pkt, unsigned int width,
		unsigned int height, unsigned int inputFormat, unsigned int mode_sel)
{
	struct rdma_global_config global_config = { 0 };

	global_config.ENGINE_EN = true;
	global_config.MODE_SEL = mode_sel;
	global_config.SOFT_RESET = false;
	pp_write(pkt, WF_LUT_RDMA_GLOBAL_CON,
		global_config.SOFT_RESET << 4 |
		global_config.MODE_SEL << 1 | global_config.ENGINE_EN << 0);
	//pp_write(PP_IMG_RDMA_GLOBAL_CON, 0x0103);      why need to set bit7

	/* OUTPUT_FRAME_WIDTH */
	pp_write_mask(pkt, WF_LUT_RDMA_SIZE_CON_0, width, GENMASK(12, 0));
	/* OUTPUT_FRAME_HEIGHT */
	pp_write_mask(pkt, WF_LUT_RDMA_SIZE_CON_1, height, GENMASK(19, 0));
	/* INPUT FORMAT */
	pp_write_mask(pkt, WF_LUT_RDMA_MEM_CON,
		      inputFormat << 4, GENMASK(7, 4));

	if (mode_sel)
		pp_write(pkt, WF_LUT_RDMA_MEM_START_ADDR,
			 hwtcon_fb_info()->wb_buffer_pa);

	/* MEM_MODE_SRC_PITCH */
	pp_write_mask(pkt, WF_LUT_RDMA_MEM_SRC_PITCH,
		      4 * width, GENMASK(15, 0));

	if (hw_tcon_get_edp_out_8bit())
		pp_write(pkt, WF_LUT_RDMA_BG_CON_0,
			((hw_tcon_get_wb_buffer_start_x() >> 2) << 16) |
			(hw_tcon_get_wb_buffer_start_x() >> 2));
	else
		pp_write(pkt, WF_LUT_RDMA_BG_CON_0,
			((hw_tcon_get_wb_buffer_start_x() >> 3) << 16) |
			(hw_tcon_get_wb_buffer_start_x() >> 3));

	pp_write(pkt, WF_LUT_RDMA_BG_CON_1,
		(hw_tcon_get_wb_buffer_start_y() << 16) | hw_tcon_get_wb_buffer_start_y());

	pp_write(pkt, WF_LUT_RDMA_MEM_GMC_SETTING_0, 0x20202040);
	pp_write(pkt, WF_LUT_RDMA_MEM_GMC_SETTING_1, 0x00000020);
	pp_write(pkt, WF_LUT_RDMA_FIFO_CON, 0x01000010);
	pp_write(pkt, WF_LUT_RDMA_INT_ENABLE, 0x00000004);
}

void wf_lut_clear_disp_rdma_irq_status(struct cmdq_pkt *pkt)
{
	pp_write_mask(pkt, WF_LUT_RDMA_INT_STATUS, 0x00 << 2, BIT_MASK(2));
}
