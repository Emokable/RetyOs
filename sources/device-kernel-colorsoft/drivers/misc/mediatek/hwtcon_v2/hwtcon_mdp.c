// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Jiaguang Zhang <jiaguang.zhang@mediatek.com>
 */

#include <linux/videodev2.h>
#include "hwtcon_def.h"
#include "hwtcon_fb.h"
#include "hwtcon_epd.h"
#include "hwtcon_debug.h"
#include "hwtcon_mdp.h"
#include "hwtcon_def.h"
#include "mtk_mdp_gamma.h"
#include "hwtcon_driver.h"
#include "hwtcon_hal.h"

int hwtcon_mdp_copy_buffer_with_region(char *dst_buffer,
		int dst_pitch, const struct rect *dst_region,
		char *src_buffer, int src_pitch,
		const struct rect *src_region)
{
	int i = 0;
	int j = 0;

	if (src_region->width != dst_region->width ||
		src_region->height != dst_region->height) {
		TCON_ERR("copy src & dst region not match");
		return HWTCON_STATUS_REGION_NOT_MATCH;
	}

	if (dst_pitch < dst_region->width) {
		TCON_ERR("invalid region: dst(%d %d %d %d) dst pitch:%d",
			dst_region->x,
			dst_region->y,
			dst_region->width,
			dst_region->height,
			dst_pitch);
		return HWTCON_STATUS_INVALID_PARAM;
	}
	if (src_pitch < src_region->width) {
		TCON_ERR("invalid region: src(%d %d %d %d) src pitch:%d",
			src_region->x,
			src_region->y,
			src_region->width,
			src_region->height,
			src_pitch);
		return HWTCON_STATUS_INVALID_PARAM;
	}

	for (i = 0; i < src_region->width; i++)
		for (j = 0; j < src_region->height; j++) {
			/* copy src region(src_region.x, src_region.y)
			 * to dst(dst_region.x, dst_region.y)
			 */
			dst_buffer[(dst_region->y + j) * dst_pitch +
				(dst_region->x + i)] =
				src_buffer[(src_region->y + j) * src_pitch +
				(src_region->x + i)] >> 4 << 4;
		}

	return 0;
}

static int sw_convert(u32 src_w, u32 src_h, u32 dst_w, u32 dst_h, u32 src_pitch, u8 *src_va,
		u8 *dst_va, u32 crop_x, u32 crop_y, u32 crop_w, u32 crop_h, u8 invert,
		u32 rotate, u8 hflip)
{
	u8 *src_addr, *dst_addr, *base_dst_addr;
	bool inc;
	u16 ix, jx;
	u8 val;

	/* calculate the starting address in image buffer */
	if (rotate == 90) {
		if (hflip)
			base_dst_addr = dst_va + dst_w * crop_x + crop_y;
		else
			base_dst_addr = dst_va + dst_w * crop_x + dst_w - crop_y - 1;
	} else if (rotate == 270) {
		if (hflip)
			base_dst_addr = dst_va + dst_w * (dst_h - crop_x - 1) + dst_w - crop_y - 1;
		else
			base_dst_addr = dst_va + dst_w * (dst_h - crop_x - 1) + crop_y;
	} else
		return -1;

	src_addr = src_va + src_pitch * crop_y + crop_x;
	inc = (rotate == 90 && hflip) || (rotate == 270 && hflip == 0);

	/* convert each line in the update region */
	for (ix = 0; ix < crop_h; ix++) {
		dst_addr = base_dst_addr + (inc ? ix : -ix);
		/* convert each pixel in a row */
		for (jx = 0; jx < crop_w; jx++) {
			val = (*src_addr) ^ (invert ? 0xff : 0);
			if (val == 0xff || val == 0x0)
				*dst_addr = val & 0xf0;
			src_addr++;
			dst_addr += (rotate == 90 ? dst_w : -dst_w);
		}
		src_addr += src_pitch - crop_w;
	}
	return 0;
}

