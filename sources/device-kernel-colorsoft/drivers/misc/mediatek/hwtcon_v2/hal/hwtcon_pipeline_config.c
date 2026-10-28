// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Jiaguang Zhang <jiaguang.zhang@mediatek.com>
 */

#include <linux/dma-direction.h>
#include <linux/dma-mapping.h>
#include <dt-bindings/gce/mt8512-gce.h>
#include <string.h>

#include "hwtcon_pipeline_config.h"
#include "hwtcon_reg.h"
#include "hwtcon_hal.h"
#include "hwtcon_def.h"
#include "hwtcon_driver.h"
#include "hwtcon_regal_config.h"
#include "hwtcon_reg.h"
#include "hwtcon_epd.h"
#include "hwtcon_core.h"
#include "hwtcon_wf_lut_config.h"
#include "hwtcon_cmdq.h"
#include "hwtcon_mdp.h"
#include <linux/mailbox/mtk-cmdq-mailbox-ext.h>
#include <linux/videodev2.h>
#include "hwtcon_debug.h"

/* change this var to switch gce / cpu config pipeline */
static bool pipeline_use_gce = true;
static bool pipeline_reserved = true;

/*init static value to make it be in line with coding style*/
void pipeline_config_init_reserve(void)
{
	pipeline_reserved = false;
}

void pipeline_config_enable_sw_wb_wdma(struct cmdq_pkt *pkt, bool enable)
{
	/* sw config working buffer wdma hardwware reg(size/addr/pitch),
	 * not pipeline hardware auto config.
	 */
	pp_write_mask(pkt, PAPER_TCTOP_BUF_CFG0, enable << 2, BIT_MASK(2));
}

void pipeline_config_enable_sw_wb_rdma(struct cmdq_pkt *pkt, bool enable)
{
	/* sw config working buffer rdma hardwware reg(size/addr/pitch),
	 * not pipeline hardware auto config.
	 */
	pp_write_mask(pkt, PAPER_TCTOP_BUF_CFG0, enable << 1, BIT_MASK(1));
}

void pipeline_config_enable_sw_img_rdma(struct cmdq_pkt *pkt, bool enable)
{
	/* sw config image buffer rdma hardwware reg(size/addr/pitch),
	 * not pipeline hardware auto config.
	 */
	pp_write_mask(pkt, PAPER_TCTOP_BUF_CFG0, enable << 0, BIT_MASK(0));
}

void pipeline_config_img_buffer_pitch(struct cmdq_pkt *pkt,
		enum PITCH_SELECT pitch_select_type, u32 pitch)
{
	if (pitch_select_type == PITCH_SELECT_HW_AUTO) {
		pp_write_mask(pkt, PAPER_TCTOP_BUF_CFG0, 0 << 6, BIT_MASK(6));
	} else {
		pp_write_mask(pkt, PAPER_TCTOP_BUF_CFG0, 1 << 6, BIT_MASK(6));
		pp_write_mask(pkt, PAPER_TCTOP_BUF_CFG1, pitch, GENMASK(15, 0));
	}
}

void pipeline_config_wb_buffer_pitch(struct cmdq_pkt *pkt,
		enum PITCH_SELECT pitch_select_type, u32 pitch)
{
	/* This is working buffer RDMA pitch
	 * Hardware will auto calculate working buffer WDMA pitch.
	 * for Non regal case: WB WDMA pitch = WB RDMA pitch
	 * for regal case: WB WDMA pitch = WB RDMA pitch / 2
	 */
	if (pitch_select_type == PITCH_SELECT_HW_AUTO) {
		pp_write_mask(pkt, PAPER_TCTOP_BUF_CFG0, 0 << 3, BIT_MASK(3));
	} else {
		pp_write_mask(pkt, PAPER_TCTOP_BUF_CFG0, 1 << 3, BIT_MASK(3));
		pp_write_mask(pkt, PAPER_TCTOP_BUF_CFG1, pitch << 16, GENMASK(31, 16));
	}
}

void pipeline_config_enable_irq(struct cmdq_pkt *pkt, enum HWTCON_IRQ_TYPE irq)
{
	switch (irq) {
	case IRQ_PIPELINE_COLLISION:
		/* collision irq must enable. if disable,
		 * lut_col event will not come.
		 * if we clear collsion irq in irq handler,
		 * the COLLISION_LUT status will reset to 0.
		 * Normal driver flow, we need to enable collision irq,
		 * but don't register this irq to kernel.
		 */
		pp_write_mask(pkt, PAPER_TCTOP_IRQ_CTL, 1 << 0, BIT_MASK(0));
		break;
	case IRQ_PIPELINE_LUT_ASSIGN_DONE:
		pp_write_mask(pkt, PAPER_TCTOP_IRQ_CTL, 1 << 1, BIT_MASK(1));
		break;
	case IRQ_PIPELINE_DPI_UPDATE_DONE:
		pp_write_mask(pkt, PAPER_TCTOP_IRQ_CTL, 1 << 2, BIT_MASK(2));
		break;
	case IRQ_PIPELINE_LUT_ILLEGAL:
		pp_write_mask(pkt, PAPER_TCTOP_IRQ_CTL, 1 << 3, BIT_MASK(3));
		break;
	case IRQ_PIPELINE_REGION_ILLEGAL:
		pp_write_mask(pkt, PAPER_TCTOP_IRQ_CTL, 1 << 4, BIT_MASK(4));
		break;
	case IRQ_WF_LUT_TCON_END:
		pp_write_mask(pkt, PAPER_TCTOP_IRQ_CTL, 1 << 5, BIT_MASK(5));
		break;
	case IRQ_PIPELINE_PIXEL_LUT_COLLISION:
		pp_write_mask(pkt, PAPER_TCTOP_IRQ_CTL, 1 << 6, BIT_MASK(6));
		break;
	case IRQ_PIPELINE_WB_FRAME_DONE:
		pp_write(pkt, WB_WDMA_INTEN, 3);
		break;
	case IRQ_DPI_SOF:
		pp_write_mask(pkt, WF_LUT_DPI_INTEN, 1 << 0, BIT_MASK(0));
		break;
	default:
		TCON_ERR("invalid irq type:%d", irq);
	}

}

void pipeline_config_disable_irq(struct cmdq_pkt *pkt, enum HWTCON_IRQ_TYPE irq)
{
	switch (irq) {
	case IRQ_PIPELINE_COLLISION:
		pp_write_mask(pkt, PAPER_TCTOP_IRQ_CTL, 0 << 0, BIT_MASK(0));
		break;
	case IRQ_PIPELINE_LUT_ASSIGN_DONE:
		pp_write_mask(pkt, PAPER_TCTOP_IRQ_CTL, 0 << 1, BIT_MASK(1));
		break;
	case IRQ_PIPELINE_DPI_UPDATE_DONE:
		pp_write_mask(pkt, PAPER_TCTOP_IRQ_CTL, 0 << 2, BIT_MASK(2));
		break;
	case IRQ_PIPELINE_LUT_ILLEGAL:
		pp_write_mask(pkt, PAPER_TCTOP_IRQ_CTL, 0 << 3, BIT_MASK(3));
		break;
	case IRQ_PIPELINE_REGION_ILLEGAL:
		pp_write_mask(pkt, PAPER_TCTOP_IRQ_CTL, 0 << 4, BIT_MASK(4));
		break;
	case IRQ_WF_LUT_TCON_END:
		pp_write_mask(pkt, PAPER_TCTOP_IRQ_CTL, 0 << 5, BIT_MASK(5));
		break;
	case IRQ_PIPELINE_PIXEL_LUT_COLLISION:
		pp_write_mask(pkt, PAPER_TCTOP_IRQ_CTL, 0 << 6, BIT_MASK(6));
		break;
	case IRQ_PIPELINE_WB_FRAME_DONE:
		pp_write(pkt, WB_WDMA_INTEN, 0);
		break;
	case IRQ_DPI_SOF:
		pp_write_mask(pkt, WF_LUT_DPI_INTEN, 0 << 0, BIT_MASK(0));
		break;
	default:
		TCON_ERR("invalid irq type:%d", irq);
	}

}


void pipeline_config_clear_irq(struct cmdq_pkt *pkt, enum HWTCON_IRQ_TYPE irq)
{
	switch (irq) {
	case IRQ_PIPELINE_COLLISION:
		pp_write_mask(pkt, PAPER_TCTOP_IRQ_CTL, 1 << 16, BIT_MASK(16));
		pp_write_mask(pkt, PAPER_TCTOP_IRQ_CTL, 0 << 16, BIT_MASK(16));
		break;
	case IRQ_PIPELINE_LUT_ASSIGN_DONE:
		pp_write_mask(pkt, PAPER_TCTOP_IRQ_CTL, 1 << 17, BIT_MASK(17));
		pp_write_mask(pkt, PAPER_TCTOP_IRQ_CTL, 0 << 17, BIT_MASK(17));
		break;
	case IRQ_PIPELINE_DPI_UPDATE_DONE:
		pp_write_mask(pkt, PAPER_TCTOP_IRQ_CTL, 1 << 18, BIT_MASK(18));
		pp_write_mask(pkt, PAPER_TCTOP_IRQ_CTL, 0 << 18, BIT_MASK(18));
		break;
	case IRQ_PIPELINE_LUT_ILLEGAL:
		pp_write_mask(pkt, PAPER_TCTOP_IRQ_CTL, 1 << 19, BIT_MASK(19));
		pp_write_mask(pkt, PAPER_TCTOP_IRQ_CTL, 0 << 19, BIT_MASK(19));
		break;
	case IRQ_PIPELINE_REGION_ILLEGAL:
		pp_write_mask(pkt, PAPER_TCTOP_IRQ_CTL, 1 << 20, BIT_MASK(20));
		pp_write_mask(pkt, PAPER_TCTOP_IRQ_CTL, 0 << 20, BIT_MASK(20));
		break;
	case IRQ_WF_LUT_TCON_END:
		pp_write_mask(pkt, PAPER_TCTOP_IRQ_CTL, 1 << 21, BIT_MASK(21));
		pp_write_mask(pkt, PAPER_TCTOP_IRQ_CTL, 0 << 21, BIT_MASK(21));
		break;
	case IRQ_PIPELINE_PIXEL_LUT_COLLISION:
		pp_write_mask(pkt, PAPER_TCTOP_IRQ_CTL, 1 << 22, BIT_MASK(22));
		pp_write_mask(pkt, PAPER_TCTOP_IRQ_CTL, 0 << 22, BIT_MASK(22));
		break;
	case IRQ_PIPELINE_WB_FRAME_DONE:
		pp_write(pkt, WB_WDMA_INTSTA, 0);
		break;
	case IRQ_DPI_SOF:
		pp_write_mask(pkt, WF_LUT_DPI_INTSTA, 0 << 0, BIT_MASK(0));
		break;
	default:
		TCON_ERR("invalid irq type:%d", irq);
	}
}

