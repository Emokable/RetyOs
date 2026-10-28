// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2021 Amazon.com, Inc.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */

#include "hwtcon_def.h"
#include "hwtcon_core.h"

#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/uaccess.h>

#include "hwtcon_epd.h"
#include "hwtcon_lightbox.h"

/* Lightbox (aka halftone pattern) feature */
/* A flag to indicate whether lightbox is needed or not */
static bool g_lightbox_needed;
/* The size of the checker for the pattern */
/* Default is 4px for 300ppi and 2px for 167ppi */
#define LIGHTBOX_PATTERN_DEFAULT_SIZE 4
static int g_lightbox_pattern_size = LIGHTBOX_PATTERN_DEFAULT_SIZE;
static int g_lightbox_invert = 0;
/* Control structure which contains areas that need to be excluded */
static struct mxcfb_halftone_data g_lightbox_ctrl = { {{0}, {0}}, 0 };

/*init static value to make it be in line with coding style*/
void hwtcon_lightbox_init_static_value(void)
{
	g_lightbox_needed = false;
}

/* Lightbox (aka halftone pattern) feature */


/******************************************************************
 * Lightbox (aka Halftone Pattern) API start.
 *
 * Below APIs are used to apply lightbox to updated area.
 ******************************************************************/

int hwtcon_lightbox_ioctl_set_lightbox_ctrl(void *arg)
{
	if (!copy_from_user(&g_lightbox_ctrl, arg, sizeof(g_lightbox_ctrl))) {
		int mode = g_lightbox_ctrl.halftone_mode & 0x7FFFFFFF;
		/* Enable applying lightbox(aka halftone pattern)
		 * when the exclude region is not empty.
		 */
		/* LIGL will ensure that the array will
		 * have non-empty region in front.
		 */
		/* Hence, only need to check the first region. */
		g_lightbox_needed = (g_lightbox_ctrl.region[0].width > 0 &&
				     g_lightbox_ctrl.region[0].height > 0);

		/* Lightbox Pattern Mode: 0:None,
		 * 1:Turn on and change to default checker size
		 * Others: Custom, checker size = mode - 1
		 */
		g_lightbox_invert = g_lightbox_ctrl.halftone_mode & 0x80000000;
		switch (mode) {
		case 0:
			g_lightbox_needed = false;
			break;
		case 1:
			g_lightbox_pattern_size = LIGHTBOX_PATTERN_DEFAULT_SIZE;
			break;
		default:
			g_lightbox_pattern_size = mode - 1;
			break;
		}
		TCON_LOG("g_lightbox_needed=%s, pattern_size=%d g_lightbox_invert=%d",
				g_lightbox_needed ? "true" : "false", g_lightbox_pattern_size, g_lightbox_invert);
	} else {
		return HWTCON_STATUS_COPY_FROM_USER_FAIL;
	}
	return 0;
}

/**
 * Algorithm to detect whether a given pixel in inside given region or not.
 * @param region The region to check whether the pixel is inside or not.
 * @param top:Top coordinate of the pixel.
 * @param left:Left coordinate of the pixel.
 * @return True if pixel is inside the region.
 *		 False, otherwise
 */
static inline bool hwtcon_lightbox_is_pixel_inside_region(const struct rect *region, uint32_t top,
		uint32_t left)
{
	return top >= region->y && top < region->y + region->height &&
		left >= region->x && left < region->x + region->width;
}

/**
 * Algorithm to detect whether a given pixel
 * needs to be updated due to the pattern.
 * @param top  Top coordinate of the pixel.
 * @param left: is Left coordinate of the pixel.
 * @return True if the pixel needs to be updated due to pattern.
 * False, otherwise
 */
static inline bool hwtcon_lightbox_is_pixel_affected_by_pattern(uint32_t top, uint32_t left)
{
	/* Currently, the pattern is a size * size black square. */
	return (top / g_lightbox_pattern_size + left / g_lightbox_pattern_size) & 0x1;
}

/**
 * Function to apply lightbox to image buffer.
 * @param task The address of struct hwtcon_task task.
 */
void hwtcon_lightbox_apply_lightbox(struct hwtcon_task *task)
{
	struct rect lightbox_regions[2] = { {0}, {0} };
	struct rect update_region = { 0 };
	u32 rotation = hwtcon_fb_get_rotation();
	u32 top = 0;
	u32 left = 0;
	u32 row = 0;
	u32 col = 0;
	u32 stride = 0;
	u32 offset = 0;
	char pattern_color = hwtcon_fb_get_grayscale() == GRAYSCALE_8BIT_INVERTED ? 0xf0 : 0x00;

	if (g_lightbox_invert)
		pattern_color = pattern_color == 0xf0 ? 0x00 : 0xf0;

	if (!g_lightbox_needed || task->update_data.flags & EPDC_FLAG_USE_ALT_BUFFER)
		return;

	stride = hw_tcon_get_display_width();
	update_region = hwtcon_core_get_task_region(task);
	lightbox_regions[0] = hwtcon_core_rotate_region(g_lightbox_ctrl.region, rotation);
	lightbox_regions[1] = hwtcon_core_rotate_region(g_lightbox_ctrl.region + 1, rotation);

	for (row = 0; row < update_region.height; row++) {
		top = update_region.y + row;
		offset = top * stride;

		for (col = 0; col < update_region.width; col++) {
			left = update_region.x + col;
			if (!hwtcon_lightbox_is_pixel_inside_region(
					lightbox_regions, top, left) &&
				!hwtcon_lightbox_is_pixel_inside_region(
					lightbox_regions + 1, top, left)
				&& hwtcon_lightbox_is_pixel_affected_by_pattern(top, left)) {
				hwtcon_fb_info()->img_buffer_va[offset + left] = pattern_color;
			}
		}
	}
	if (hw_tcon_get_epd_type())
		hwtcon_fb_clean_cache(hwtcon_fb_info()->img_cache_buf);
}