int hwtcon_mdp_convert(struct hwtcon_task *task)
{
	u32 src_buffer_pa = 0;
	u32 dst_buffer_pa = 0;
	u32 src_buffer_width = 0;
	u32 dst_buffer_pitch = 0;
	u32 src_buffer_height = 0;
	u32 dst_buffer_height = 0;
	struct rect src_region = { 0 };
	struct rect dst_region = { 0 };
	u32 src_format = V4L2_PIX_FMT_Y8;
	u32 dst_format = V4L2_PIX_FMT_Y4_M0;
	u8 dither_enable = 0;
	u32 dither_mode = MDP_DITHER_ALGO_Y8_Y4_S;
	u8 invert_enable = 0;
	int rotate = hwtcon_fb_get_rotation() * 90;
	u32 gamma_flag = MDP_GAMMA_USE_CMAP;

	if (task->update_data.flags & EPDC_FLAG_ENABLE_INVERSION)
		gamma_flag |= MDP_GAMMA_ENABLE_INVERSION;

	if (task->update_data.flags & EPDC_FLAG_FORCE_MONOCHROME)
		gamma_flag |= MDP_GAMMA_FORCE_MONOCHROME;

	src_region = hwtcon_core_get_mdp_region(task);
	dst_region = hwtcon_core_get_task_region(task);
	if (hw_tcon_get_epd_type()) {
		if (!hwtcon_core_use_cfa_color_mapping(task)) {
			gamma_flag = MDP_GAMMA_USE_CINFO;
			src_format = hwtcon_fb_get_cur_color_format();
			dst_format = V4L2_PIX_FMT_Y8;
		} else {
			gamma_flag = MDP_GAMMA_USE_CFA;
			if (task->update_data.flags & EPDC_FLAG_ENABLE_INVERSION)
				TCON_ERR("Inversion used on color content?");
			src_format = V4L2_PIX_FMT_Y8;
			dst_format = V4L2_PIX_FMT_Y8;
		}
	} else
		src_format = hwtcon_fb_get_cur_color_format();

	hwtcon_core_get_mdp_input_buffer_info(task, &src_buffer_pa,
			&src_buffer_width, &src_buffer_height);
	hwtcon_core_get_task_buffer_info(task, &dst_buffer_pa,
			&dst_buffer_pitch, &dst_buffer_height);

	if (task->update_data.flags & EPDC_FLAG_USE_DITHERING_Y4)
		dither_enable = 1;

	if (task->update_data.flags & EPDC_FLAG_ENABLE_INVERSION)
		invert_enable = !invert_enable;
	/* night mode, need to invert every update */
	if (hwtcon_fb_get_grayscale() == GRAYSCALE_8BIT_INVERTED)
		invert_enable = !invert_enable;

	/*
	 *if (task->update_data.waveform_mode == WAVEFORM_MODE_DU
	 *	|| task->update_data.waveform_mode == WAVEFORM_MODE_A2) {
	 *	dither_enable = 1;
	 *	dither_mode = MDP_DITHER_ALGO_Y8_Y1_S;
	 *}
	 */
	if (task->update_data.flags & EPDC_FLAG_USE_DITHERING_Y1) {
		dither_enable = 1;
		dither_mode = task->update_data.dither_mode;
		if (dither_mode != MDP_DITHER_ALGO_Y8_Y1_Q &&
				dither_mode != MDP_DITHER_ALGO_Y8_Y1_B &&
				dither_mode != MDP_DITHER_ALGO_Y8_Y1_S)
			dither_mode = MDP_DITHER_ALGO_Y8_Y1_S; /*use Y8Y1S as default*/
		dst_format = V4L2_PIX_FMT_Y1_M1;
		TCON_LOG("use mdp dither y1");
	}

	if (task->update_data.flags & EPDC_FLAG_USE_DITHERING_Y4) {
		dither_enable = 1;
		dither_mode = MDP_DITHER_ALGO_Y8_Y4_S;
		dst_format = V4L2_PIX_FMT_Y4_M0;
	}

	TCON_LOG("[MARKER]:%d MDP convert, dither:%d invert:%d rotate:%d degree gamma:0x%08x",
		task->update_data.update_marker,
		dither_enable, invert_enable, rotate, gamma_flag);
	if (rotate) {
		TCON_LOG("[MARKER]:%d src width:%d height:%d region:[%d %d %d %d]",
			task->update_data.update_marker,
			src_buffer_width, src_buffer_height,
			src_region.x,
			src_region.y,
			src_region.width,
			src_region.height);
		TCON_LOG("MARKER:%d dst width:%d height:%d region:[%d %d %d %d]",
			task->update_data.update_marker,
			dst_buffer_pitch, dst_buffer_height,
			dst_region.x,
			dst_region.y,
			dst_region.width,
			dst_region.height);
	}
	if (hwtcon_fb_info()->stylus_mode &&
		(rotate == 90 || rotate == 270) &&
		(task->update_data.waveform_mode == WAVEFORM_MODE_DU ||
		task->update_data.waveform_mode == WAVEFORM_MODE_DUNM) &&
		src_region.width * src_region.height < MIN_MDP_SIZE) {
		sw_convert(src_buffer_width, src_buffer_height,
			dst_buffer_pitch, dst_buffer_height,
			hwtcon_fb_get_virtual_width(),
			hwtcon_fb_info()->fb_buffer_va,
			hwtcon_fb_info()->img_buffer_va,
			src_region.x, src_region.y,
			src_region.width, src_region.height,
			invert_enable, rotate,
			hw_tcon_get_edp_hflip());
	} else {
		easy_mtk_mdp_func_v2(src_buffer_width, src_buffer_height,
				dst_buffer_pitch, dst_buffer_height,
				src_format, dst_format,
				hwtcon_fb_get_virtual_width(), dst_buffer_pitch,
				src_buffer_pa, dst_buffer_pa,
				src_region.x,
				src_region.y,
				src_region.width,
				src_region.height,
			dither_enable, dither_mode,
			invert_enable,
			rotate, gamma_flag, hw_tcon_get_edp_hflip(), 0, 0, 0);
	}

	/* Signal MDP completion for all markers */
	{
		struct update_marker_struct *update_marker, *tmp;
		list_for_each_entry_safe(update_marker, tmp,
				&task->marker_info_list.list, task_list) {
			complete(&update_marker->mdp_completion);
			TCON_LOG("[MARKER] mdp completion: %d", update_marker->update_marker);
		}
	}

	return 0;
}