void pipeline_config_reset_wb_rdma(struct cmdq_pkt *pkt)
{
	pp_write_mask(pkt, WB_RDMA_GLOBAL_CON, 1 << 4, BIT_MASK(4));
	pp_write_mask(pkt, WB_RDMA_GLOBAL_CON, 0 << 4, BIT_MASK(4));
}

void pipeline_config_enable_wb_rdma(struct cmdq_pkt *pkt, bool enable)
{
	u32 ultra_threshold_low = 0;
	u32 ultra_threshold_high = 0;
	u32 pre_ultra_threshold_low = 0x80;
	u32 pre_ultra_threshold_high = 0x80;

	if (enable)
		pp_write(pkt, WB_RDMA_GLOBAL_CON, 0x103);
	else
		pp_write(pkt, WB_RDMA_GLOBAL_CON, 0x0);

	/* config wb rdma fifo */
	pp_write_mask(pkt, WB_RDMA_FIFO_CON, 1, GENMASK(9, 0));

	/* config rdma ultra & pre-ultra */
	pp_write_mask(pkt, WB_RDMA_GMC_SETTING_0,
		ultra_threshold_low << 0 |
		ultra_threshold_high << 16 |
		pre_ultra_threshold_low << 8 |
		pre_ultra_threshold_high << 24,
		GENMASK(7, 0) |
		GENMASK(23, 16) |
		GENMASK(15, 8) |
		GENMASK(31, 24));
}

void pipeline_config_reset_img_rdma(struct cmdq_pkt *pkt)
{
	pp_write_mask(pkt, IMG_RDMA_GLOBAL_CON, 1 << 4, BIT_MASK(4));
	pp_write_mask(pkt, IMG_RDMA_GLOBAL_CON, 0 << 4, BIT_MASK(4));
}

void pipeline_config_enable_img_rdma(struct cmdq_pkt *pkt, bool enable)
{
	u32 ultra_threshold_low = 0;
	u32 ultra_threshold_high = 0;
	u32 pre_ultra_threshold_low = 0x80;
	u32 pre_ultra_threshold_high = 0x80;

	if (enable)
		pp_write(pkt, IMG_RDMA_GLOBAL_CON, 0x103);
	else
		pp_write(pkt, IMG_RDMA_GLOBAL_CON, 0x0);
	/* config img rdma fifo */
	pp_write_mask(pkt, IMG_RDMA_FIFO_CON, 1, GENMASK(9, 0));

	/* config rdma ultra & pre-ultra */
	pp_write_mask(pkt, IMG_RDMA_GMC_SETTING_0,
		ultra_threshold_low << 0 |
		ultra_threshold_high << 16 |
		pre_ultra_threshold_low << 8 |
		pre_ultra_threshold_high << 24,
		GENMASK(7, 0) |
		GENMASK(23, 16) |
		GENMASK(15, 8) |
		GENMASK(31, 24));
}

void pipeline_config_reset_wb_wdma(struct cmdq_pkt *pkt)
{
	pp_write(pkt, WB_WDMA_RST, 1);
	pp_write(pkt, WB_WDMA_RST, 0);
}

void pipeline_config_enable_wb_wdma(struct cmdq_pkt *pkt, bool enable)
{
	int ultra_en = 0;
	int pre_ultra_en = 1;
	int frame_end_ultra_en = 0;
	int issue_req_threshold = 1;

	if (enable) {
		/* reset WDMA */
		pp_write(pkt, WB_WDMA_RST, 1);
		/* set output format */
		pp_write(pkt, WB_WDMA_CFG, 0x50);
		/* enable WB WDMA */
		pp_write(pkt, WB_WDMA_EN, 1);
	} else {
		pp_write(pkt, WB_WDMA_EN, 0);
	}

	/* config wb wdma fifo */
	pp_write_mask(pkt, WB_WDMA_BUF_CON1,
		ultra_en << 31 | pre_ultra_en << 30 | frame_end_ultra_en << 28 |
		issue_req_threshold << 16,
		BIT_MASK(31) | BIT_MASK(30) | BIT_MASK(28) | GENMASK(25, 16));
}

void pipeline_config_enable_illegal_setting_buffer_write(struct cmdq_pkt *pkt, bool enable)
{		/* 0x1400D004[29]: 1: disable working buffer write when illegal setting.
		 * 0x1400D004[29]: 0: enable working buffer write when illegal setting.
		 */
	pp_write_mask(pkt, PAPER_TCTOP_MAIN_CTL, (!enable) << 29, BIT_MASK(29));
}

void paper_config_reset_dpi_on_frame_done(struct cmdq_pkt *pkt, bool enable)
{
	/* 0x1400D004[1]: 1: enable reset dpi on every DPI frame done.
	 * reset when WF_LUT hang to recover.
	 * 0x1400D004[0]: 0: disable this feature.
	 * when WF_LUT hang, the WF_LUT will not recover.
	 */
	pp_write_mask(pkt, PAPER_TCTOP_MAIN_CTL, enable << 1, BIT_MASK(1));
}

void pipeline_config_fifo(struct cmdq_pkt *pkt, bool enable, u32 fifo_size, u32 read_threshold)
{
	pp_write_mask(pkt, PAPET_TCTOP_FIFO_CFG,
		enable << 16 | fifo_size << 8 | read_threshold << 0,
		BIT_MASK(16) | GENMASK(15, 8) | GENMASK(7, 0));
}

void pipeline_config_enable_histogram(struct cmdq_pkt *pkt, bool enable)
{
	if (enable)
		pp_write_mask(pkt, PAPER_TCTOP_HIST_CFG0, 1 << 0, BIT_MASK(0));
	else
		pp_write_mask(pkt, PAPER_TCTOP_HIST_CFG0, 0 << 0, BIT_MASK(0));
}

void pipeline_config_gray_value(struct cmdq_pkt *pkt,
	u32 y2_gray_val, u32 y4_gray_val,
	u32 y8_gray_val, u32 y16_gray_val)
{
	pp_write(pkt, PAPER_TCTOP_HIST_CFG1, y2_gray_val);
	pp_write(pkt, PAPER_TCTOP_HIST_CFG2, y4_gray_val);
	pp_write(pkt, PAPER_TCTOP_HIST_CFG3, y8_gray_val);
	pp_write(pkt, PAPER_TCTOP_HIST_CFG4, y16_gray_val);
}

void pipeline_config_wb_merge_region(struct cmdq_pkt *pkt, bool enable, struct rect *merge_region)
{
	pp_write_mask(pkt, PAPER_TCTOP_WB_BUF_XY_INFO, enable << 31, BIT_MASK(31));
	if (enable && merge_region) {
		pp_write_mask(pkt, PAPER_TCTOP_WB_BUF_XY_INFO,
			merge_region->x << 0 | merge_region->y << 16,
			GENMASK(13, 0) | GENMASK(29, 16));
		pp_write_mask(pkt, PAPER_TCTOP_WB_BUF_WH_INFO,
			merge_region->width << 0 | merge_region->height << 16,
			GENMASK(13, 0) | GENMASK(29, 16));
	}
}

void pipeline_config_img_rdma_addr(struct cmdq_pkt *pkt, u32 img_rdma_pa)
{
	pp_write(pkt, PAPER_TCTOP_IMG_ST_ADDR, img_rdma_pa);
}

void pipeline_config_wb_rdma_addr(struct cmdq_pkt *pkt, u32 wb_rdma_pa)
{
	pp_write(pkt, PAPER_TCTOP_WB_ST_ADDR1, wb_rdma_pa);
}

void pipeline_config_wb_wdma_addr(struct cmdq_pkt *pkt, u32 wb_wdma_pa)
{
	pp_write(pkt, PAPER_TCTOP_WB_ST_ADDR0, wb_wdma_pa);
}

void pipeline_config_panel_resolution(struct cmdq_pkt *pkt, int panel_width, int panel_height)
{
	panel_width &= 0x3FFF;
	panel_height &= 0x3FFF;
	pp_write(pkt, PAPER_TCTOP_PANEL_SIZE, panel_height << 16 | panel_width);
}

/* enum PIPELINE_FLAG_ENUM */
void pipeline_config_update_flag(struct cmdq_pkt *pkt, u32 flag)
{
	pp_write(pkt, PIPELINE_FLAG, flag);
}

void pipeline_config_update_lut(struct cmdq_pkt *pkt, int lut_id)
{
	lut_id &= GENMASK(5, 0);
	pp_write_mask(pkt, PAPER_TCTOP_UPD_CFG2, lut_id << 4, GENMASK(9, 4));
}

void pipeline_config_trigger_hw(struct cmdq_pkt *pkt)
{
	/* trigger pipeline start to work */
	pp_write_mask(pkt, PAPER_TCTOP_UPD_CFG2, 1 << 0, BIT_MASK(0));
	pp_write_mask(pkt, PAPER_TCTOP_UPD_CFG2, 0 << 0, BIT_MASK(0));
}

