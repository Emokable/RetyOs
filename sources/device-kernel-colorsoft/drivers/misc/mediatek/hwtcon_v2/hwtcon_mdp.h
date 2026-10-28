/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Jiaguang Zhang <jiaguang.zhang@mediatek.com>
 */

#ifndef __HWTCON_MDP_H__
#define __HWTCON_MDP_H__

#include "hwtcon_core.h"
#include "hwtcon_rect.h"

#define MIN_MDP_SIZE 6000

int hwtcon_mdp_convert(struct hwtcon_task *task);

int easy_mtk_mdp_func(
		u32 src_w, u32 src_h,
		u32 dst_w, u32 dst_h,
		u32 src_fmt, u32 dst_fmt,
		u32 src_pitch, u32 dst_pitch,
		dma_addr_t src_mva, dma_addr_t dst_mva,
		u32 crop_x, u32 crop_y,
		u32 crop_w, u32 crop_h,
		u8 dth_en, u32 dth_algo, u8 invert,
		u32 rotate, u32 gamma_flag, u8 hflip, u8 vflip);

int easy_mtk_mdp_func_v2(
		u32 src_w, u32 src_h,
		u32 dst_w, u32 dst_h,
		u32 src_fmt, u32 dst_fmt,
		u32 src_pitch, u32 dst_pitch,
		dma_addr_t src_mva, dma_addr_t dst_mva,
		u32 crop_x, u32 crop_y,
		u32 crop_w, u32 crop_h,
		u8 dth_en, u32 dth_algo, u8 invert,
		u32 rotate, u32 gamma_flag, u8 hflip, u8 vflip, u8 regal, s32 update_mode);

int hwtcon_mdp_copy_buffer_with_region(char *dst_buffer,
		int dst_pitch, const struct rect *dst_region,
		char *src_buffer, int src_pitch,
		const struct rect *src_region);
int hwtcon_mdp_memcpy(dma_addr_t src_buffer, dma_addr_t dst_buffer);
int hwtcon_mdp_region_memcpy(dma_addr_t dst_buffer,
	dma_addr_t src_buffer, struct rect region,
	int buffer_width, int buffer_height, int pixel_width);
int hwtcon_mdp_regal_handle(struct hwtcon_task *task);
int hwtcon_mdp_swipe_clear_handle(struct hwtcon_task *task);

#endif