int hwtcon_mdp_memcpy(dma_addr_t dst_buffer, dma_addr_t src_buffer)
{
	int buffer_width = hw_tcon_get_display_width();
	int buffer_height = hw_tcon_get_display_height();

	if (src_buffer == hwtcon_fb_info()->fb_buffer_pa &&
		hw_tcon_get_epd_type() != 0) {
		buffer_width *= hwtcon_device_info()->color_format;
	}

	return easy_mtk_mdp_func_v2(buffer_width,
			buffer_height,
			buffer_width,
			buffer_height,
			V4L2_PIX_FMT_Y8, V4L2_PIX_FMT_Y8,
			buffer_width, buffer_width,
			src_buffer, dst_buffer,
				0,
				0,
				buffer_width,
				buffer_height,
				0, MDP_DITHER_ALGO_Y8_Y4_S,
				0, 0, 0, 0, 0, 0, 0);
}

int hwtcon_mdp_region_memcpy(dma_addr_t dst_buffer,
	dma_addr_t src_buffer, struct rect region,
	int buffer_width, int buffer_height, int pixel_width)
{
	buffer_width *= pixel_width;
	region.x *= pixel_width;
	region.width *= pixel_width;

	return easy_mtk_mdp_func_v2(buffer_width, buffer_height, buffer_width, buffer_height,
			V4L2_PIX_FMT_Y8, V4L2_PIX_FMT_Y8, buffer_width, buffer_width,
			src_buffer, dst_buffer, region.x, region.y, region.width,
			region.height, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

int hwtcon_mdp_regal_handle(struct hwtcon_task *task)
{
	u32 src_buffer_pa = 0;
	u32 dst_buffer_pa = 0;
	u32 src_buffer_width = 0;
	u32 dst_buffer_pitch = 0;
	u32 src_buffer_height = 0;
	u32 dst_buffer_height = 0;
	struct rect src_region = {0};
	struct rect dst_region = {0};
	u32 src_format = V4L2_PIX_FMT_Y8;
	u32 dst_format = V4L2_PIX_FMT_Y8;
	u8 dither_enable = 0;
	u8 invert_enable = 0;
	int rotate = hwtcon_fb_get_rotation() * 90;
	u32 gamma_flag = 0;

	src_region = hwtcon_core_get_mdp_region(task);
	dst_region = hwtcon_core_get_task_region(task);
	hwtcon_core_get_mdp_input_buffer_info(task, &src_buffer_pa,
		&src_buffer_width, &src_buffer_height);
	hwtcon_core_get_task_buffer_info(task, &dst_buffer_pa,
			&dst_buffer_pitch, &dst_buffer_height);
	TCON_LOG("update mode :%d", task->update_data.update_mode);
	TCON_LOG("Regal MARKER:%d dst width:%d height:%d region:[%d %d %d %d]",
		task->update_data.update_marker,
		dst_buffer_pitch, dst_buffer_height,
		dst_region.x,
		dst_region.y,
		dst_region.width,
		dst_region.height);
	easy_mtk_mdp_func_v2(src_buffer_width, src_buffer_height,
		dst_buffer_pitch, dst_buffer_height,
		src_format, dst_format,
		hwtcon_fb_get_virtual_width(), dst_buffer_pitch,
		src_buffer_pa, dst_buffer_pa,
		src_region.x,
		src_region.y,
		src_region.width,
		src_region.height,
		dither_enable, MDP_DITHER_ALGO_Y8_Y4_S,
		invert_enable,
		rotate, gamma_flag, hw_tcon_get_edp_hflip(),
		0, 1, (!!task->update_data.update_mode << 1 | !!hwtcon_core_use_night_mode()));
	return 0;
}

int hwtcon_mdp_swipe_clear_handle(struct hwtcon_task *task)
{
	u32 src_buffer_pa = 0;
	u32 dst_buffer_pa = 0;
	u32 src_buffer_width = 0;
	u32 dst_buffer_pitch = 0;
	u32 src_buffer_height = 0;
	u32 dst_buffer_height = 0;
	struct rect src_region = {0};
	struct rect dst_region = {0};
	u32 src_format = V4L2_PIX_FMT_Y8;
	u32 dst_format = V4L2_PIX_FMT_Y8;
	int rotate = hwtcon_fb_get_rotation() * 90;
	int swipe = 0;

	if (task->swipe_info.is_color)
		swipe = hwtcon_debug_get_info()->swipe_color_clear ? 4 : 2;
	else if (!task->swipe_info.is_color && hwtcon_debug_get_info()->swipe_gray_clear)
		swipe = 6;
	else
		return 0;

	src_region = hwtcon_core_get_mdp_region(task);
	dst_region = hwtcon_core_get_task_region(task);
	hwtcon_core_get_mdp_input_buffer_info(task, &src_buffer_pa,
		&src_buffer_width, &src_buffer_height);
	hwtcon_core_get_task_buffer_info(task, &dst_buffer_pa,
			&dst_buffer_pitch, &dst_buffer_height);
	easy_mtk_mdp_func_v2(src_buffer_width, src_buffer_height,
		dst_buffer_pitch, dst_buffer_height,
		src_format, dst_format,
		hwtcon_fb_get_virtual_width(), dst_buffer_pitch,
		src_buffer_pa, dst_buffer_pa,
		src_region.x,
		src_region.y,
		src_region.width,
		src_region.height,
		0, MDP_DITHER_ALGO_Y8_Y4_S,
		0,
		rotate, 0, hw_tcon_get_edp_hflip(),
		0, 1, swipe);
	return 0;
}