void pipeline_config_update_region(struct cmdq_pkt *pkt, struct rect region)
{
	pp_write_mask(pkt, PAPER_TCTOP_UPD_CFG0,
		(region.x & 0x3FFF) << 0 |
		(region.y & 0x3FFF) << 16,
		GENMASK(13, 0) | GENMASK(29, 16));
	pp_write_mask(pkt, PAPER_TCTOP_UPD_CFG1,
		(region.width & 0x3FFF) << 0 |
		(region.height & 0x3FFF) << 16,
		GENMASK(13, 0) | GENMASK(29, 16));
}

void pipeline_config_active_lut(struct cmdq_pkt *pkt, u32 active_lut1, u32 active_lut0)
{
	pp_write(pkt, PAPER_TCTOP_LUT_ACTIVE0, active_lut0);
	pp_write(pkt, PAPER_TCTOP_LUT_ACTIVE1, active_lut1);
}

void pipeline_print_irq_status(void)
{
	u32 irq_status = pp_read(PAPER_TCTOP_UPD_CFG3_VA);
	int i = 0;
	char *irq_name[] = {
		"lut_illegal",	/* bit 0: lut id >= 63 */
		"region_illegal",	/* bit 1: region > resolution */
		"lut_assign_done",	/* bit 2: wb wdma frame done */
		/* bit 3: request lut_id has
		 * collision with active lut id
		 */
		"lut_collision",
		"reserved",	/* bit 4 */
		/* bit 5: the update region has the same
		 * LUT ID number(no matter the LUT ID is
		 * active or not) with current update LUT ID
		 */
		"pixel_lut_id_collision",
		"wb_wdma_done",	/* bit 6: wb wdma frame done */
		"image_rdma_done",	/* bit 7: image rdma frame done */
		"wb_rdma_done",	/* bit 8: wb rdma frame done */
		"dpi_frame_done",	/* bit 9: dpi frame done */
		"tcon_end",	/* bit 10: tcon end */
	};

	for (i = 0; i < ARRAY_SIZE(irq_name); i++) {
		if (irq_status & BIT_MASK(i))
			TCON_ERR("irq status:%d -> %s", i, irq_name[i]);
	}
}

void pipeline_get_collision_region(struct rect *region)
{
	u32 val = 0;

	val = pp_read(PAPER_TCTOP_PXL_COL_RGN0_VA);
	region->x = val & GENMASK(13, 0);
	region->y = (val & GENMASK(29, 16)) >> 16;

	val = pp_read(PAPER_TCTOP_PXL_COL_RGN1_VA);
	region->width = val & GENMASK(13, 0);
	region->height = (val & GENMASK(29, 16)) >> 16;
}

void pipeline_get_collision_lut_mask(u32 *col_lut1, u32 *col_lut0)
{
	*col_lut1 = pp_read(PAPER_TCTOP_COL_STATUS1_VA);
	*col_lut0 = pp_read(PAPER_TCTOP_COL_STATUS0_VA);
}

void pipeline_get_pixel_update_region(struct rect *region)
{
	u32 val = 0;

	val = pp_read(PAPER_TCTOP_PXL_UPD_RGN0_VA);
	region->x = val & GENMASK(13, 0);
	region->y = (val & GENMASK(29, 16)) >> 16;

	val = pp_read(PAPER_TCTOP_PXL_UPD_RGN1_VA);
	region->width = val & GENMASK(13, 0);
	region->height = (val & GENMASK(29, 16)) >> 16;
}

bool pipeline_get_update_void_status(void)
{
	u32 val = 0;

	val = pp_read(PAPER_TCTOP_VOID_LUT_VA);
	return val & BIT_MASK(0);
}

bool pipeline_get_do_clear_status(void)
{
	u32 val = 0;

	val = pp_read(PAPER_TCTOP_VOID_LUT_VA);
	return (val & BIT_MASK(1)) >> 1;
}

void pipeline_get_histogram(u32 *next_histogram, u32 *current_histogram)
{
	if (next_histogram)
		*next_histogram = pp_read(PAPER_TCTOP_HIST_STA1_VA);
	if (current_histogram)
		*current_histogram = pp_read(PAPER_TCTOP_HIST_STA0_VA);
}

void pipeline_get_gray_value(u32 *next_gray_value, u32 *current_gray_value)
{
	u32 gray_value = pp_read(PAPER_TCTOP_HIST_STA2_VA);

	if (next_gray_value)
		*next_gray_value = (gray_value & GENMASK(6, 4)) >> 4;
	if (current_gray_value)
		*current_gray_value = gray_value & GENMASK(2, 0);
}

int pipeline_poll_task_done(struct cmdq_pkt *pkt)
{
	if (pkt) {
		cmdq_pkt_wfe(pkt, CMDQ_EVENT_WB_WDMA_DONE);
		return cmdq_pkt_flush(pkt);
	}
	if (wait_for_completion_timeout(&hwtcon_fb_info()->wb_frame_done_completion,
					msecs_to_jiffies(1000)) == 0) {
		TCON_ERR("wait pipeline wb frame done timeout");
		return -1;
	}

	return 0;
}

/* make sure pipeline is triggered with sequence */
static DEFINE_MUTEX(pipline_mutex);

int pipeline_config_trigger(u32 img_addr_pa,
		u32 input_wb_addr_pa,
		u32 output_wb_addr_pa,
	struct pipeline_config_info config,
	struct pipeline_info *info)
{
	int status = 0;
	struct cmdq_pkt *pkt = NULL;
	unsigned long flags = 0;
	u64 active_lut = 0;

	if (pipeline_use_gce) {
		pkt = cmdq_pkt_create(hwtcon_cmdq_get_pipeline_clt());
		cmdq_mbox_enable(hwtcon_cmdq_get_pipeline_clt()->chan);
	}

	TCON_LOG(
		"trigger pipeline lut:%d regoin[%d %d %d %d], flag:0x%08x active lut[0x%08x 0x%08x] regal_mode:%d",
		config.update_lut,
		config.update_region.x,
		config.update_region.y,
		config.update_region.width,
		config.update_region.height,
		config.pipeline_ctl_flag,
		config.active_lut1,
		config.active_lut0,
		config.regal_mode);

	mutex_lock(&pipline_mutex);

	pipeline_config_enable_img_rdma(pkt, true);
	pipeline_config_enable_wb_rdma(pkt, true);
	pipeline_config_enable_wb_wdma(pkt, true);
	pipeline_config_enable_histogram(pkt, true);
	if (hwtcon_fb_info()->stylus_enabled)
		pipeline_config_enable_illegal_setting_buffer_write(pkt, true);
	else
		pipeline_config_enable_illegal_setting_buffer_write(pkt, false);
	paper_config_reset_dpi_on_frame_done(pkt, true);

	/* enable irq */
	pipeline_config_enable_irq(pkt, IRQ_PIPELINE_WB_FRAME_DONE);
	pipeline_config_enable_irq(pkt, IRQ_PIPELINE_COLLISION);
	pipeline_config_enable_irq(pkt, IRQ_PIPELINE_LUT_ASSIGN_DONE);
	pipeline_config_enable_irq(pkt, IRQ_PIPELINE_LUT_ILLEGAL);
	pipeline_config_enable_irq(pkt, IRQ_PIPELINE_REGION_ILLEGAL);

	if (config.pipeline_ctl_flag & PIPELINE_FLAG_REGAL)
		pipeline_config_disable_irq(pkt, IRQ_PIPELINE_PIXEL_LUT_COLLISION);
	else
		pipeline_config_enable_irq(pkt, IRQ_PIPELINE_PIXEL_LUT_COLLISION);
	pipeline_config_fifo(pkt, true, 0xFF, 0x10);

	pipeline_config_img_rdma_addr(pkt, img_addr_pa);
	pipeline_config_wb_rdma_addr(pkt, input_wb_addr_pa);
	pipeline_config_wb_wdma_addr(pkt, output_wb_addr_pa);

	pipeline_config_panel_resolution(pkt,
		hw_tcon_get_display_width(),
		hw_tcon_get_display_height());

	pipeline_config_update_flag(pkt, config.pipeline_ctl_flag);
	if (config.pipeline_ctl_flag & PIPELINE_FLAG_REGAL)
		hwtcon_regal_config_regal_mode(pkt,
			config.regal_mode,
			hw_tcon_get_display_width(),
			hw_tcon_get_display_height());

	if (config.use_wb_merge_region) {
		/* sw config working merge region */
		pipeline_config_wb_merge_region(pkt, true, &config.wb_merge_region);
	} else
		pipeline_config_wb_merge_region(pkt, false, NULL);

	if (config.use_sw_config_img_pitch)
		pipeline_config_img_buffer_pitch(pkt, PITCH_SELECT_SW_CONFIG, config.img_pitch);
	else
		pipeline_config_img_buffer_pitch(pkt, PITCH_SELECT_HW_AUTO, 0);

	if (config.use_sw_config_wb_pitch)
		pipeline_config_wb_buffer_pitch(pkt, PITCH_SELECT_SW_CONFIG, config.wb_pitch);
	else
		pipeline_config_wb_buffer_pitch(pkt, PITCH_SELECT_HW_AUTO, 0);

	pipeline_config_update_lut(pkt, config.update_lut);
	pipeline_config_update_region(pkt, config.update_region);

	/* if both config.active_lut0 and config.active_lut1 are 0xFFFFFFFF, they
	 * need to be assigned the actual active lut here to avoid potential race
	 * condition as new luts might have been activated and assigned to pixels
	 * before pipeline_mutex is locked
	 */
	if (config.active_lut0 == 0xFFFFFFFF && config.active_lut1 == 0xFFFFFFFF) {
		/* if stylus_enabled is not true,
		 * the pipeline is handling updates with non-DU
		 *waveform, it might clear the WB during update.
		 *Get out to avoid race contidition
		 */
		if (!hwtcon_fb_info()->stylus_enabled) {
			if (pipeline_use_gce) {
				cmdq_mbox_disable(hwtcon_cmdq_get_pipeline_clt()->chan);
				cmdq_pkt_destroy(pkt);
			}
			mutex_unlock(&pipline_mutex);
			return -1;
		}
		spin_lock_irqsave(&hwtcon_fb_info()->lut_active_lock, flags);
		active_lut = hwtcon_fb_info()->lut_active;
		spin_unlock_irqrestore(&hwtcon_fb_info()->lut_active_lock, flags);
		config.active_lut0 = (u32)(active_lut & 0xFFFFFFFF);
		config.active_lut1 = (u32)(active_lut >> 32);
	}
	pipeline_config_active_lut(pkt, config.active_lut1, config.active_lut0);

