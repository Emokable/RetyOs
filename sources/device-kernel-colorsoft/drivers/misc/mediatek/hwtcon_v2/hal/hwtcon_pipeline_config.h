/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Jiaguang Zhang <jiaguang.zhang@mediatek.com>
 */

#ifndef __HWTCON_PIPELINE_CONFIG_H__
#define __HWTCON_PIPELINE_CONFIG_H__
#include <linux/mailbox/mtk-cmdq-mailbox-ext.h>
#include "hwtcon_def.h"
#include "hwtcon_rect.h"

struct hwtcon_task;
enum PITCH_SELECT {
	PITCH_SELECT_HW_AUTO = 0,
	PITCH_SELECT_SW_CONFIG = 1,
};

enum SWIPE_DIRECTION_ENUM {
	SWIPE_DOWN = 0,
	SWIPE_UP = 1,
	SWIPE_LEFT = 2,
	SWIPE_RIGHT = 3,
	SWIPE_MAX,
};

enum PIPELINE_FLAG_ENUM {
	/* 0: partial update,
	 * 1: full update
	 */
	PIPELINE_FLAG_FULL_UPDATE = BIT_MASK(0),
	/* 0: Image Buffer input format is Y4,
	 * 1: Image Buffer input format is Y5
	 */
	PIPELINE_FLAG_Y5_INPUT = BIT_MASK(1),
	/* 0: Normal Pipeline processing flow,
	 * 1: Only update LUT field of each non-colliding pixel
	 */
	PIPELINE_FLAG_LUT_OVERRIDE = BIT_MASK(2),
	/* 0: Normal Pipeline processing flow,
	 * 1: REGAL Only mode.
	 */
	PIPELINE_FLAG_REGAL = BIT_MASK(3),
		/* 0: Normal Pipeline processing flow,
	     * 1: keep previous data not change, only update current val.
	     */
	PIPELINE_FLAG_KEEP_PRE = BIT_MASK(4),
	/* 0: Normal Pipeline processing flow,
	 * 1: dry run, don't update working buffer.
	 */
	PIPELINE_FLAG_DRY_RUN = BIT_MASK(5),
	/* 0: Normal Pipeline processing flow,
	 * 1: clear all non active LUT to 0x3F
	 */
	PIPELINE_FLAG_CLEAR = BIT_MASK(15),
};

#define MAX_SWIPE_COUNT 60
#define STEPS_WB_INIT 64
struct swipe_info_struct {
	bool enable;
	enum SWIPE_DIRECTION_ENUM direction;
	int count;
	bool is_color;
};

struct pipeline_config_info {
	u32 update_lut;
	struct rect update_region;

	u32 active_lut0;
	u32 active_lut1;
	u32 pipeline_ctl_flag;	/* pipeline control setting */
	u32 regal_mode;

	bool use_wb_merge_region;
	struct rect wb_merge_region;

	bool use_sw_config_img_pitch;
	int img_pitch;
	bool use_sw_config_wb_pitch;
	int wb_pitch;
};

struct pipeline_info {
	u32 collision_lut_0;	/* collision lut bits:0 ~ 31 */
	u32 collision_lut_1;	/* collision lut bits:32 ~ 63 */
	struct rect collision_region;	/* collision region info */
	bool update_void;	/* not change working buffer */
	bool do_clear;		/* pipeline is clearing working buffer lut id */
	u32 next_histogram;
	u32 current_histogram;
	u32 panel_width;	/* panel width */
	u32 panel_height;	/* panel height */
};

void rdma_config_smi_setting(struct cmdq_pkt *pkt);
void pipeline_config_reset_wb_wdma(struct cmdq_pkt *pkt);
void pipeline_config_reset_img_rdma(struct cmdq_pkt *pkt);
void pipeline_config_reset_wb_rdma(struct cmdq_pkt *pkt);
void pipeline_config_enable_irq(struct cmdq_pkt *pkt, enum HWTCON_IRQ_TYPE irq);
void pipeline_config_disable_irq(struct cmdq_pkt *pkt, enum HWTCON_IRQ_TYPE irq);

void pipeline_config_panel_resolution(struct cmdq_pkt *pkt, int panel_width, int panel_height);
void pipeline_config_fifo(struct cmdq_pkt *pkt, bool enable, u32 fifo_size, u32 read_threshold);
void pipeline_config_wb_merge_region(struct cmdq_pkt *pkt, bool enable, struct rect *merge_region);


void pipeline_config_clear_irq(struct cmdq_pkt *pkt, enum HWTCON_IRQ_TYPE irq);
void pipeline_config_update_lut(struct cmdq_pkt *pkt, int lut_id);
void pipeline_config_enable_illegal_setting_buffer_write(struct cmdq_pkt *pkt, bool enable);
void paper_config_reset_dpi_on_frame_done(struct cmdq_pkt *pkt, bool enable);


void pipeline_get_collision_region(struct rect *region);
void pipeline_get_collision_lut_mask(u32 *col_lut1, u32 *col_lut0);
void pipeline_get_pixel_update_region(struct rect *region);
bool pipeline_get_update_void_status(void);
bool pipeline_get_do_clear_status(void);
void pipeline_get_histogram(u32 *next_histogram, u32 *current_histogram);

void pipeline_config_img_buffer_pitch(struct cmdq_pkt *pkt,
		enum PITCH_SELECT pitch_select_type, u32 pitch);
void pipeline_config_wb_buffer_pitch(struct cmdq_pkt *pkt,
		enum PITCH_SELECT pitch_select_type, u32 pitch);

int pipeline_config_trigger(u32 img_addr_pa,
		u32 input_wb_addr_pa,
		u32 output_wb_addr_pa,
	struct pipeline_config_info config,
	struct pipeline_info *info);
int pipeline_config_trigger_twice(u32 img_addr_pa,
	u32 input_wb_addr_pa,
	u32 output_wb_addr_pa,
	struct pipeline_config_info config,
	struct pipeline_info *info);
int pipeline_init_working_buffer(void);
int pipeline_init_working_buffer_ll(void);
int pipeline_trigger_regal(struct hwtcon_task *task);
int pipeline_handle_normal_update(struct hwtcon_task *task,
	struct pipeline_info *info);
int pipeline_handle_swipe_update(struct hwtcon_task *task,
	struct pipeline_info *info);
int pipeline_handle_stylus_mode_update(struct hwtcon_task *task,
	struct pipeline_info *info);
void pipeline_config_init_reserve(void);
struct rect  hwtcon_core_get_cfa_regal_region(void);
int pipeline_handle_previous_csr2_partial_update(struct hwtcon_task *task, struct pipeline_info *info);

#endif		/* __HWTCON_PIPELINE_CONFIG_H__ */