	if (pkt)
		cmdq_clear_event(hwtcon_cmdq_get_pipeline_clt()->chan, CMDQ_EVENT_WB_WDMA_DONE);
	else
		reinit_completion(&hwtcon_fb_info()->wb_frame_done_completion);
	pipeline_config_trigger_hw(pkt);

	status = pipeline_poll_task_done(pkt);

	pipeline_get_collision_lut_mask(&info->collision_lut_1, &info->collision_lut_0);
	pipeline_get_collision_region(&info->collision_region);
	pipeline_get_histogram(&info->next_histogram, &info->current_histogram);
	info->do_clear = pipeline_get_do_clear_status();
	info->update_void = pipeline_get_update_void_status();
	info->panel_width = pp_read(PAPER_TCTOP_PANEL_SIZE_VA) & GENMASK(12, 0);
	info->panel_height = (pp_read(PAPER_TCTOP_PANEL_SIZE_VA) & GENMASK(28, 16)) >> 16;

	mutex_unlock(&pipline_mutex);

	TCON_LOG(
		"pipeline info void:%d clear:%d histo[0x%08x 0x%08x] col_lut[0x%08x 0x%08x] col_region[%d %d %d %d]",
		info->update_void,
		info->do_clear,
		info->next_histogram,
		info->current_histogram,
		info->collision_lut_1,
		info->collision_lut_0,
		info->collision_region.x,
		info->collision_region.y,
		info->collision_region.width,
		info->collision_region.height);

	if (pipeline_use_gce) {
		cmdq_mbox_disable(hwtcon_cmdq_get_pipeline_clt()->chan);
		cmdq_pkt_destroy(pkt);
	}
	return status;
}


/* For 4 Bit panel display feature,
 * we need to trigger pipeline twice to write 2 LUT ID to working buffer.
 * 1st trigger:
 * image_addr: image_addr_pa
 * wb_addr: input_wb_addr_pa
 * update_lut: config.update_lut
 * active_lut0: config.active_lut0
 * active_lut1: config.active_lut1
 *
 * 2nd trigger:
 * image_addr: image_addr_pa
 * wb_addr: input_wb_addr_pa + wb_buffer_size / 2
 * update_lut: config.update_lut + 1
 * active_lut0: config.active_lut0
 * active_lut1: config.active_lut1
 *
 * compare 2 pipeline info
 * 1st trigger collision_lut_0 = 2nd trigger collision_lut_1;
 * 1st trigger collision_lut_1 == 2nd trigger collision_lut_0 == 0;
 *
 * the following info should same.
 * struct rect collision_region;
 * bool update_void;
 * bool do_clear;
 * u32 next_histogram;
 * u32 current_histogram;
 * u32 panel_width;
 * u32 panel_height;
 */
int pipeline_config_trigger_twice(u32 img_addr_pa,
	u32 input_wb_addr_pa,
	u32 output_wb_addr_pa,
	struct pipeline_config_info config,
	struct pipeline_info *info)
{
	int i = 0;
	int status = 0;
	u32 img_addr_pa_list[2] = {img_addr_pa, img_addr_pa};
	u32 input_wb_addr_pa_list[2] = {input_wb_addr_pa,
		input_wb_addr_pa + (hwtcon_fb_info()->wb_buffer_size >> 1)};
	u32 output_wb_addr_pa_list[2] = {output_wb_addr_pa,
		output_wb_addr_pa + (hwtcon_fb_info()->wb_buffer_size >> 1)};
	struct pipeline_config_info config_list[2] = {config, config};
	struct pipeline_info info_list[2] = {{0}};

	/* when PIPELINE_FLAG_CLEAR is set, and the config.update_lut = 63,
	 * we don't need to add update_lut = 64 in this case.
	 */
	if (!((config.pipeline_ctl_flag & PIPELINE_FLAG_CLEAR) &&
		(config.update_lut == MAX_LUT_REGION_COUNT)))
		config_list[1].update_lut += 1;

	/* trigger pipeline twice */
	for (i = 0; i < 2; i++) {
		status = pipeline_config_trigger(img_addr_pa_list[i],
			input_wb_addr_pa_list[i],
			output_wb_addr_pa_list[i],
			config_list[i], &info_list[i]);
		if (status != 0) {
			TCON_ERR("trigger pipeline twice[%d] fail:%d", i, status);
			TCON_ERR(
				"trigger pipeline addr[0x%08x 0x%08x 0x%08x] lut:%d regoin[%d %d %d %d], flag:0x%08x active lut[0x%08x 0x%08x] regal_mode:%d",
				img_addr_pa_list[i],
				input_wb_addr_pa_list[i],
				output_wb_addr_pa_list[i],
				config_list[i].update_lut,
				config_list[i].update_region.x,
				config_list[i].update_region.y,
				config_list[i].update_region.width,
				config_list[i].update_region.height,
				config_list[i].pipeline_ctl_flag,
				config_list[i].active_lut1,
				config_list[i].active_lut0,
				config_list[i].regal_mode);
			dump_stack();
			return status;
		}
	}

	/* check pipeline return info */
	if ((memcmp(&info_list[0].collision_region,
			&info_list[1].collision_region,
			sizeof(info_list[0].collision_region)) != 0) ||
		(info_list[0].update_void != info_list[1].update_void) ||
		(info_list[0].do_clear != info_list[1].do_clear) ||
		(info_list[0].next_histogram != info_list[1].next_histogram) ||
		(info_list[0].current_histogram != info_list[1].current_histogram) ||
		(info_list[0].panel_width != info_list[1].panel_width) ||
		(info_list[0].panel_height != info_list[1].panel_height)) {
		TCON_ERR("pipeline info not sync\n");
		for (i = 0; i < 2; i++) {
			TCON_ERR(
				"info[%d] collision_lut_0:0x%08x collision_lut_1:0x%08x\n",
				i,
				info_list[i].collision_lut_0,
				info_list[i].collision_lut_1);
			TCON_ERR(
				"info[%d] collision_region[%d %d %d %d]\n",
				i,
				info_list[i].collision_region.x,
				info_list[i].collision_region.y,
				info_list[i].collision_region.width,
				info_list[i].collision_region.height);
			TCON_ERR(
				"info[%d] void:%d clear:%d histogram[0x%08x 0x%08x] panel[%d %d]\n",
				i,
				info_list[i].update_void,
				info_list[i].do_clear,
				info_list[i].current_histogram,
				info_list[i].next_histogram,
				info_list[i].panel_width,
				info_list[i].panel_height);
		}
		return HWTCON_STATUS_PIPELINE_INFO_NOT_SYNC;
	}

	memcpy(info, &info_list[0], sizeof(*info));
	return 0;
}

int pipeline_init_working_buffer(void)
{
	struct pipeline_config_info config;
	struct pipeline_info info;
	u64 active_lut = 0LL;
	unsigned long flags;
	int status = 0;

	memset(&config, 0, sizeof(config));
	memset(&info, 0, sizeof(info));

	spin_lock_irqsave(&hwtcon_fb_info()->lut_active_lock, flags);
	active_lut = hwtcon_fb_info()->lut_active;
	spin_unlock_irqrestore(&hwtcon_fb_info()->lut_active_lock, flags);

	config.active_lut0 = (u32) (active_lut & 0xFFFFFFFF);
	config.active_lut1 = (u32) (active_lut >> 32);
	config.update_lut = 0x3f;
	config.update_region.x = 0;
	config.update_region.y = 0;
	config.update_region.width = 0;
	config.update_region.height = 0;

	config.pipeline_ctl_flag = PIPELINE_FLAG_FULL_UPDATE |
		PIPELINE_FLAG_Y5_INPUT |
		PIPELINE_FLAG_CLEAR;

	if (hw_tcon_get_dual_mode()) {
		status = pipeline_config_trigger_twice(hwtcon_fb_info()->img_buffer_pa,
			hwtcon_fb_info()->wb_buffer_pa,
			hwtcon_fb_info()->wb_buffer_pa,
			config, &info);
		TCON_LOG("trigger pipeline twice");
	} else {
		status = pipeline_config_trigger(hwtcon_fb_info()->img_buffer_pa,
			hwtcon_fb_info()->wb_buffer_pa,
			hwtcon_fb_info()->wb_buffer_pa,
			config, &info);
	}
	if (status != 0)
		return status;

	/* update released lut to free lut */
	spin_lock_irqsave(&hwtcon_fb_info()->lut_free_lock, flags);
	hwtcon_fb_info()->lut_free = ~active_lut & hw_tcon_get_all_lut();
	spin_unlock_irqrestore(&hwtcon_fb_info()->lut_free_lock, flags);

	return 0;
}

/* clear working buffer with low latency */
int pipeline_init_working_buffer_ll(void)
{
	struct pipeline_config_info config;
	struct pipeline_info info;
	u64 active_lut = 0LL;
	u32 y_pos = 0;
	u32 stride, remain, ix;
	unsigned long flags;
	int status = 0;

	remain = hw_tcon_get_display_height();
	stride = hw_tcon_get_display_height() / STEPS_WB_INIT + 1;

	memset(&config, 0, sizeof(config));
	memset(&info, 0, sizeof(info));
	config.update_lut = 0x3f;
	config.update_region.x = 0;
	config.update_region.width = hw_tcon_get_display_width();

	for (ix = 0; ix < STEPS_WB_INIT; ix++) {
		spin_lock_irqsave(&hwtcon_fb_info()->lut_active_lock, flags);
		active_lut |= hwtcon_fb_info()->lut_active;
		spin_unlock_irqrestore(&hwtcon_fb_info()->lut_active_lock, flags);

		/* set both active_lut0 and active_lut1 to 0xFFFFFFFF to indicate
		 * the actual active luts need to be checked inside function
		 * pipeline_config_trigger after pipeline_mutex is acquired
		 */
		config.active_lut0 = config.active_lut1 = 0xFFFFFFFF;
		config.update_region.y = y_pos;
		config.update_region.height = min(stride, remain);
		config.pipeline_ctl_flag = PIPELINE_FLAG_LUT_OVERRIDE;

		if (hw_tcon_get_dual_mode()) {
			status = pipeline_config_trigger_twice(hwtcon_fb_info()->img_buffer_pa,
				hwtcon_fb_info()->wb_buffer_pa,
				hwtcon_fb_info()->wb_buffer_pa,
				config, &info);
			TCON_LOG("trigger pipeline twice");
		} else {
			status = pipeline_config_trigger(hwtcon_fb_info()->img_buffer_pa,
				hwtcon_fb_info()->wb_buffer_pa,
				hwtcon_fb_info()->wb_buffer_pa,
				config, &info);
		}

		if (status != 0)
			return status;

		if (remain <= stride)
			break;

		remain -= stride;
		y_pos += stride;

		/* if the pipeline has been reserved by an incoming update, get out of the way */
		while (pipeline_reserved)
			yield();
	}

	/* update released lut to free lut. The active_lut needs to be checked again
	 * in case new luts are activated after the last clean-up session. Also lut_active_lock
	 * needs to be acquired before converting the released luts to free luts to prevent other
	 * functions or ISRs to change active_lut right before the conversion
	 */

	spin_lock_irqsave(&hwtcon_fb_info()->lut_active_lock, flags);
	active_lut |= hwtcon_fb_info()->lut_active;
	spin_lock_irqsave(&hwtcon_fb_info()->lut_free_lock, flags);
	hwtcon_fb_info()->lut_free = ~active_lut & hw_tcon_get_all_lut();
	spin_unlock_irqrestore(&hwtcon_fb_info()->lut_free_lock, flags);
	spin_unlock_irqrestore(&hwtcon_fb_info()->lut_active_lock, flags);

	return 0;
}

int pipeline_trigger_regal(struct hwtcon_task *task)
{
	struct pipeline_config_info config;
	struct pipeline_info info;
	int status = 0;

	memset(&config, 0, sizeof(config));
	memset(&info, 0, sizeof(info));

	if (task->regal_status != REGAL_STATUS_REGAL) {
		TCON_ERR("invalid regal status:%d",
			task->regal_status);
		return 0;
	}

	config.regal_mode = task->regal_mode;

	/* update region: panel resolution */
	config.update_region.x = 0;
	config.update_region.y = 0;
	config.update_region.width = hw_tcon_get_display_width();
	config.update_region.height = hw_tcon_get_display_height();

	config.active_lut0 = 0;
	config.active_lut1 = 0;
	config.update_lut = 0;
	config.pipeline_ctl_flag = PIPELINE_FLAG_FULL_UPDATE |
		PIPELINE_FLAG_REGAL;

	status = pipeline_config_trigger(hwtcon_fb_info()->img_buffer_pa,
		hwtcon_fb_info()->wb_buffer_pa,
		hwtcon_fb_info()->temp_img_buffer_pa,
		config, &info);
	if (status != 0)
		return status;

	task->regal_status = REGAL_STATUS_REGAL_HANDLED;

	return 0;
}

static int pipeline_copy_previous_buffers(struct hwtcon_task *task) {
	if (hw_tcon_get_epd_type()) {
		struct rect region = hwtcon_core_get_task_region(task);
		int width = hw_tcon_get_display_width();
		int off = width * region.y + region.x;

		task->time_prebuffer_start = timeofday_ms();
		while (region.height--) {
			memcpy(hwtcon_fb_info()->pre_img_buffer_va + off,
					hwtcon_fb_info()->img_buffer_va + off,
					region.width);
			off += width;
		}
		task->time_prebuffer_end = timeofday_ms();
	}
	return 0;
}

int pipeline_handle_normal_update(struct hwtcon_task *task,
	struct pipeline_info *info)
{
	int status = 0;
	int acquired_id = 0;
	bool need_do_clear = false;
	u32 pipeline_flag = 0;
	struct pipeline_config_info config;
	u64 active_lut = 0LL;
	unsigned long flags;
	int ret = 0;

	if (hwtcon_core_use_cfa_color_and_regal(task)) {
		TCON_LOG("cfa color regal starts");
		task->time_csr_start = timeofday_ms();
		ret = hwtcon_mdp_regal_handle(task);
		if (ret)
			TCON_ERR("trigger cfa color regal fail");
		else
			TCON_LOG("cfa color regal done successfully");
		/* clean CFA regal output buffer after CFA regal work done */
		hwtcon_fb_clean_cache(hwtcon_fb_info()->temp_img_cache_buf);
		hwtcon_fb_clean_cache(hwtcon_fb_info()->partial_regal_cache_buf2);
		task->time_csr_end = timeofday_ms();
		task->regal_status = REGAL_STATUS_REGAL_HANDLED;
		pipeline_flag |= PIPELINE_FLAG_FULL_UPDATE;
		pipeline_flag |= PIPELINE_FLAG_Y5_INPUT;
	} else if (task->regal_status == REGAL_STATUS_REGAL) {

		status = pipeline_trigger_regal(task);
		pipeline_flag |= PIPELINE_FLAG_Y5_INPUT;
		if (status != 0)
			return status;
	}

	/* full / partial update */
	if (task->update_data.update_mode == UPDATE_MODE_FULL &&
		!((task->update_data.waveform_mode == WAVEFORM_MODE_DU ||
		task->update_data.waveform_mode == WAVEFORM_MODE_DUNM) &&
		hwtcon_fb_info()->stylus_mode))
		pipeline_flag |= PIPELINE_FLAG_FULL_UPDATE;

	/* allocate a free lut */
	status = hwtcon_core_get_free_lut(&need_do_clear, &acquired_id);
	if (status == GET_LUT_ERR || status == GET_LUT_TIMEOUT)
		return status;
	if (status == GET_LUT_BUSY) {
		status = hwtcon_core_get_free_lut(&need_do_clear, &acquired_id);
		if (status != GET_LUT_OK) {
			TCON_ERR("realloc lut id fail:%d", status);
			return status;
		}
	}

	spin_lock_irqsave(&hwtcon_fb_info()->lut_active_lock, flags);
	active_lut = hwtcon_fb_info()->lut_active & ~(1LL << acquired_id);
	if (hw_tcon_get_dual_mode())
		active_lut &= ~(1LL << (acquired_id + 1));

	spin_unlock_irqrestore(&hwtcon_fb_info()->lut_active_lock, flags);

	/* allocate lut success. */
	if (need_do_clear)
		pipeline_flag |= PIPELINE_FLAG_CLEAR;

	memset(&config, 0, sizeof(config));
	memset(info, 0, sizeof(*info));

	config.update_lut = acquired_id;
	config.active_lut0 = (u32) (active_lut & 0xFFFFFFFF);
	config.active_lut1 = (u32) (active_lut >> 32);
	config.pipeline_ctl_flag = pipeline_flag;
	config.update_region.x = hwtcon_core_get_task_region(task).x;
	config.update_region.y = hwtcon_core_get_task_region(task).y;
	config.update_region.width = hwtcon_core_get_task_region(task).width;
	config.update_region.height = hwtcon_core_get_task_region(task).height;

	config.use_sw_config_img_pitch = true;
	config.img_pitch = hw_tcon_get_display_width();

	if (hw_tcon_get_dual_mode()) {
		if (task->regal_status == REGAL_STATUS_REGAL_HANDLED)
			status = pipeline_config_trigger_twice(hwtcon_fb_info()->temp_img_buffer_pa,
				hwtcon_fb_info()->wb_buffer_pa,
				hwtcon_fb_info()->wb_buffer_pa,
				config, info);
		else
			status = pipeline_config_trigger_twice(hwtcon_fb_info()->img_buffer_pa,
				hwtcon_fb_info()->wb_buffer_pa,
				hwtcon_fb_info()->wb_buffer_pa,
				config, info);
		TCON_LOG("trigger pipeline twice");
	} else {
		if (task->regal_status == REGAL_STATUS_REGAL_HANDLED)
			status = pipeline_config_trigger(hwtcon_fb_info()->temp_img_buffer_pa,
				hwtcon_fb_info()->wb_buffer_pa,
				hwtcon_fb_info()->wb_buffer_pa,
				config, info);
		else
			status = pipeline_config_trigger(hwtcon_fb_info()->img_buffer_pa,
				hwtcon_fb_info()->wb_buffer_pa,
				hwtcon_fb_info()->wb_buffer_pa,
				config, info);
		TCON_LOG("trigger pipeline onece");
	}
	if (status != 0) {
		/* pipeline update working buffer fail,
		 * put back acquired lut id
		 */
		spin_lock_irqsave(&hwtcon_fb_info()->lut_free_lock, flags);
		hwtcon_fb_info()->lut_free |= (1LL << acquired_id);
		if (hw_tcon_get_dual_mode())
			hwtcon_fb_info()->lut_free |= (1LL << (acquired_id + 1));
		spin_unlock_irqrestore(&hwtcon_fb_info()->lut_free_lock, flags);

		spin_lock_irqsave(&hwtcon_fb_info()->lut_active_lock, flags);
		hwtcon_fb_info()->lut_active &= ~(1LL << acquired_id);
		if (hw_tcon_get_dual_mode())
			hwtcon_fb_info()->lut_active &= ~(1LL << (acquired_id + 1));
		spin_unlock_irqrestore(&hwtcon_fb_info()->lut_active_lock, flags);
		return status;
	}

	/* update free lut */
	if (info->update_void) {
		/* pipeline does not update working buffer, put back acquired lut id */
		spin_lock_irqsave(&hwtcon_fb_info()->lut_free_lock, flags);
		hwtcon_fb_info()->lut_free |= (1LL << acquired_id);
		if (hw_tcon_get_dual_mode())
			hwtcon_fb_info()->lut_free |= (1LL << (acquired_id + 1));
		spin_unlock_irqrestore(&hwtcon_fb_info()->lut_free_lock, flags);

		spin_lock_irqsave(&hwtcon_fb_info()->lut_active_lock, flags);
		hwtcon_fb_info()->lut_active &= ~(1LL << acquired_id);
		if (hw_tcon_get_dual_mode())
			hwtcon_fb_info()->lut_active &= ~(1LL << (acquired_id + 1));
		spin_unlock_irqrestore(&hwtcon_fb_info()->lut_active_lock, flags);
	} else {
		// update previous buffers if not void
		pipeline_copy_previous_buffers(task);
		task->assign_lut = acquired_id;

		spin_lock_irqsave(&hwtcon_fb_info()->lut_pic_order_lock, flags);
		hwtcon_fb_info()->lut_pic_order[acquired_id] = task->pic_order;
		TCON_LOG("trigger: updata_order:%d, lut_pic_order[%d]:%d, task pic order:%d",
			task->update_order, acquired_id,
			hwtcon_fb_info()->lut_pic_order[acquired_id],
			task->pic_order);
		spin_unlock_irqrestore(&hwtcon_fb_info()->lut_pic_order_lock, flags);

		if (pipeline_flag & PIPELINE_FLAG_CLEAR) {
			spin_lock_irqsave(&hwtcon_fb_info()->lut_free_lock, flags);
			if (hw_tcon_get_dual_mode())
				hwtcon_fb_info()->lut_free =
					~(active_lut |
						(1LL << acquired_id) | (1LL << (acquired_id + 1)))
					& hw_tcon_get_all_lut();
			else
				hwtcon_fb_info()->lut_free =
					~(active_lut | 1LL << acquired_id) & hw_tcon_get_all_lut();
			spin_unlock_irqrestore(&hwtcon_fb_info()->lut_free_lock, flags);
		}
	}

	return 0;

}

int pipeline_handle_previous_csr2_partial_update(struct hwtcon_task *task, struct pipeline_info *info)
{
	int status = 0;
	int acquired_id = 0;
	bool need_do_clear = false;
	u32 pipeline_flag = PIPELINE_FLAG_Y5_INPUT;
	struct pipeline_config_info config;
	u64 active_lut = 0LL;
	unsigned long flags;

	/* full / partial update */
	TCON_LOG("trigger csr2_partial update");

	pipeline_flag |= PIPELINE_FLAG_FULL_UPDATE;

	/* allocate a free lut */
	status = hwtcon_core_get_free_lut(&need_do_clear, &acquired_id);
	if (status == GET_LUT_ERR || status == GET_LUT_TIMEOUT)
		return status;
	if (status == GET_LUT_BUSY) {
		status = hwtcon_core_get_free_lut(&need_do_clear, &acquired_id);
		if (status != GET_LUT_OK) {
			TCON_ERR("realloc lut id fail:%d", status);
			return status;
		}
	}

	spin_lock_irqsave(&hwtcon_fb_info()->lut_active_lock, flags);
	active_lut = hwtcon_fb_info()->lut_active & ~(1LL << acquired_id);
	if (hw_tcon_get_dual_mode())
		active_lut &= ~(1LL << (acquired_id + 1));

	spin_unlock_irqrestore(&hwtcon_fb_info()->lut_active_lock, flags);


	/* allocate lut success. */
	if (need_do_clear)
		pipeline_flag |= PIPELINE_FLAG_CLEAR;

	pipeline_flag |= PIPELINE_FLAG_FULL_UPDATE |
		PIPELINE_FLAG_Y5_INPUT | PIPELINE_FLAG_KEEP_PRE;

	memset(&config, 0, sizeof(config));
	memset(info, 0, sizeof(*info));

	config.update_lut = acquired_id;
	config.active_lut0 = (u32) (active_lut & 0xFFFFFFFF);
	config.active_lut1 = (u32) (active_lut >> 32);
	config.pipeline_ctl_flag = pipeline_flag;
	config.update_region.x = hwtcon_core_get_task_region(task).x;
	config.update_region.y = hwtcon_core_get_task_region(task).y;
	config.update_region.width = hwtcon_core_get_task_region(task).width;
	config.update_region.height = hwtcon_core_get_task_region(task).height;

	config.use_sw_config_img_pitch = true;
	config.img_pitch = hw_tcon_get_display_width();

	if (hw_tcon_get_dual_mode()) {
		status = pipeline_config_trigger_twice(hwtcon_fb_info()->partial_regal_buffer2_pa,
			hwtcon_fb_info()->wb_buffer_pa,
			hwtcon_fb_info()->wb_buffer_pa,
			config, info);
		TCON_LOG("trigger pipeline twice");
	} else {
		status = pipeline_config_trigger(hwtcon_fb_info()->partial_regal_buffer2_pa,
			hwtcon_fb_info()->wb_buffer_pa,
			hwtcon_fb_info()->wb_buffer_pa,
			config, info);
	}

	if (status != 0) {
		/* pipeline update working buffer fail,
		 * put back acquired lut id
		 */
		TCON_ERR("fail to write csr2_partial");
		spin_lock_irqsave(&hwtcon_fb_info()->lut_free_lock, flags);
		hwtcon_fb_info()->lut_free |= (1LL << acquired_id);
		if (hw_tcon_get_dual_mode())
			hwtcon_fb_info()->lut_free |= (1LL << (acquired_id + 1));
		spin_unlock_irqrestore(&hwtcon_fb_info()->lut_free_lock, flags);

		spin_lock_irqsave(&hwtcon_fb_info()->lut_active_lock, flags);
		hwtcon_fb_info()->lut_active &= ~(1LL << acquired_id);
		if (hw_tcon_get_dual_mode())
			hwtcon_fb_info()->lut_active &= ~(1LL << (acquired_id + 1));
		spin_unlock_irqrestore(&hwtcon_fb_info()->lut_active_lock, flags);
		return status;
	}

	/* update free lut */
	if (info->update_void) {
		TCON_ERR("write csr2_partial VOID");
		/* pipeline does not update working buffer, put back acquired lut id */
		spin_lock_irqsave(&hwtcon_fb_info()->lut_free_lock, flags);
		hwtcon_fb_info()->lut_free |= (1LL << acquired_id);
		if (hw_tcon_get_dual_mode())
			hwtcon_fb_info()->lut_free |= (1LL << (acquired_id + 1));
		spin_unlock_irqrestore(&hwtcon_fb_info()->lut_free_lock, flags);

		spin_lock_irqsave(&hwtcon_fb_info()->lut_active_lock, flags);
		hwtcon_fb_info()->lut_active &= ~(1LL << acquired_id);
		if (hw_tcon_get_dual_mode())
			hwtcon_fb_info()->lut_active &= ~(1LL << (acquired_id + 1));
		spin_unlock_irqrestore(&hwtcon_fb_info()->lut_active_lock, flags);
	} else {
		task->assign_lut = acquired_id;
		spin_lock_irqsave(&hwtcon_fb_info()->lut_pic_order_lock, flags);
		hwtcon_fb_info()->lut_pic_order[acquired_id] = task->pic_order;
		TCON_LOG("trigger: updata_order:%d, lut_pic_order[%d]:%d, task pic order:%d",
			task->update_order, acquired_id,
			hwtcon_fb_info()->lut_pic_order[acquired_id],
			task->pic_order);
		spin_unlock_irqrestore(&hwtcon_fb_info()->lut_pic_order_lock, flags);

		if (pipeline_flag & PIPELINE_FLAG_CLEAR) {
			spin_lock_irqsave(&hwtcon_fb_info()->lut_free_lock, flags);
			if (hw_tcon_get_dual_mode())
				hwtcon_fb_info()->lut_free =
					~(active_lut |
							(1LL << acquired_id) | (1LL << (acquired_id + 1)))
					& hw_tcon_get_all_lut();
			else
				hwtcon_fb_info()->lut_free =
					~(active_lut | 1LL << acquired_id) & hw_tcon_get_all_lut();
			spin_unlock_irqrestore(&hwtcon_fb_info()->lut_free_lock, flags);
		}
	}
	return 0;
}

/*
 * Below SWIPE enum looks correct.
 * position( at least this is true for Malbac),
 * to make it right from UI perspective, we need to do
 * some conversion for other panel_rotate position.
 * Also Malbec initial rotate is at 3
 * SWIPE_DOWN = 0,
 *	SWIPE_UP = 1,
 *	SWIPE_LEFT = 2,
 *	SWIPE_RIGHT = 3,
 *
 *  Malbec Mapping
 * panel /user dir	DOWN_0	UP_1	LEFT_2  RIGHT_3
 * 0	1	0	3	2
 * 1	3	2	0	1
 * 2	0	1	2	3
 * 3	2	3	1	0
 *
 * Assuming the default portrait position is pointed
 * by hw_tcon_get_edp_rotate(),
 * we can get current mapping from the  lookup table.
 */

enum SWIPE_DIRECTION_ENUM get_dev_swipe_dir(enum SWIPE_DIRECTION_ENUM user_dir)
{
	static int dev_dir_from_user_dir_0[4][SWIPE_MAX] = {
		/* panel rotate 0 */
		{SWIPE_UP, SWIPE_DOWN, SWIPE_RIGHT, SWIPE_LEFT},
		/* panel rotate 1 */
		{SWIPE_RIGHT, SWIPE_LEFT, SWIPE_DOWN, SWIPE_UP},
		/* panel rotate 2 */
		{SWIPE_DOWN, SWIPE_UP, SWIPE_LEFT, SWIPE_RIGHT},
		/* panel rotate 3 */
		{SWIPE_LEFT, SWIPE_RIGHT, SWIPE_UP, SWIPE_DOWN},
	};

	static int dev_dir_from_user_dir_1[4][SWIPE_MAX] = {
		/* panel rotate 0 */
		{SWIPE_UP, SWIPE_DOWN, SWIPE_RIGHT, SWIPE_LEFT},
		/* panel rotate 1 */
		{SWIPE_RIGHT, SWIPE_LEFT, SWIPE_DOWN, SWIPE_UP},
		/* panel rotate 2 */
		{SWIPE_DOWN, SWIPE_UP, SWIPE_LEFT, SWIPE_RIGHT},
		/* panel rotate 3 */
		{SWIPE_LEFT, SWIPE_RIGHT, SWIPE_UP, SWIPE_DOWN},
	};

	static int dev_dir_from_user_dir_2[4][SWIPE_MAX] = {
		/* panel rotate 0 */
		{SWIPE_UP, SWIPE_DOWN, SWIPE_RIGHT, SWIPE_LEFT},
		/* panel rotate 1 */
		{SWIPE_LEFT, SWIPE_RIGHT, SWIPE_UP, SWIPE_DOWN},
		/* panel rotate 2 */
		{SWIPE_UP, SWIPE_DOWN, SWIPE_RIGHT, SWIPE_LEFT},
		/* panel rotate 3 */
		{SWIPE_RIGHT, SWIPE_LEFT, SWIPE_DOWN, SWIPE_UP},
	};

	static int dev_dir_from_user_dir_3[4][SWIPE_MAX] = {
		/* panel rotate 0 */
		{SWIPE_UP, SWIPE_DOWN, SWIPE_RIGHT, SWIPE_LEFT},
		/* panel rotate 1 */
		{SWIPE_RIGHT, SWIPE_LEFT, SWIPE_DOWN, SWIPE_UP},
		/* panel rotate 2 */
		{SWIPE_DOWN, SWIPE_UP, SWIPE_LEFT, SWIPE_RIGHT},
		/* panel rotate 3 */
		{SWIPE_LEFT, SWIPE_RIGHT, SWIPE_UP, SWIPE_DOWN},
	};

	int relative_pos = 0;
	enum SWIPE_DIRECTION_ENUM dev_dir = SWIPE_DOWN;

	/* correspond to Row 3 (180 degree) of dev_dir_from_user_dir */
	int initial_pos = hw_tcon_get_edp_rotate();
	/* get current panel rotate position */
	int cur_pos = hwtcon_fb_get_rotation();
	/*relative to Row 2, the RIGHT SWIPE direction row */
	switch (initial_pos) {
	case 0:
		relative_pos = (cur_pos - initial_pos) % 4;
		dev_dir =
			dev_dir_from_user_dir_0[relative_pos][user_dir % SWIPE_MAX];
		break;
	case 1:
		relative_pos = (cur_pos - initial_pos + 1) % 4;
		dev_dir =
			dev_dir_from_user_dir_1[relative_pos][user_dir % SWIPE_MAX];
		break;
	case 2:
		relative_pos = (cur_pos - initial_pos + 2) % 4;
		dev_dir =
			dev_dir_from_user_dir_2[relative_pos][user_dir % SWIPE_MAX];
		break;
	case 3:
		relative_pos = (cur_pos - initial_pos + 3) % 4;
		dev_dir =
			dev_dir_from_user_dir_3[relative_pos][user_dir % SWIPE_MAX];
		break;
	}
	return dev_dir;
}


int hwtcon_core_divide_swipe_region(struct swipe_info_struct swipe_info,
		struct rect update_region, struct rect *devide_regions)
{
	int i = 0;
	int stride = 0;

	if (swipe_info.count > MAX_SWIPE_COUNT || swipe_info.count == 0) {
		TCON_ERR("invalid swipe count:%d", swipe_info.count);
		return -1;
	}

	switch (get_dev_swipe_dir(swipe_info.direction)) {
	case SWIPE_RIGHT:
		stride = update_region.width / swipe_info.count;
		for (i = 0; i < swipe_info.count - 1; i++) {
			devide_regions[i].x =
				update_region.x + update_region.width - stride * (i + 1);
			devide_regions[i].y = update_region.y;
			devide_regions[i].width = stride;
			devide_regions[i].height = update_region.height;
		}
		devide_regions[swipe_info.count - 1].x = update_region.x;
		devide_regions[swipe_info.count - 1].y = update_region.y;
		devide_regions[swipe_info.count - 1].width =
		    update_region.width - stride * (swipe_info.count - 1);
		devide_regions[swipe_info.count - 1].height = update_region.height;
		break;
	case SWIPE_LEFT:
		stride = update_region.width / swipe_info.count;
		for (i = 0; i < swipe_info.count - 1; i++) {
			devide_regions[i].x = update_region.x + i * stride;
			devide_regions[i].y = update_region.y;
			devide_regions[i].width = stride;
			devide_regions[i].height = update_region.height;
		}
		devide_regions[swipe_info.count - 1].x =
			update_region.x + (swipe_info.count - 1) * stride;
		devide_regions[swipe_info.count - 1].y = update_region.y;
		devide_regions[swipe_info.count - 1].width =
			update_region.width - (swipe_info.count - 1) * stride;
		devide_regions[swipe_info.count - 1].height = update_region.height;
		break;
	case SWIPE_DOWN:
		stride = update_region.height / swipe_info.count;
		for (i = 0; i < swipe_info.count - 1; i++) {
			devide_regions[i].x = update_region.x;
			devide_regions[i].y =
				update_region.y + update_region.height - stride * (i + 1);
			devide_regions[i].width = update_region.width;
			devide_regions[i].height = stride;
		}
		devide_regions[swipe_info.count - 1].x = update_region.x;
		devide_regions[swipe_info.count - 1].y = update_region.y;
		devide_regions[swipe_info.count - 1].width = update_region.width;
		devide_regions[swipe_info.count - 1].height =
			update_region.height - stride * (swipe_info.count - 1);
		break;
	case SWIPE_UP:
		stride = update_region.height / swipe_info.count;
		for (i = 0; i < swipe_info.count - 1; i++) {
			devide_regions[i].x = update_region.x;
			devide_regions[i].y = update_region.y + stride * i;
			devide_regions[i].width = update_region.width;
			devide_regions[i].height = stride;
		}
		devide_regions[swipe_info.count - 1].x = update_region.x;
		devide_regions[swipe_info.count - 1].y =
			update_region.y + stride * (swipe_info.count - 1);
		devide_regions[swipe_info.count - 1].width = update_region.width;
		devide_regions[swipe_info.count - 1].height =
			update_region.height - (swipe_info.count - 1) * stride;
		break;
	default:
		TCON_ERR("invalid swipe direction[%d]",
			swipe_info.direction);
		return -1;
	}

	TCON_LOG("swipe direction:%d count:%d update region[%d %d %d %d]",
		swipe_info.direction,
		swipe_info.count,
		update_region.x,
		update_region.y,
		update_region.width,
		update_region.height);
	for (i = 0; i < swipe_info.count; i++)
		TCON_LOG("swipe divided region:%d [%d %d %d %d]", i,
			devide_regions[i].x,
			devide_regions[i].y,
			devide_regions[i].width,
			devide_regions[i].height);
	return 0;
}

int pipeline_handle_swipe_update(struct hwtcon_task *task,
	struct pipeline_info *info)
{
	/* swipe handle flow
	 * 1. trigger regal function, write output to temp_image_buffer.
	 * 2. wait all WF_LUT lut frame done
	 * 3. trigger pipeline for init working buffer
	 * (write all pixel's lut_id = 0x3F)
	 * 4. trigger pipeline for swipe count times
	 * 5. trigger WF_LUT show all parts of picture every each frame
	 * 6. wait all WF_LUT lut frame done
	 * 7. trigger pipeline with LUT_OVERRIDE
	 */
	int status = 0;
	int i = 0;
	int lut_id_array[MAX_SWIPE_COUNT];
	struct rect *devide_regions = NULL;
	struct rect update_region = hwtcon_core_get_task_region(task);

	if (task->swipe_info.count > MAX_SWIPE_COUNT ||
		task->swipe_info.count == 0) {
		TCON_ERR("invalid swipe count:%d", task->swipe_info.count);
		status = HWTCON_STATUS_INVALID_PARAM;
		goto FREE;
	}

	if ((task->swipe_info.direction < 0) ||
		(task->swipe_info.direction >= SWIPE_MAX)) {
		TCON_ERR("invalid swipe direction[%d]",
			task->swipe_info.direction);
		status = HWTCON_STATUS_INVALID_PARAM;
		goto FREE;
	}

	memset(lut_id_array, 63, sizeof(lut_id_array));

	devide_regions = vmalloc(sizeof(struct rect) * task->swipe_info.count);
	if (devide_regions == NULL) {
		TCON_ERR("alloc devide_regions fail");
		goto FREE;
	}
	memset(devide_regions, 0, sizeof(struct rect) * task->swipe_info.count);

	status = hwtcon_core_divide_swipe_region(task->swipe_info,
		update_region, devide_regions);
	if (status != 0)
		goto FREE;

	/* step 1: trigger regal function, write output to temp_image_buffer. */
	TCON_LOG("swipe_color:%d, swipe_debug_info: color:%d, color_phase2:%d color_clear:%d gray_phase2:%d gray_clear:%d",
			task->swipe_info.is_color,
			hwtcon_debug_get_info()->swipe_color,
			hwtcon_debug_get_info()->swipe_color_phase2, hwtcon_debug_get_info()->swipe_color_clear,
			hwtcon_debug_get_info()->swipe_gray_phase2, hwtcon_debug_get_info()->swipe_gray_clear
			);
	if (task->swipe_info.is_color) {
		task->time_csr_start = timeofday_ms();
		status = hwtcon_mdp_swipe_clear_handle(task);
		if (status)
			TCON_ERR("trigger color swipe regal fail");
		else
			TCON_LOG("trigger color swipe regal successfully");
		hwtcon_fb_clean_cache(hwtcon_fb_info()->temp_img_cache_buf);
		hwtcon_fb_clean_cache(hwtcon_fb_info()->partial_regal_cache_buf2);
		task->time_csr_end = timeofday_ms();
		TCON_LOG("color swipe regal takes: %d ms", hwtcon_hal_get_time_in_ms(task->time_csr_start, task->time_csr_end));
	} else {
		if (!task->swipe_info.is_color && hwtcon_debug_get_info()->swipe_gray_phase2)
			memcpy(hwtcon_fb_info()->partial_regal_buffer2_va,
			hwtcon_fb_info()->tmp_buffer_va, hwtcon_fb_info()->tmp_buffer_size);
		status = pipeline_trigger_regal(task);
	}
	if (status != 0)
		goto FREE;

	/* step 2: wait all WF_LUT lut frame done */
	status = hwtcon_core_wait_all_wf_lut_release();
	if (status != 0)
		goto FREE;

	/* step 3: trigger pipeline for init working buffer
	 * (write all pixel's lut_id = 0x3F)
	 */
	status = pipeline_init_working_buffer();
	if (status != 0)
		goto FREE;

	/* step 4: trigger pipeline for swipe count times */
	for (i = 0; i < task->swipe_info.count; i++) {
		struct pipeline_config_info config;
		struct pipeline_info info;
		int status = 0;
		u64 active_lut = 0LL;
		unsigned long flags;

		memset(&config, 0, sizeof(config));
		memset(&info, 0, sizeof(info));
		/* get free LUT id */
		hwtcon_core_get_free_lut(NULL, &config.update_lut);
		lut_id_array[i] = config.update_lut;
		config.update_region = devide_regions[i];

		spin_lock_irqsave(&hwtcon_fb_info()->lut_active_lock, flags);
		active_lut = hwtcon_fb_info()->lut_active & ~(1LL << config.update_lut);
		if (hw_tcon_get_dual_mode())
			active_lut &= ~(1LL << (config.update_lut + 1));

		spin_unlock_irqrestore(&hwtcon_fb_info()->lut_active_lock, flags);

		config.active_lut0 = (u32)(active_lut & 0xFFFFFFFF);
		config.active_lut1 = (u32)(active_lut >> 32);
		config.pipeline_ctl_flag = PIPELINE_FLAG_Y5_INPUT | PIPELINE_FLAG_FULL_UPDATE;

		config.use_sw_config_img_pitch = true;
		config.img_pitch = hw_tcon_get_display_width();

		if (hw_tcon_get_dual_mode()) {
			status = pipeline_config_trigger_twice(hwtcon_fb_info()->temp_img_buffer_pa,
				hwtcon_fb_info()->wb_buffer_pa,
				hwtcon_fb_info()->wb_buffer_pa,
				config, &info);
		} else {
			status = pipeline_config_trigger(hwtcon_fb_info()->temp_img_buffer_pa,
				hwtcon_fb_info()->wb_buffer_pa,
				hwtcon_fb_info()->wb_buffer_pa,
				config, &info);
		}
		if (status != 0)
			goto FREE;


		WARN(info.update_void == true, "swipe update void is true");
	}

	/* step 5: trigger WF_LUT show all parts of picture every each frame */
	wf_lut_config_show_picture_by_frame(lut_id_array,
		task->swipe_info.count,
		wf_lut_get_waveform_mode_slot(task->update_data.waveform_mode,
			hwtcon_core_use_night_mode()));

	/* step 6: wait all WF_LUT lut frame done */
	status = hwtcon_core_wait_all_wf_lut_release();
	if (status != 0)
		goto FREE;

	/* step 7:  trigger pipeline with LUT_OVERRIDE */
	// Check if need phase 2 first.
	if ((!task->swipe_info.is_color && hwtcon_debug_get_info()->swipe_gray_phase2) || // B/W with swipe_gray_phase2
			(task->swipe_info.is_color && hwtcon_debug_get_info()->swipe_color_phase2)) { // Color with swipe_color_phase2
		TCON_LOG("Need phase 2");
	} else {
		TCON_LOG("Skip phase 2");
		info->update_void = true;
		info->collision_lut_0 = 0;
		info->collision_lut_1 = 0;
		goto FREE;
	}

	// call gray clear if needed
	if (!task->swipe_info.is_color && hwtcon_debug_get_info()->swipe_gray_clear) {
		HWTCON_TIME T1, T2;
		T1 = timeofday_ms();
		status = hwtcon_mdp_swipe_clear_handle(task);
		if (status)
			TCON_ERR("trigger gray_swipe_clear fail");
		else
			TCON_LOG("trigger gray_swipe_clear successfully");
		hwtcon_fb_clean_cache(hwtcon_fb_info()->partial_regal_cache_buf2);
		T2 = timeofday_ms();
		TCON_LOG("gray swipe clear takes: %d ms", hwtcon_hal_get_time_in_ms(T1, T2));
	}

	do {
		struct pipeline_config_info config;
		int status = 0;
		u64 active_lut = 0LL;
		unsigned long flags;
		dma_addr_t swipe_addr;

		memset(&config, 0, sizeof(config));
		memset(info, 0, sizeof(*info));
		/* get free LUT id */
		hwtcon_core_get_free_lut(NULL, &config.update_lut);
		config.update_region = update_region;

		spin_lock_irqsave(&hwtcon_fb_info()->lut_active_lock, flags);
		active_lut = hwtcon_fb_info()->lut_active & ~(1LL << config.update_lut);
		if (hw_tcon_get_dual_mode())
			active_lut &= ~(1LL << (config.update_lut + 1));
		spin_unlock_irqrestore(&hwtcon_fb_info()->lut_active_lock, flags);
		config.active_lut0 = (u32)(active_lut & 0xFFFFFFFF);
		config.active_lut1 = (u32) (active_lut >> 32);
		WARN(active_lut != 0LL, "active_lut should all zero");

		config.pipeline_ctl_flag = PIPELINE_FLAG_Y5_INPUT |
			PIPELINE_FLAG_FULL_UPDATE |
			PIPELINE_FLAG_LUT_OVERRIDE;

		config.use_sw_config_img_pitch = true;
		config.img_pitch = hw_tcon_get_display_width();

		if ((!task->swipe_info.is_color && hwtcon_debug_get_info()->swipe_gray_clear) || // B/W with swipe_gray_clear
				(task->swipe_info.is_color && hwtcon_debug_get_info()->swipe_color_clear)) { // Color with swipe_color_clear
			TCON_LOG("p2sw use clear buffer");
			swipe_addr = hwtcon_fb_info()->partial_regal_buffer2_pa;
			config.pipeline_ctl_flag = PIPELINE_FLAG_Y5_INPUT |
				PIPELINE_FLAG_FULL_UPDATE |
				PIPELINE_FLAG_KEEP_PRE;
		} else
			swipe_addr = hwtcon_fb_info()->temp_img_buffer_pa;

		if (hw_tcon_get_dual_mode()) {
			status = pipeline_config_trigger_twice(swipe_addr,
				hwtcon_fb_info()->wb_buffer_pa,
				hwtcon_fb_info()->wb_buffer_pa,
				config, info);
		} else {
			status = pipeline_config_trigger(swipe_addr,
				hwtcon_fb_info()->wb_buffer_pa,
				hwtcon_fb_info()->wb_buffer_pa,
				config, info);
		}
		if (status != 0)
			goto FREE;

		task->assign_lut = config.update_lut;
		task->update_data.waveform_mode = WAVEFORM_MODE_P2SW;
		WARN(info->update_void == true, "swipe update void is true");

	} while (0);

FREE:
	pipeline_copy_previous_buffers(task);
	if (devide_regions)
		vfree(devide_regions);

	return status;
}

int pipeline_handle_stylus_mode_update(struct hwtcon_task *task,
	struct pipeline_info *info)
{
	int status = 0;
	int acquired_id = 0;
	u32 pipeline_flag = PIPELINE_FLAG_Y5_INPUT;
	struct pipeline_config_info config;
	u64 active_lut = 0LL;
	unsigned long flags;

	pipeline_reserved = true;
	/* allocate a free lut */
	spin_lock_irqsave(&hwtcon_fb_info()->frame_lut_lock, flags);
	acquired_id = hwtcon_fb_info()->frame_valid_lut;
	spin_unlock_irqrestore(&hwtcon_fb_info()->frame_lut_lock, flags);

	spin_lock_irqsave(&hwtcon_fb_info()->lut_active_lock, flags);
	active_lut = hwtcon_fb_info()->lut_active;
	spin_unlock_irqrestore(&hwtcon_fb_info()->lut_active_lock, flags);

	TCON_LOG("trigger pipeline with lut[%d] active[0x%016llx]",
		acquired_id,
		active_lut);

	memset(&config, 0, sizeof(config));
	memset(info, 0, sizeof(*info));

	config.update_lut = acquired_id;
	config.active_lut0 = (u32) (active_lut & 0xFFFFFFFF);
	config.active_lut1 = (u32) (active_lut >> 32);
	config.pipeline_ctl_flag = pipeline_flag;
	config.update_region.x = hwtcon_core_get_task_region(task).x;
	config.update_region.y = hwtcon_core_get_task_region(task).y;
	config.update_region.width = hwtcon_core_get_task_region(task).width;
	config.update_region.height = hwtcon_core_get_task_region(task).height;

	config.use_sw_config_img_pitch = true;
	config.img_pitch = hw_tcon_get_display_width();

	if (hw_tcon_get_dual_mode()) {
		status = pipeline_config_trigger_twice(hwtcon_fb_info()->img_buffer_pa,
			hwtcon_fb_info()->wb_buffer_pa,
			hwtcon_fb_info()->wb_buffer_pa,
			config, info);
	} else {
		status = pipeline_config_trigger(hwtcon_fb_info()->img_buffer_pa,
			hwtcon_fb_info()->wb_buffer_pa,
			hwtcon_fb_info()->wb_buffer_pa,
			config, info);
	}

	if (status != 0) {
		/* pipeline update working buffer fail,
		 * put back acquired lut id
		 */
		TCON_ERR("trigger pipeline failed[%d]", status);
		return status;
	}

	/* update free lut */
	task->assign_lut = acquired_id;
	return 0;
}


void rdma_config_smi_setting(struct cmdq_pkt *pkt)
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

	for (i = 0; i < 10; i++) {
		pp_write(pkt, 0x14003380 + i * 4, 0);
		pp_write(pkt, 0x14003f80 + i * 4, 0);
	}

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
