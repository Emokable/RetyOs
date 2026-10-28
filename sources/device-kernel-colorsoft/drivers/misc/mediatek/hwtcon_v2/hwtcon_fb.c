// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Jiaguang Zhang <jiaguang.zhang@mediatek.com>
 */


#include "hwtcon_fb.h"
#include "hwtcon_def.h"
#include "hwtcon_debug.h"
#include "hwtcon_core.h"
#include "hwtcon_lightbox.h"
#include "fiti_core.h"
#include "hwtcon_wf_lut_config.h"

#include <linux/platform_device.h>
#include <linux/fb.h>
#include <linux/dma-mapping.h>
#include <linux/uaccess.h>
#include <linux/kthread.h>
#include <uapi/linux/sched/types.h>
#include <linux/sched.h>
#include <linux/pm_qos.h>
#include <linux/pm_opp.h>
#include <linux/device.h>
#include <linux/errno.h>
#include "mtk_mdp_path.h"
#include "mtk_vcu.h"
#include <linux/vmalloc.h>
#include "hwtcon_epd.h"
#include "hwtcon_driver.h"
#include "hwtcon_cmdq.h"
#include <linux/mxcfb.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>
#include <linux/kernel.h>


#define FB_ALIGN 16

extern int bd71828_get_batt_temperature(void);

static struct fb_info *g_framebuffer_info;

static int hwtcon_fb_open(struct fb_info *info, int user)
{
	return 0;
}

static int hwtcon_fb_release(struct fb_info *info, int user)
{
	return 0;
}

/* no use */
static int hwtcon_fb_setcolreg(unsigned int regno, unsigned int red,
		unsigned int green,
		unsigned int blue, unsigned int transp, struct fb_info *info)
{
	return 0;
}

/* no use */
static int hwtcon_fb_pan_display_proxy(struct fb_var_screeninfo *var,
	struct fb_info *info)
{
	return 0;
}

/* no use */
static int hwtcon_fb_set_par(struct fb_info *fbi)
{
	TCON_LOG(" hwtcon change other color format info");

	switch (g_framebuffer_info->var.bits_per_pixel) {
	case 8:
		g_framebuffer_info->fix.visual = FB_VISUAL_MONO10;
		break;
	case 16:
	case 24:
	case 32:
		g_framebuffer_info->fix.visual = FB_VISUAL_TRUECOLOR;
		break;
	}
	return 0;
}

u32 hwtcon_fb_get_cur_color_format(void)
{
	u32 format = 0;

	switch (g_framebuffer_info->var.bits_per_pixel) {
	case 8:
		format = V4L2_PIX_FMT_Y8;
		break;
	case 16:
		format = V4L2_PIX_FMT_RGB565;
		break;
	case 24:
		format = V4L2_PIX_FMT_RGB24;
		break;
	case 32:
		format = V4L2_PIX_FMT_ARGB32;
		break;
	default:
		format = V4L2_PIX_FMT_ARGB32;
		break;
	}
	return format;
}

static int hwtcon_fb_check_rotate(struct fb_var_screeninfo *var, struct fb_info *info)
{
	var->rotate = var->rotate % 4;
	switch (var->rotate) {
	case HWTCON_ROTATE_0:
	case HWTCON_ROTATE_180:
		var->xres = hw_tcon_get_display_width();
		var->yres = hw_tcon_get_display_height();
		var->width = hw_tcon_get_edp_area_width();
		var->height = hw_tcon_get_edp_area_height();
		break;
	case HWTCON_ROTATE_90:
	case HWTCON_ROTATE_270:
		var->xres = hw_tcon_get_display_height();
		var->yres = hw_tcon_get_display_width();
		var->width = hw_tcon_get_edp_area_height();
		var->height = hw_tcon_get_edp_area_width();
		break;
	default:
		TCON_ERR("invalid rotate:%d check fail",
			var->rotate);
		return -1;
	}

	var->xres_virtual = var->xres;
	var->yres_virtual = var->yres * FB_VIRTUAL_FRAME_COUNT;

	return 0;
}

static void hwtcon_fb_pixel_format(struct fb_var_screeninfo *var)
{
	switch (var->bits_per_pixel) {
	case 16:
		/*RGB565*/
		var->red =    (struct fb_bitfield){11, 5, 0};
		var->green =  (struct fb_bitfield){5, 6, 0};
		var->blue =   (struct fb_bitfield){0, 5, 0};
		var->transp = (struct fb_bitfield){0, 0, 0};
		break;
	case 24:
		/*RGB*/
		var->red =    (struct fb_bitfield){0, 8, 0};
		var->green =  (struct fb_bitfield){8, 8, 0};
		var->blue =   (struct fb_bitfield){16, 8, 0};
		var->transp = (struct fb_bitfield){0, 0, 0};
		break;
	case 32:
		/*RGBA*/
		var->red =    (struct fb_bitfield){0, 8, 0};
		var->green =  (struct fb_bitfield){8, 8, 0};
		var->blue =   (struct fb_bitfield){16, 8, 0};
		var->transp = (struct fb_bitfield){24, 8, 0};
		break;
	case 8:
	default:
		/*Y8*/
		var->bits_per_pixel = 8;
		var->red =    (struct fb_bitfield){0, 8, 0};
		var->green =  (struct fb_bitfield){0, 8, 0};
		var->blue =   (struct fb_bitfield){0, 8, 0};
		var->transp = (struct fb_bitfield){0, 0, 0};
			break;
	}
}
int hwtcon_fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	int status = 0;
	struct fb_var_screeninfo v;

	if (hwtcon_fb_check_rotate(var, info))
		return -1;
	/* 0 = color, 1 = grayscale, 2 = night mode */
	if (var->grayscale == 0 || var->grayscale == 1 || var->grayscale == 2 || var->grayscale == 3)
		TCON_LOG(" check grayscale success");
	else {
		TCON_ERR(" check grayscale fail");
		return -EINVAL;
	}

	if ((var->grayscale == 0 || var->grayscale == 3) && (!hw_tcon_get_epd_type())) {
		TCON_ERR(" It is a B/W panel, can not be used by color");
		return -EINVAL;
	}

	if (var->bits_per_pixel == 8 || var->bits_per_pixel == 16
		|| var->bits_per_pixel == 24 || var->bits_per_pixel == 32)
		TCON_LOG(" check pixel depth success");
	else {
		TCON_ERR(" check pixel depth fail");
		return -EINVAL;
	}

	v.bits_per_pixel = var->bits_per_pixel;
	/* Get supported pixel format */
	hwtcon_fb_pixel_format(&v);

	if (v.bits_per_pixel != var->bits_per_pixel) {
		TCON_ERR(" unsupported bits_per_pixel");
		return -EINVAL;
	} else if (memcmp(&v.red, &var->red, sizeof(var->red)) ||
			memcmp(&v.green, &var->green, sizeof(var->green)) ||
			memcmp(&v.blue, &var->blue, sizeof(var->blue)) ||
			memcmp(&v.transp, &var->transp, sizeof(var->transp))) {
		TCON_ERR(
			" unsupported color format: red=(%d %d %d), green=(%d %d %d), blue=(%d %d %d), transp=(%d %d %d)",
			var->red.offset, var->red.length, var->red.msb_right,
			var->green.offset, var->green.length, var->green.msb_right,
			var->blue.offset, var->blue.length, var->blue.msb_right,
			var->transp.offset, var->transp.length, var->transp.msb_right);
		return -EINVAL;
	}

	info->fix.line_length =
		hw_tcon_get_display_width() * info->var.bits_per_pixel / 8;


	/* wait all task done before call fb_set_par to change color format*/
	status = hwtcon_core_wait_all_task_done();
	if (status)
		TCON_ERR(" fail to wait hw idle before change fb");
	return 0;
}

void hwtcon_fb_flush_update(void)
{
	mutex_lock(&hwtcon_fb_info()->update_queue_mutex);

	/* hwtcon busy,need to wait it finish update */
	hwtcon_core_wait_all_task_done();

	mutex_unlock(&hwtcon_fb_info()->update_queue_mutex);
}

static int hwtcon_fb_blank(int blank_mode, struct fb_info *info)
{
	TCON_LOG("fb blank mode:%d", blank_mode);
	if (hwtcon_fb_info()->blank == blank_mode)
		return 0;

	switch (blank_mode) {
	case FB_BLANK_UNBLANK:
		/* resume */
		hwtcon_fb_info()->blank = blank_mode;
		break;
	case FB_BLANK_NORMAL:
	case FB_BLANK_VSYNC_SUSPEND:
	case FB_BLANK_HSYNC_SUSPEND:
		/* Only flush the pending updates in current HWTCON list */
		hwtcon_fb_flush_update();
		break;
	case FB_BLANK_POWERDOWN:
		hwtcon_fb_flush_update();
		if (hwtcon_fb_info()->mmsys_power_enable == true &&
			hwtcon_fb_info()->power_down_delay_ms == EINK_DEFAULT_POWER_DOWN_TIME) {
			/* no power off mode, force to power off here */
			hwtcon_driver_enable_mmsys_power(false);
		} else if (hwtcon_fb_info()->mmsys_power_enable == true) {
			/* power on, but need wait for power off finished.
			 * if timeout force to power off here
			 */
			hwtcon_core_wait_power_down();
		}
		hwtcon_fb_info()->blank = blank_mode;
		/* suspend */
		break;
	default:
		TCON_ERR("invalid blank_mode:%d", blank_mode);
		return -EINVAL;
	}

	return 0;
}

static int hwtcon_core_update_task_wf_mode(
	struct mxcfb_update_data *update_data)
{
	enum WAVEFORM_MODE_ENUM wf_mode = update_data->waveform_mode;
	bool full_update = (update_data->update_mode == UPDATE_MODE_FULL);

	if (hwtcon_core_use_night_mode()) {
		switch (wf_mode) {
		case WAVEFORM_MODE_GC16:
			wf_mode = WAVEFORM_MODE_GCK16;
			break;
		case WAVEFORM_MODE_GLR16:
			wf_mode = WAVEFORM_MODE_GLKW16;
			break;
		case WAVEFORM_MODE_GCC16:
			wf_mode = WAVEFORM_MODE_GCCK16;
			break;
		case WAVEFORM_MODE_GLRC16:
			wf_mode = WAVEFORM_MODE_GLRCK16;
			break;
		case WAVEFORM_MODE_P2SW:
			if (hw_tcon_get_epd_type())
				wf_mode = WAVEFORM_MODE_GLRCK16;
			else
				wf_mode = WAVEFORM_MODE_GLKW16;
			TCON_ERR("p2sw id invalid mode, use mode %d instead", wf_mode);
			break;
		case WAVEFORM_MODE_DU:
			if ((wf_lut_get_wf_info()->mode_version == WF_MODE_VERSION_TL
				|| wf_lut_get_wf_info()->mode_version == WF_MODE_VERSION_JB
				|| wf_lut_get_wf_info()->mode_version == WF_MODE_VERSION_TEST))
				wf_mode = WAVEFORM_MODE_DUNM;
			break;
		default:
			break;
		}
	} else {
		switch (wf_mode) {
		case WAVEFORM_MODE_GCK16:
			wf_mode = WAVEFORM_MODE_GC16;
			break;
		case WAVEFORM_MODE_GLKW16:
			wf_mode = WAVEFORM_MODE_GLR16;
			break;
		case WAVEFORM_MODE_GCCK16:
			wf_mode = WAVEFORM_MODE_GCC16;
			break;
		case WAVEFORM_MODE_GLRCK16:
			wf_mode = WAVEFORM_MODE_GLRC16;
			break;
		case WAVEFORM_MODE_DUNM:
			wf_mode = WAVEFORM_MODE_DU;
			break;
		default:
			break;
		}
	}

	if (!hw_tcon_get_epd_type()) {
		switch (wf_mode) {
		case WAVEFORM_MODE_GCC16:
			wf_mode = WAVEFORM_MODE_GC16;
			break;
		case WAVEFORM_MODE_GLRC16:
			wf_mode = WAVEFORM_MODE_GLR16;
			break;
		case WAVEFORM_MODE_GCCK16:
			wf_mode = WAVEFORM_MODE_GCK16;
			break;
		case WAVEFORM_MODE_GLRCK16:
			wf_mode = WAVEFORM_MODE_GLKW16;
			break;
		default:
			break;
		}
	}
	/* update partial update mode */
	if (wf_mode == WAVEFORM_MODE_GC16 && !full_update)
		wf_mode = WAVEFORM_MODE_GC16_PARTIAL;
	if (wf_mode == WAVEFORM_MODE_GCK16 && !full_update)
		wf_mode = WAVEFORM_MODE_GCK16_PARTIAL;

	switch (wf_mode) {
	case WAVEFORM_MODE_INIT:
	case WAVEFORM_MODE_DU:
	case WAVEFORM_MODE_GC16:
	case WAVEFORM_MODE_GL16:
	case WAVEFORM_MODE_GLR16:
	case WAVEFORM_MODE_GLD16:
	case WAVEFORM_MODE_A2:
	case WAVEFORM_MODE_DU4:
	case WAVEFORM_MODE_GCK16:
	case WAVEFORM_MODE_GLKW16:
	case WAVEFORM_MODE_GC16_PARTIAL:
	case WAVEFORM_MODE_GCK16_PARTIAL:
	case WAVEFORM_MODE_DUNM:
	case WAVEFORM_MODE_P2SW:
	case WAVEFORM_MODE_GCC16:
	case WAVEFORM_MODE_GLRC16:
	case WAVEFORM_MODE_GCCK16:
	case WAVEFORM_MODE_GLRCK16:
	case WAVEFORM_MODE_AUTO:
		break;
	default:
		TCON_ERR("invalid waveform mode:%d->%s",
			wf_mode,
			hwtcon_core_get_wf_mode_name(wf_mode));
		return -1;
	}
	update_data->waveform_mode = wf_mode;
	return 0;
}

int hwtcon_fb_get_width(void)
{
	return g_framebuffer_info->var.xres;
}
EXPORT_SYMBOL(hwtcon_fb_get_width);

int hwtcon_fb_get_height(void)
{
	return g_framebuffer_info->var.yres;
}
EXPORT_SYMBOL(hwtcon_fb_get_height);

int hwtcon_fb_get_virtual_width(void)
{
	return g_framebuffer_info->var.xres_virtual;
}
EXPORT_SYMBOL(hwtcon_fb_get_virtual_width);

int hwtcon_fb_check_update_data_invalid(
	struct mxcfb_update_data *update_data)
{
	struct rect panel_region = {0, 0,
		hw_tcon_get_display_width(),
		hw_tcon_get_display_height()
	};
	struct rect task_region = { 0 };

	task_region = hwtcon_core_get_update_data_region(update_data);

	if (update_data->update_mode != UPDATE_MODE_FULL &&
		update_data->update_mode != UPDATE_MODE_PARTIAL) {
		TCON_ERR("invalid update_mode:%d", update_data->update_mode);
		return HWTCON_STATUS_INVALID_PARAM;
	}

	if (!hwtcon_rect_contain_00(&panel_region, &task_region)) {
		TCON_ERR("invalid region[%d %d %d %d] panel[%d %d] rot:%d",
			task_region.x,
			task_region.y,
			task_region.width,
			task_region.height,
			hw_tcon_get_display_width(),
			hw_tcon_get_display_height(),
			hwtcon_fb_get_rotation());
		return HWTCON_STATUS_INVALID_PARAM;
	}

	if (hwtcon_core_update_task_wf_mode(update_data) != 0) {
		TCON_ERR("invalid waveform_mode:%d update_mode:%d",
			update_data->waveform_mode,
			update_data->update_mode);
		return HWTCON_STATUS_INVALID_PARAM;
	}

	if (update_data->flags & EPDC_FLAG_ENABLE_SWIPE) {
		if (wf_lut_get_wf_info()->mode_version != WF_MODE_VERSION_TL
			&& wf_lut_get_wf_info()->mode_version != WF_MODE_VERSION_JB
			&& wf_lut_get_wf_info()->mode_version != WF_MODE_VERSION_TEST) {
			TCON_LOG("wavefrom file not support swipe");
			update_data->flags &= ~EPDC_FLAG_ENABLE_SWIPE;
		}

		if ((update_data->swipe_data.direction >= SWIPE_MAX) ||
			(update_data->swipe_data.steps == 0) ||
			(update_data->swipe_data.steps > MAX_SWIPE_COUNT)) {
			TCON_ERR("invalid swipe setting: flags[0x%08x] dir[%d] steps[%d]",
				update_data->flags,
				update_data->swipe_data.direction,
				update_data->swipe_data.steps);
			return HWTCON_STATUS_INVALID_PARAM;
		}
	}

	return 0;
}

static int hwtcon_fb_ioctl_set_temperature(void *arg)
{
	int value = 0;

	if (copy_from_user(&value, arg, sizeof(value)) != 0) {
		TCON_ERR("copy_from_user fail");
		return HWTCON_STATUS_COPY_FROM_USER_FAIL;
	}

	TCON_LOG("set temperature:%d", value);
	hwtcon_fb_info()->temperature = value;

	return 0;
}

static int hwtcon_fb_ioctl_get_temperature(void *arg)
{
	int value = hwtcon_core_read_temperature();

	if (copy_to_user((void *)arg, &value, sizeof(value)) != 0) {
		TCON_ERR("copy_to_user fail");
		return HWTCON_STATUS_COPY_TO_USER_FAIL;
	}

	return 0;
}

static int hwtcon_fb_ioctl_send_update(void *arg)
{
	int status = 0;
	struct mxcfb_update_data update_data;
	struct update_hist *hist_ptr;
	int next;

	if (hwtcon_fb_info()->ignore_request || hwtcon_fb_info()->blank != FB_BLANK_UNBLANK) {
		TCON_LOG("ignore request");
		return 0;
	}

	memset(&update_data, 0, sizeof(update_data));
	if (copy_from_user(&update_data, (void *)arg,
		sizeof(update_data)) != 0) {
		TCON_ERR("copy_from_user fail");
		return HWTCON_STATUS_COPY_FROM_USER_FAIL;
	}

	status = hwtcon_fb_check_update_data_invalid(&update_data);
	if (status != 0)
		return status;

	status = hwtcon_core_submit_task(&update_data);

	if (hwtcon_fb_info()->tchbst_opt_enabled) {
		/* save the update info for page turn check later */
		hist_ptr = &hwtcon_fb_info()->hist;
		next = (hist_ptr->index + 1) % MAX_INPUT_INDEX;
		hist_ptr->update[next].jiffies = jiffies;
		pr_debug("cur jiffies %lu, prev jiffies %lu\n",
			hist_ptr->update[next].jiffies,
			hist_ptr->update[hist_ptr->index].jiffies);
		hist_ptr->update[next].width = update_data.update_region.width;
		hist_ptr->update[next].height = update_data.update_region.height;
		if (hist_ptr->update[next].jiffies > hist_ptr->update[hist_ptr->index].jiffies) {
			hist_ptr->update[next].time_gap = hist_ptr->update[next].jiffies -
				hist_ptr->update[hist_ptr->index].jiffies;
			if (hist_ptr->update[next].width == g_framebuffer_info->var.xres &&
				hist_ptr->update[next].height == g_framebuffer_info->var.yres &&
				hist_ptr->update[hist_ptr->index].width == g_framebuffer_info->var.xres &&
				hist_ptr->update[hist_ptr->index].height == g_framebuffer_info->var.yres) {
				hist_ptr->update[next].time_gap += hist_ptr->update[hist_ptr->index].time_gap;
			}
		}
		else {
			hist_ptr->update[next].time_gap = 0;
		}
		hist_ptr->index = next;
	}
	return status;
}

static int hwtcon_fb_ioctl_set_update_scheme(void *arg)
{
	u32 scheme = 0;

	if (copy_from_user(&scheme, arg, sizeof(scheme)) != 0) {
		TCON_ERR("copy_from_user fail");
		return HWTCON_STATUS_COPY_FROM_USER_FAIL;
	}

	/* check param valid. */
	if ((scheme != UPDATE_SCHEME_QUEUE) &&
		(scheme != UPDATE_SCHEME_QUEUE_AND_MERGE)) {
		TCON_ERR("invalid param: %d", scheme);
		return HWTCON_STATUS_INVALID_UPDATE_SCHEME;
	}

	hwtcon_fb_info()->update_scheme = scheme;
	return 0;
}

static int hwtcon_fb_ioctl_wait_for_task_triggered(struct fb_info *info, void *arg)
{
	u32 update_marker = 0;
	int ret;

	if (copy_from_user(&update_marker, arg, sizeof(u32)) != 0) {
		TCON_ERR("copy_from_user fail");
		return HWTCON_STATUS_COPY_FROM_USER_FAIL;
	}
	/*unlock to allow other concurrent IOCTLS */
	/* it is locked before this function is called,
	 * see fbmem.c function do_fb_ioctl case default.
	 */
	unlock_fb_info(info);
	ret = hwtcon_core_wait_for_task_triggered(update_marker);
	/*re-lock it before exit, it will unlock in do_fb_ioctl case default */
	lock_fb_info(info);
	return ret;
}

static int hwtcon_fb_ioctl_wait_for_mdp_completion(struct fb_info *info, void *arg)
{
	u32 update_marker = 0;
	int ret;

	if (copy_from_user(&update_marker, arg, sizeof(u32)) != 0) {
		TCON_ERR("copy_from_user fail");
		return HWTCON_STATUS_COPY_FROM_USER_FAIL;
	}

	/*unlock to allow other concurrent IOCTLS */
	unlock_fb_info(info);

	TCON_LOG("kernel mdp_completion ioctl waiting for marker:%d ", update_marker);
	ret = hwtcon_core_wait_for_mdp_triggered(update_marker);
	TCON_LOG("kernel mdp_completion ioctl wait complete for marker:%d ", update_marker);

	/*re-lock it before exit, it will unlock in do_fb_ioctl case default */
	lock_fb_info(info);

	return ret;
}

static int hwtcon_fb_ioctl_wait_for_task_displayed(struct fb_info *info, void *arg)
{
	u32 update_marker = 0;
	int ret;

	if (copy_from_user(&update_marker, arg, sizeof(u32)) != 0) {
		TCON_ERR("copy_from_user fail");
		return HWTCON_STATUS_COPY_FROM_USER_FAIL;
	}
	/*unlock to allow other concurrent IOCTLS */
	/* it is locked before this function is called,
	 * see fbmem.c function do_fb_ioctl case default.
	 */
	unlock_fb_info(info);
	ret = hwtcon_core_wait_for_task_displayed(update_marker);
	/*re-lock it before exit, it will unlock in do_fb_ioctl case default */
	lock_fb_info(info);
	TCON_LOG("kernel ioctl wait complete for marker:%d ", update_marker);
	return ret;
}

static int hwtcon_fb_ioctl_get_working_buffer(void *arg)
{
	if (copy_to_user(arg, hwtcon_fb_info()->wb_buffer_va,
			 hwtcon_fb_info()->wb_buffer_size) != 0) {
		TCON_ERR("copy_to_user fail");
		return HWTCON_STATUS_COPY_TO_USER_FAIL;
	}
	return 0;
}

static int hwtcon_fb_ioctl_get_waveform_type(void *arg)
{
	u32 wf_type = 0;

	wf_type = hwtcon_core_get_waveform_type();

	if (copy_to_user(arg, &wf_type, sizeof(wf_type)) != 0) {
		TCON_ERR("copy to user fail");
		return HWTCON_STATUS_COPY_TO_USER_FAIL;
	}

	return 0;
}

static int hwtcon_fb_ioctl_get_material_type(void *arg)
{
	u32 material_type = 0;

	material_type = hw_tcon_get_edp_material_type();

	if (copy_to_user(arg, &material_type, sizeof(material_type)) != 0) {
		TCON_ERR("copy to user fail");
		return HWTCON_STATUS_COPY_TO_USER_FAIL;
	}

	return 0;
}


static int hwtcon_fb_ioctl_set_night_mode(void *arg)
{
	struct mxcfb_nightmode_ctrl night_mode_info;

	if (copy_from_user(&night_mode_info, arg,
		sizeof(night_mode_info)) != 0) {
		TCON_ERR("copy from user fail");
		return HWTCON_STATUS_COPY_FROM_USER_FAIL;
	}
	/* hwtcon_fb_info()->enable_night_mode = !night_mode_info.disable; */
	return 0;
}

static int hwtcon_fb_ioctl_set_power_down_delay_time(void *arg)
{
	int delay_time = 0;

	if (copy_from_user(&delay_time, arg, sizeof(delay_time)) != 0) {
		TCON_ERR("copy from user fail");
		return HWTCON_STATUS_COPY_FROM_USER_FAIL;
	}

	/* check param valid */
	if (delay_time < 0) {
		TCON_ERR("invalid delay time:%d", delay_time);
		return HWTCON_STATUS_INVALID_PARAM;
	}

	hwtcon_fb_info()->power_down_delay_ms = delay_time;
	return 0;
}

static int hwtcon_fb_ioctl_get_power_down_delay_time(void *arg)
{
	int delay_time = hwtcon_fb_info()->power_down_delay_ms;

	if (copy_to_user(arg, &delay_time, sizeof(delay_time)) != 0) {
		TCON_ERR("invalid copy to user");
		return HWTCON_STATUS_COPY_TO_USER_FAIL;
	}

	return 0;
}

static int hwtcon_fb_ioctl_set_pause(void *arg)
{
	hwtcon_fb_info()->ignore_request = true;
	return 0;
}

static int hwtcon_fb_ioctl_set_resume(void *arg)
{
	hwtcon_fb_info()->ignore_request = false;
	return 0;
}

static int hwtcon_fb_ioctl_get_pause(void *arg)
{
	u32 ignore_request = hwtcon_fb_info()->ignore_request;

	if (copy_to_user(arg, &ignore_request, sizeof(ignore_request)) != 0) {
		TCON_ERR("copy to user fail");
		return HWTCON_STATUS_COPY_TO_USER_FAIL;
	}

	return 0;
}

static int hwtcon_fb_ioctl_get_panel_info(void *arg)
{
	struct mxcfb_panel_info info;

	memset(&info, 0, sizeof(info));
	info.temp = hwtcon_core_read_temperature();
	info.temp_zone = hwtcon_core_read_temp_zone();
#ifndef FPGA_EARLY_PORTING
	/* TODO */
	info.vcom_value = fiti_read_vcom();
#endif
	snprintf(info.wf_file_name, sizeof(info.wf_file_name), "%s",
		 wf_lut_get_wf_info()->wf_file_name);

	if (copy_to_user(arg, &info, sizeof(info)) != 0) {
		TCON_ERR("copy to user fail");
		return HWTCON_STATUS_COPY_TO_USER_FAIL;
	}
	return 0;
}

static int hwtcon_fb_ioctl_set_stylus_mode(void *arg)
{
	int stylus_mode = 0;

	if (copy_from_user(&stylus_mode, arg, sizeof(stylus_mode)) != 0) {
		TCON_ERR("%s: copy from user fail", __func__);
		return HWTCON_STATUS_COPY_FROM_USER_FAIL;
	}
	switch (stylus_mode) {
	#ifdef STYLUS
	case EPDC_STYLUS_MODE_DISABLED:
		hwtcon_core_disable_stylus_mode();
		break;
	case EPDC_STYLUS_MODE_WITH_NO_TPS:
		hwtcon_core_enable_stylus_mode();
		break;
	case EPDC_STYLUS_MODE_WITH_BLACK_TPS:
	case EPDC_STYLUS_MODE_WITH_WHITE_TPS:
	#endif
	default:
		TCON_ERR("Invalid stylus mode");
		break;
	}
	return 0;
}

static int hwtcon_fb_ioctl(struct fb_info *info, unsigned int cmd,
	unsigned long arg)
{
	switch (cmd) {
	case MXCFB_SET_WAVEFORM_MODES:
		break;
	case MXCFB_SET_TEMPERATURE:
		return hwtcon_fb_ioctl_set_temperature((void *)arg);
	case MXCFB_GET_TEMPERATURE:
		return hwtcon_fb_ioctl_get_temperature((void *)arg);
	case MXCFB_SEND_UPDATE:
		return hwtcon_fb_ioctl_send_update((void *)arg);
	case MXCFB_SET_HALFTONE:
		return hwtcon_lightbox_ioctl_set_lightbox_ctrl((void *)arg);
	case MXCFB_SET_STYLUS_MODE:
		return hwtcon_fb_ioctl_set_stylus_mode((void *)arg);
	case MXCFB_SET_UPDATE_SCHEME:
		return hwtcon_fb_ioctl_set_update_scheme((void *)arg);
	case MXCFB_WAIT_FOR_UPDATE_SUBMISSION:
		return hwtcon_fb_ioctl_wait_for_task_triggered(info, (void *)arg);
	case MXCFB_WAIT_FOR_MDP_COMPLETION:
		return hwtcon_fb_ioctl_wait_for_mdp_completion(info, (void *)arg);
	case MXCFB_WAIT_FOR_UPDATE_COMPLETE:
		return hwtcon_fb_ioctl_wait_for_task_displayed(info, (void *)arg);
	case MXCFB_GET_WORK_BUFFER:
		return hwtcon_fb_ioctl_get_working_buffer((void *)arg);
	case MXCFB_GET_WAVEFORM_TYPE:
		return hwtcon_fb_ioctl_get_waveform_type((void *)arg);
	case MXCFB_GET_MATERIAL_TYPE:
		return hwtcon_fb_ioctl_get_material_type((void *)arg);
	case MXCFB_SET_NIGHTMODE:
		return hwtcon_fb_ioctl_set_night_mode((void *)arg);
	case MXCFB_SET_PWRDOWN_DELAY:
		return hwtcon_fb_ioctl_set_power_down_delay_time((void *)arg);
	case MXCFB_GET_PWRDOWN_DELAY:
		return hwtcon_fb_ioctl_get_power_down_delay_time((void *)arg);
	case MXCFB_SET_PAUSE:
		return hwtcon_fb_ioctl_set_pause((void *)arg);
	case MXCFB_SET_RESUME:
		return hwtcon_fb_ioctl_set_resume((void *)arg);
	case MXCFB_GET_PAUSE:
		return hwtcon_fb_ioctl_get_pause((void *)arg);
	case MXCFB_GET_PANEL_INFO:
		return hwtcon_fb_ioctl_get_panel_info((void *)arg);
	case MXCFB_SET_AUTO_UPDATE_MODE:
		return 0;
	case MXCFB_WAIT_FOR_ANY_UPDATE_COMPLETE:
		return hwtcon_fb_ioctl_wait_for_any_update_complete(info, arg);
	case MXCFB_SET_UPDATE_FLAGS:
		return hwtcon_fb_ioctl_set_update_flags(info, arg);
	case MXCFB_GET_UPDATE_FLAGS:
		return hwtcon_fb_ioctl_get_update_flags(info, arg);
	default:
		TCON_ERR("err cmd:0x%08x dir:0x%x type:0x%x nr:0x%x size:0x%x",
			cmd,
			_IOC_DIR(cmd),
			_IOC_TYPE(cmd),
			_IOC_NR(cmd),
			_IOC_SIZE(cmd));
		return HWTCON_STATUS_INVALID_IOCTL_CMD;
	}

	TCON_ERR("ioctl cmd:0x%x not implement", cmd);
	return 0;
}

#ifdef CONFIG_COMPAT
static int hwtcon_fb_compat_ioctl(struct fb_info *info, unsigned int cmd,
	unsigned long arg)
{
	return hwtcon_fb_ioctl(info, cmd, arg);
}
#endif

int hwtcon_mmap_cacheble(struct sg_table *table, struct vm_area_struct *vma)
{
	int status = 0;

	if (hw_tcon_get_epd_type()) {
		/* vmalloc buffer */
		unsigned long addr = 0L;
		unsigned long offset = 0L;
		struct scatterlist *sg = NULL;
		int i;

		addr = vma->vm_start;
		offset = vma->vm_pgoff * PAGE_SIZE;
		for_each_sg(table->sgl, sg, table->nents, i) {
			struct page *page = sg_page(sg);
			unsigned long remainder = vma->vm_end - addr;
			unsigned long len = sg->length;

			if (offset >= sg->length) {
				offset -= sg->length;
				continue;
			} else if (offset) {
				page += offset / PAGE_SIZE;
				len = sg->length - offset;
				offset = 0;
			}
			len = min(len, remainder);
			status = remap_pfn_range(vma, addr, page_to_pfn(page), len,
					vma->vm_page_prot);
			if (status) {
				TCON_ERR("%s remap fail 0x%p, %lu, %lu, %lu, %d.\n",
					   __func__, vma, addr,
					   page_to_pfn(page), len, status);
				return status;
			}
			addr += len;
			if (addr >= vma->vm_end)
				return 0;
		}
	}
	return status;
}


/* frame buffer already have mmap implement,
 * this function can be removed.
 */
static int hwtcon_fb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	int status = 0;

	if (hw_tcon_get_epd_type()) {
		status =
			hwtcon_mmap_cacheble(
			((struct fb_private_info *)(info->par))->fb_cache_buf->sgt, vma);
	} else {
		status = dma_mmap_attrs(((struct fb_private_info *)(info->par))->dev,
			vma,
			((struct fb_private_info *)(info->par))->fb_buffer_va,
			((struct fb_private_info *)(info->par))->fb_buffer_pa,
			((struct fb_private_info *)(info->par))->fb_buffer_size,
			0);
		if (status != 0)
			TCON_ERR("mmap fail: status:%d size:%d",
			status,
			((struct fb_private_info *)
			(info->par))->fb_buffer_size);
	}
	return status;
}

int hwtcon_image_mmap(struct file *flip, struct vm_area_struct *vma)
{
	return hwtcon_mmap_cacheble(
		hwtcon_fb_info()->img_cache_buf->sgt, vma);
}

int hwtcon_pre_image_mmap(struct file *flip, struct vm_area_struct *vma)
{
	return hwtcon_mmap_cacheble(
		hwtcon_fb_info()->pre_img_cache_buf->sgt, vma);
}

int hwtcon_tmpbuf_mmap(struct file *flip, struct vm_area_struct *vma)
{
	return hwtcon_mmap_cacheble(
		hwtcon_fb_info()->temp_img_cache_buf->sgt, vma);
}

int hwtcon_regal_buf2_mmap(struct file *flip, struct vm_area_struct *vma)
{
	return hwtcon_mmap_cacheble(
		hwtcon_fb_info()->partial_regal_cache_buf2->sgt, vma);
}
static int hwtcon_fb_setcmap(struct fb_cmap *cmap, struct fb_info *info)
{
	/* color map */
	mdp_update_cmap_lut(cmap->red, cmap->len);
	return 0;
}

static struct fb_ops hwtcon_fb_ops = {
	.owner = THIS_MODULE,
	.fb_open = hwtcon_fb_open,
	.fb_release = hwtcon_fb_release,
	.fb_setcolreg = hwtcon_fb_setcolreg,
	.fb_pan_display = hwtcon_fb_pan_display_proxy,
	.fb_check_var = hwtcon_fb_check_var,
	.fb_blank = hwtcon_fb_blank,
#if 0
	.fb_fillrect = cfb_fillrect,
	.fb_copyarea = cfb_copyarea,
	.fb_imageblit = cfb_imageblit,
#endif
	.fb_set_par = hwtcon_fb_set_par,
	.fb_ioctl = hwtcon_fb_ioctl,
#ifdef CONFIG_COMPAT
	.fb_compat_ioctl = hwtcon_fb_compat_ioctl,
#endif
	.fb_mmap = hwtcon_fb_mmap,
	.fb_setcmap = hwtcon_fb_setcmap,

};

int hwtcon_core_clear_wb(void *ignore)
{
	while (1) {
		wait_event_interruptible(hwtcon_fb_info()->clear_wb_wait_queue,
			hwtcon_fb_info()->wb_clearing == true);
		if (hwtcon_hal_bit_set_cnt(hwtcon_core_get_released_lut()) >
			hwtcon_fb_info()->max_release_lut_count) {
			pipeline_init_working_buffer_ll();
		}
		hwtcon_fb_info()->wb_clearing = false;
	}

	return 0;
}

static struct task_struct *pThread1;
static struct task_struct *pThread2;
static struct task_struct *pThread3;
static int hwtcon_fb_kthread_create(void)
{
	struct sched_param param = {.sched_priority = 2};

	if (pThread1 == NULL) {
		pThread1 = kthread_run(hwtcon_core_dispatch_pipeline, NULL,
			"dispatch_pipeline");
		if (IS_ERR(pThread1)) {
			TCON_ERR("create thread dispatch_pipeline failed");
			return HWTCON_STATUS_CREAT_THREAD_FAIL;
		}

		/* adjust thread priority. */
		sched_setscheduler(pThread1, SCHED_FIFO, &param);
	}

	if (pThread2 == NULL) {
		pThread2 = kthread_run(hwtcon_core_dispatch_mdp, NULL,
			"dispatch_mdp");
		if (IS_ERR(pThread2)) {
			TCON_ERR("create thread hwtcon_dispatch_mdp failed");
			return HWTCON_STATUS_CREAT_THREAD_FAIL;
		}
		/* adjust thread priority. */
		sched_setscheduler(pThread2, SCHED_FIFO, &param);
	}

	if (pThread3 == NULL) {
		pThread3 = kthread_run(hwtcon_core_clear_wb, NULL,
			"clear wb");
		if (IS_ERR(pThread3)) {
			TCON_ERR("create thread hwtcon_core_clear_wb failed");
			return HWTCON_STATUS_CREAT_THREAD_FAIL;
		}
		/* adjust thread priority. */
		sched_setscheduler(pThread3, SCHED_RR, &param);
	}
#if 0
	/* hwtcon_core_start_lut_assign_done_trigger_loop(); */
#endif

	return 0;
}

static int hwtcon_fb_kthread_destroy(void)
{
#if 0
	if (pThread1)
		kthread_stop(pThread1);
	if (pThread2)
		kthread_stop(pThread2);
#endif
	return 0;
}


#ifdef CONFIG_PMIC_FITI_V2
void hwtcon_fb_read_temperature_from_sensor(struct work_struct *workItem)
{
	unsigned int interval = SENSOR_READ_INTERVAL_MS;
	int temperature;
	bool valid = true;
	static int count = 0;

	if (hwtcon_debug_get_info()->fixed_temperature != TEMP_USE_AMBIENT) {
		temperature = hwtcon_debug_get_info()->fixed_temperature;
		TCON_LOG("read fixed temperature[%d]", temperature);
	}
	else {
		valid = fiti_read_temperature(&temperature) > 0;
		if (temperature == 0) {
			valid = false;
			count++;
			if (count >= MAX_TEMP_READ) {
				TCON_ERR("sensor temperature is 0 in %d consecutive reads!", count);
				/* read the backup temp sensor and take the average of that temperature and
				 * the previous reading*/
				temperature = bd71828_get_batt_temperature();
				if (temperature != -255) {
					temperature = (temperature / 10 + hwtcon_fb_info()->temperature) / 2;
					count = 0;
					valid = true;
				}
				else {
					TCON_ERR("error in reading battery temperature!");
				}
			}
		}

		if (valid) {
			TCON_LOG("read sensor temperature[%d]", temperature);
		}
		else {
			TCON_ERR("fail to read sensor temperature");
			interval = SENSOR_READ_SHORT_INTERVAL_MS;
		}
	}

	if (valid && temperature != hwtcon_fb_info()->temperature) {
		hwtcon_core_load_init_setting_from_file();
		if (fiti_update_sv(hwtcon_core_convert_temperature(temperature)) < 0)
			interval = SENSOR_READ_SHORT_INTERVAL_MS;
		/* update the EPD temperature only if all SVs are updated successfully */
		else
			hwtcon_fb_info()->temperature = temperature;
	}

	schedule_delayed_work(&hwtcon_fb_info()->read_sensor_work,
			msecs_to_jiffies(interval));
}

#else
void hwtcon_fb_read_temperature_from_sensor(struct work_struct *workItem)
{
	if (!fiti_pmic_judge_power_on_going()) {
		/* Do not use fiti when fiti is in POWER_ON_GOING state */
		hwtcon_fb_info()->temperature = fiti_read_temperature();
		TCON_LOG("read sensor temperature[%d]",
			hwtcon_fb_info()->temperature);
		}
	schedule_delayed_work(&hwtcon_fb_info()->read_sensor_work,
		msecs_to_jiffies(SENSOR_READ_INTERVAL_MS));
	}
#endif

static int hwtcon_fb_init_fb_info(struct fb_info *info)
{
	info->fbops = &hwtcon_fb_ops;
	info->flags = FBINFO_FLAG_DEFAULT;
	info->screen_buffer = ((struct fb_private_info *)
			(info->par))->fb_buffer_va;
	info->screen_size = ((struct fb_private_info *)
			(info->par))->fb_buffer_size;
	info->pseudo_palette = ((struct fb_private_info *)
				(info->par))->pseudo_palette;

	/* fb_fix_screeninfo */
	strncpy(info->fix.id, HWTCON_DRIVER_NAME, sizeof(info->fix.id));
	info->fix.smem_start = ((struct fb_private_info *)
				(info->par))->fb_buffer_pa;
	info->fix.smem_len = ((struct fb_private_info *)
			(info->par))->fb_buffer_size;
	info->fix.type = FB_TYPE_PACKED_PIXELS;
	if (hwtcon_device_info()->color_format == 1) {
		info->fix.visual = FB_VISUAL_MONO10;
		info->var.grayscale = 1; /* 0 = color, 1 = grayscale, */
		info->var.nonstd = 0; /* non standard pixel format */
	} else {
		info->fix.visual = FB_VISUAL_TRUECOLOR;
		info->var.grayscale = 0; /* 0 = color, 1 = grayscale, */
		info->var.nonstd = 0; /* non standard pixel format */
	}
	info->fix.xpanstep = 0;
	info->fix.ypanstep = 0;
	info->fix.ywrapstep = 0;
	info->fix.line_length =
		hw_tcon_get_display_width() * hwtcon_device_info()->color_format;

	info->fix.accel = FB_ACCEL_NONE;

	/* fb_var_screeninfo */
	info->var.rotate = hw_tcon_get_edp_rotate();
	hwtcon_fb_check_rotate(&info->var, info); /* init xres yres*/

	info->var.left_margin = hw_tcon_get_wb_buffer_start_x();
	info->var.right_margin = hw_tcon_get_wb_buffer_start_x();
	info->var.upper_margin = hw_tcon_get_wb_buffer_start_y();
	info->var.lower_margin = hw_tcon_get_wb_buffer_start_y();

	info->var.pixclock = hw_tcon_get_edp_clk() / 2;
	info->var.hsync_len = hw_tcon_get_edp_blank_hsa();
	info->var.vsync_len = hw_tcon_get_edp_blank_vsa();

	info->var.bits_per_pixel = hwtcon_device_info()->color_format << 3;
	hwtcon_fb_pixel_format(&info->var);

	info->var.xoffset = 0;
	info->var.yoffset = 0;
	info->var.activate = FB_ACTIVATE_NOW;

	info->fix.line_length = info->var.xres_virtual * info->var.bits_per_pixel >> 3;

	return 0;
}

int hwtcon_fb_alloc_debug_img_buffer(struct fb_private_info *private_info)
{
	int i = 0;
	int buffer_size = hw_tcon_get_display_width() * hw_tcon_get_display_height();
	int img_buf_count = private_info->debug_img_buffer_count;

	if (private_info->debug_img_buffer_available)
		return HWTCON_STATUS_OK;

	TCON_WARN("Enabling debug img buffer, %d MB additional memory will be used.",
		(img_buf_count * buffer_size) >> 20);

	do {
		private_info->debug_img_buffer_size =
			(size_t *)kcalloc(img_buf_count, sizeof(size_t), GFP_KERNEL);
		if (private_info->debug_img_buffer_size == NULL) {
			TCON_ERR("fail to allocate debug_img_buffer_size");
			break;
		}

		private_info->debug_img_buffer_va =
			(char **)kcalloc(img_buf_count, sizeof(char *), GFP_KERNEL);
		if (private_info->debug_img_buffer_va == NULL) {
			TCON_ERR("fail to allocate debug_img_buffer_va");
			break;
		}

		private_info->debug_img_buffer_pa =
			(dma_addr_t *)kcalloc(img_buf_count, sizeof(dma_addr_t), GFP_KERNEL);
		if (private_info->debug_img_buffer_pa == NULL) {
			TCON_ERR("fail to allocate debug_img_buffer_pa");
			break;
		}

		private_info->debug_img_buffer_name =
			(char **)kcalloc(img_buf_count, sizeof(char *), GFP_KERNEL);
		if (private_info->debug_img_buffer_name == NULL) {
			TCON_ERR("fail to allocate debug_img_buffer_name");
			break;
		}

		for (i = 0; i < img_buf_count; i++) {
			private_info->debug_img_buffer_size[i] = buffer_size;
			private_info->debug_img_buffer_va[i] =
				(char *)dma_alloc_coherent(private_info->dev,
					private_info->debug_img_buffer_size[i],
					&private_info->debug_img_buffer_pa[i],
					GFP_KERNEL);
			if (private_info->debug_img_buffer_va[i] == NULL) {
				TCON_ERR("allocate debug image buffer fail w:%d h:%d size:%d",
					hw_tcon_get_display_width(),
					hw_tcon_get_display_height(),
					private_info->debug_img_buffer_size[i]);
				break;
			}
			private_info->debug_img_buffer_name[i] =
				kzalloc(MAX_FILE_NAME_LEN, GFP_KERNEL);
			if (private_info->debug_img_buffer_name[i] == NULL) {
				TCON_ERR("allocate buffer name fail");
				break;
			}
		}
		private_info->debug_img_buffer_available = true;
		return HWTCON_STATUS_OK;
	} while (0);

	// free allocated memory and clean up
	if (private_info->debug_img_buffer_va != NULL &&
		private_info->debug_img_buffer_pa != NULL) {
		for (i = 0; i < img_buf_count; i++) {
			if (private_info->debug_img_buffer_va[i] != NULL) {
				dma_free_coherent(private_info->dev,
					private_info->debug_img_buffer_size[i],
					private_info->debug_img_buffer_va[i],
					private_info->debug_img_buffer_pa[i]);
			} else
				break;
			}
	}

	if (private_info->debug_img_buffer_name != NULL) {
		for (i = 0; i < img_buf_count; i++) {
			if (private_info->debug_img_buffer_name[i] != NULL)
				kfree(private_info->debug_img_buffer_name[i]);
			else
				break;
			kvfree(private_info->debug_img_buffer_name);
		}
	}

	if (private_info->debug_img_buffer_va != NULL)
		kvfree(private_info->debug_img_buffer_va);

	if (private_info->debug_img_buffer_pa != NULL)
		kvfree(private_info->debug_img_buffer_pa);

	if (private_info->debug_img_buffer_size != NULL)
		kvfree(private_info->debug_img_buffer_size);

	return HWTCON_STATUS_FB_ALLOC_FAIL;
}

static size_t calculate_frame_size(void)
{
	return (hw_tcon_get_display_width() * hw_tcon_get_display_height()
		* hwtcon_device_info()->color_format);
}


int hwtcon_fb_alloc_debug_fb_buffer(struct fb_private_info *private_info)
{
	int i = 0;
	int buffer_size = calculate_frame_size();
	int fb_buf_count = private_info->debug_fb_buffer_count;

	if (private_info->debug_fb_buffer_available)
		return HWTCON_STATUS_OK;

	TCON_WARN("Enabling debug fb buffer, %d MB additional memory will be used.",
		(fb_buf_count * buffer_size) >> 20);

	do {
		private_info->debug_fb_buffer_size =
			(size_t *)kcalloc(fb_buf_count, sizeof(size_t), GFP_KERNEL);
		if (private_info->debug_fb_buffer_size == NULL) {
			TCON_ERR("fail to allocate debug_img_buffer_size");
			break;
		}

		private_info->debug_fb_buffer_va =
			(char **)kcalloc(fb_buf_count, sizeof(char *), GFP_KERNEL);
		if (private_info->debug_fb_buffer_va == NULL) {
			TCON_ERR("fail to allocate debug_img_buffer_va");
			break;
		}

		private_info->debug_fb_buffer_pa =
			(dma_addr_t *)kcalloc(fb_buf_count, sizeof(dma_addr_t), GFP_KERNEL);
		if (private_info->debug_fb_buffer_pa == NULL) {
			TCON_ERR("fail to allocate debug_img_buffer_pa");
			break;
		}

		private_info->debug_fb_buffer_name =
			(char **)kcalloc(fb_buf_count, sizeof(char *), GFP_KERNEL);
		if (private_info->debug_fb_buffer_name == NULL) {
			TCON_ERR("fail to allocate debug_img_buffer_name");
			break;
		}

		for (i = 0; i < fb_buf_count; i++) {
			private_info->debug_fb_buffer_size[i] = buffer_size;
			private_info->debug_fb_buffer_va[i] =
				(char *)dma_alloc_coherent(private_info->dev,
					private_info->debug_fb_buffer_size[i],
					&private_info->debug_fb_buffer_pa[i],
					GFP_KERNEL);
			if (private_info->debug_fb_buffer_va[i] == NULL) {
				TCON_ERR("allocate debug image buffer fail w:%d h:%d size:%d",
					hwtcon_fb_get_virtual_width(),
					hwtcon_fb_get_height(),
					private_info->debug_fb_buffer_size[i]);
				break;
			}
			private_info->debug_fb_buffer_name[i] =
				kzalloc(MAX_FILE_NAME_LEN, GFP_KERNEL);
			if (private_info->debug_fb_buffer_name[i] == NULL) {
				TCON_ERR("allocate buffer name fail");
				break;
			}
		}
		private_info->debug_fb_buffer_available = true;
		return HWTCON_STATUS_OK;
	} while (0);

	// free allocated memory and clean up
	if (private_info->debug_fb_buffer_va != NULL &&
		private_info->debug_fb_buffer_pa != NULL) {
		for (i = 0; i < fb_buf_count; i++) {
			if (private_info->debug_fb_buffer_va[i] != NULL) {
				dma_free_coherent(private_info->dev,
					private_info->debug_fb_buffer_size[i],
					private_info->debug_fb_buffer_va[i],
					private_info->debug_fb_buffer_pa[i]);
			} else
				break;
			}
	}

	if (private_info->debug_fb_buffer_name != NULL) {
		for (i = 0; i < fb_buf_count; i++) {
			if (private_info->debug_fb_buffer_name[i] != NULL)
				kfree(private_info->debug_fb_buffer_name[i]);
			else
				break;
			kvfree(private_info->debug_fb_buffer_name);
		}
	}

	if (private_info->debug_fb_buffer_va != NULL)
		kvfree(private_info->debug_fb_buffer_va);

	if (private_info->debug_fb_buffer_pa != NULL)
		kvfree(private_info->debug_fb_buffer_pa);

	if (private_info->debug_fb_buffer_size != NULL)
		kvfree(private_info->debug_fb_buffer_size);

	return HWTCON_STATUS_FB_ALLOC_FAIL;
}

/*
static size_t calculate_frame_size(void)
{
	size_t rot0, rot3 = 0;

	switch (hwtcon_device_info()->color_format) {
	case 1:
		rot0 = ALIGN(hw_tcon_get_edp_width(), FB_ALIGN) * hw_tcon_get_edp_height();
		rot3 = hw_tcon_get_edp_width() * ALIGN(hw_tcon_get_edp_height(), FB_ALIGN);
		break;
	case 2:
		rot0 = ALIGN(hw_tcon_get_edp_width(), FB_ALIGN) * hw_tcon_get_edp_height() * 2;
		rot3 = hw_tcon_get_edp_width() * ALIGN(hw_tcon_get_edp_height(), FB_ALIGN) * 2;
		break;
	case 3:
		rot0 = ALIGN(hw_tcon_get_edp_width(), FB_ALIGN) * hw_tcon_get_edp_height() * 3;
		rot3 = hw_tcon_get_edp_width() * ALIGN(hw_tcon_get_edp_height(), FB_ALIGN) * 3;
		break;
	case 4:
		rot0 = ALIGN(hw_tcon_get_edp_width(), FB_ALIGN) * hw_tcon_get_edp_height() * 4;
		rot3 = hw_tcon_get_edp_width() * ALIGN(hw_tcon_get_edp_height(), FB_ALIGN) * 4;
		break;
	default:
		rot0 = ALIGN(hw_tcon_get_edp_width(), FB_ALIGN) * hw_tcon_get_edp_height();
		rot3 = hw_tcon_get_edp_width() * ALIGN(hw_tcon_get_edp_height(), FB_ALIGN);
		break;
	}

	return (rot0 > rot3 ? rot0 : rot3);
}
*/

static int hwtcon_fb_init_private_fb_info(
	struct fb_private_info *private_info, struct platform_device *pdev)
{
	unsigned long flags;
	int i = 0;
	int ret = 0;

	private_info->regulator_vcore =
		devm_regulator_get_optional(&pdev->dev, "hwtcon-dvfsrc-vcore");
	if (private_info->regulator_vcore == NULL)
		TCON_ERR("%s get vcore regulator fail\n", __func__);

	if (private_info->regulator_vcore != NULL) {
		/* request vcore voltage 0.65V */
		ret = regulator_set_voltage(
			private_info->regulator_vcore,
			650000, INT_MAX);
		if (ret)
			TCON_ERR(
			"%s regulator_set_voltage(mt8512-hwtcon) fail ret:%d\n",
				__func__, ret);
	}

	private_info->dev = &pdev->dev;
	/* set private_info as dev->data */
	dev_set_drvdata(&pdev->dev, private_info);

	/* allocate frame buffer */
	if (hw_tcon_get_epd_type()) {
		private_info->fb_buffer_size = calculate_frame_size() * FB_VIRTUAL_FRAME_COUNT;
		private_info->fb_cache_buf = hwtcon_fb_alloc_cached_mva_buf(&pdev->dev,
			private_info->fb_buffer_size,
			GFP_KERNEL);
		if (private_info->fb_cache_buf == NULL) {
			TCON_ERR("allocate fb buffer fail w:%d h:%d size:%d",
				hw_tcon_get_display_width(),
				hw_tcon_get_display_height(),
				private_info->fb_buffer_size);
			return HWTCON_STATUS_FB_ALLOC_FAIL;
		}
		private_info->fb_buffer_va = private_info->fb_cache_buf->alloc_va;
		private_info->fb_buffer_pa = private_info->fb_cache_buf->dma_handle;
	} else {
		private_info->fb_buffer_size = calculate_frame_size() * FB_VIRTUAL_FRAME_COUNT;
		private_info->fb_buffer_va = (char *)dma_alloc_coherent(&pdev->dev,
			private_info->fb_buffer_size,
			&private_info->fb_buffer_pa,
			GFP_KERNEL);

		if (private_info->fb_buffer_va == NULL) {
			TCON_ERR("allocate fb buffer fail w:%d h:%d size:%d",
			hw_tcon_get_display_width(),
			hw_tcon_get_display_height(),
			private_info->fb_buffer_size);
		return HWTCON_STATUS_FB_ALLOC_FAIL;
		}
	}

	if (hw_tcon_get_epd_type()) {
		/* allocate color buffer */
		private_info->color_buffer_size =
			hw_tcon_get_display_width() * hw_tcon_get_display_height();
		private_info->color_cache_buf = hwtcon_fb_alloc_cached_mva_buf(&pdev->dev,
			private_info->color_buffer_size, GFP_KERNEL);
		if (private_info->color_cache_buf == NULL) {
			TCON_ERR("allocate color_buffer fail");
			return HWTCON_STATUS_FB_ALLOC_FAIL;
		}
		private_info->color_buffer_va = private_info->color_cache_buf->alloc_va;
		private_info->color_buffer_pa = private_info->color_cache_buf->dma_handle;

		/* allocate image buffer */
		private_info->img_buffer_size =
			hw_tcon_get_display_width() * hw_tcon_get_display_height();
		private_info->img_cache_buf = hwtcon_fb_alloc_cached_mva_buf(&pdev->dev,
			private_info->img_buffer_size,
			GFP_KERNEL);
		if (private_info->img_cache_buf == NULL) {
			TCON_ERR("allocate image buffer fail w:%d h:%d size:%d",
				hw_tcon_get_display_width(),
				hw_tcon_get_display_height(),
				private_info->img_buffer_size);
			return HWTCON_STATUS_FB_ALLOC_FAIL;
		}
		private_info->img_buffer_va = private_info->img_cache_buf->alloc_va;
		private_info->img_buffer_pa = private_info->img_cache_buf->dma_handle;

		/* allocate regal buffer */
		private_info->temp_img_buffer_size =
			hw_tcon_get_display_width() * hw_tcon_get_display_height();
		private_info->temp_img_cache_buf = hwtcon_fb_alloc_cached_mva_buf(&pdev->dev,
			private_info->temp_img_buffer_size,
			GFP_KERNEL);
		if (private_info->temp_img_cache_buf == NULL) {
			TCON_ERR("allocate temp image buffer fail w:%d h:%d size:%d",
				hw_tcon_get_display_width(),
				hw_tcon_get_display_height(),
				private_info->temp_img_buffer_size);
			return HWTCON_STATUS_FB_ALLOC_FAIL;
		}
		private_info->temp_img_buffer_va = private_info->temp_img_cache_buf->alloc_va;
		private_info->temp_img_buffer_pa = private_info->temp_img_cache_buf->dma_handle;

		/* allocate regal buffer */
		private_info->partial_regal_buffer2_size =
			hw_tcon_get_display_width() * hw_tcon_get_display_height();
		private_info->partial_regal_cache_buf2 = hwtcon_fb_alloc_cached_mva_buf(&pdev->dev,
			private_info->partial_regal_buffer2_size,
			GFP_KERNEL);
		if (private_info->partial_regal_cache_buf2== NULL) {
			TCON_ERR("allocate csr2 partial buffer fail w:%d h:%d size:%d",
				hw_tcon_get_display_width(),
				hw_tcon_get_display_height(),
				private_info->partial_regal_buffer2_size);
			return HWTCON_STATUS_FB_ALLOC_FAIL;
		}
		private_info->partial_regal_buffer2_va = private_info->partial_regal_cache_buf2->alloc_va;
		private_info->partial_regal_buffer2_pa = private_info->partial_regal_cache_buf2->dma_handle;

		/* allocate image buffer */
		private_info->pre_img_buffer_size =
			hw_tcon_get_display_width() * hw_tcon_get_display_height();
		private_info->pre_img_cache_buf = hwtcon_fb_alloc_cached_mva_buf(&pdev->dev,
			private_info->pre_img_buffer_size,
			GFP_KERNEL);
		if (private_info->pre_img_cache_buf == NULL) {
			TCON_ERR("allocate pre image buffer fail w:%d h:%d size:%d",
				hw_tcon_get_display_width(),
				hw_tcon_get_display_height(),
				private_info->pre_img_buffer_size);
			return HWTCON_STATUS_FB_ALLOC_FAIL;
		}
		private_info->pre_img_buffer_va = private_info->pre_img_cache_buf->alloc_va;
		private_info->pre_img_buffer_pa = private_info->pre_img_cache_buf->dma_handle;
	} else {
		/* allocate image buffer */
		private_info->img_buffer_size =
			hw_tcon_get_display_width() * hw_tcon_get_display_height();
		private_info->img_buffer_va = (char *)dma_alloc_coherent(&pdev->dev,
			private_info->img_buffer_size,
			&private_info->img_buffer_pa,
			GFP_KERNEL);
		if (private_info->img_buffer_va == NULL) {
			TCON_ERR("allocate image buffer fail w:%d h:%d size:%d",
				hw_tcon_get_display_width(),
				hw_tcon_get_display_height(),
				private_info->img_buffer_size);
			return HWTCON_STATUS_FB_ALLOC_FAIL;
		}

		/* allocate regal buffer */
		private_info->temp_img_buffer_size =
			hw_tcon_get_display_width() * hw_tcon_get_display_height();
		private_info->temp_img_buffer_va = (char *)dma_alloc_coherent(&pdev->dev,
			private_info->temp_img_buffer_size,
			&private_info->temp_img_buffer_pa,
			GFP_KERNEL);
		if (private_info->temp_img_buffer_va == NULL) {
			TCON_ERR("allocate temp image buffer fail w:%d h:%d size:%d",
				hw_tcon_get_display_width(),
				hw_tcon_get_display_height(),
				private_info->temp_img_buffer_size);
			return HWTCON_STATUS_FB_ALLOC_FAIL;
		}
	}

	/* allocate debug image buffer info */
	private_info->debug_img_buffer_counter = 0;
	private_info->debug_img_buffer_available = false;
	if (hwtcon_device_info()->param_enable_debug_img_buffer) {
		if (hwtcon_fb_alloc_debug_img_buffer(private_info))
			return HWTCON_STATUS_FB_ALLOC_FAIL;
	}

	/* allocate debug fb buffer info */
	private_info->debug_fb_buffer_counter = 0;
	private_info->debug_fb_buffer_available = false;
	if (hwtcon_device_info()->param_enable_debug_fb_buffer) {
		if (hwtcon_fb_alloc_debug_fb_buffer(private_info))
			return HWTCON_STATUS_FB_ALLOC_FAIL;
	}

	if (hw_tcon_get_dual_mode()) {
		/* need to double working buffer size. */
		private_info->wb_buffer_size =
			hw_tcon_get_display_width() * hw_tcon_get_display_height() * 4;
	} else {
		private_info->wb_buffer_size =
			hw_tcon_get_display_width() * hw_tcon_get_display_height() * 2;
	}

	if (hwtcon_device_info()->reserved_buf_ready) {
		/* use reserved buffer */
		private_info->waveform_va = hwtcon_device_info()->reserved_buf_va;
		private_info->waveform_pa = hwtcon_device_info()->reserved_buf_mva;
		private_info->waveform_size = hwtcon_driver_get_reserved_waveform_buffer_size();
		private_info->wb_buffer_va = hwtcon_device_info()->reserved_buf_va +
			hwtcon_driver_get_reserved_waveform_buffer_size();
		private_info->wb_buffer_pa = hwtcon_device_info()->reserved_buf_mva +
			hwtcon_driver_get_reserved_waveform_buffer_size();
	} else {
		/* use dma alloc buffer */
		/* allocate working buffer */

			private_info->wb_buffer_va = (char *)dma_alloc_coherent(&pdev->dev,
				private_info->wb_buffer_size,
				&private_info->wb_buffer_pa,
				GFP_KERNEL);

			if (private_info->wb_buffer_va == NULL) {
				TCON_ERR("allocate working buffer fail w:%d h:%d size:%d",
					hw_tcon_get_display_width(),
					hw_tcon_get_display_height(),
					private_info->wb_buffer_size);
				return HWTCON_STATUS_FB_ALLOC_FAIL;
			}

		/* allocate waveform buffer */
		private_info->waveform_size = hwtcon_driver_get_reserved_waveform_buffer_size();
		private_info->waveform_va = (char *)dma_alloc_coherent(&pdev->dev,
			private_info->waveform_size,
			&private_info->waveform_pa,
			GFP_KERNEL);

		if (private_info->waveform_va == NULL) {
			TCON_ERR("allocate waveform buffer fail size:%d",
				private_info->waveform_size);
			return HWTCON_STATUS_FB_ALLOC_FAIL;
		}
	}

	hwtcon_debug_err_printf("fb buffer va: %p pa:0x%08x size:0x%x\n",
			private_info->fb_buffer_va,
			private_info->fb_buffer_pa,
			private_info->fb_buffer_size);
	if (hw_tcon_get_epd_type()) {
		hwtcon_debug_err_printf("color buffer va: %p pa:0x%08x size:0x%x\n",
			private_info->color_buffer_va,
			private_info->color_buffer_pa,
			private_info->color_buffer_size);
	}
	hwtcon_debug_err_printf("img buffer va: %p pa:0x%08x size:0x%x\n",
		private_info->img_buffer_va,
		private_info->img_buffer_pa,
		private_info->img_buffer_size);
	hwtcon_debug_err_printf("temp img buffer va: %p pa:0x%08x size:0x%x\n",
		private_info->temp_img_buffer_va,
		private_info->temp_img_buffer_pa,
		private_info->temp_img_buffer_size);
	hwtcon_debug_err_printf("wb va:%p pa:0x%08x size:0x%x reserve:%d\n",
		private_info->wb_buffer_va,
		private_info->wb_buffer_pa,
		private_info->wb_buffer_size,
		hwtcon_device_info()->reserved_buf_ready);
	hwtcon_debug_err_printf("waveform buffer va:%p pa:0x%08x size:0x%x reserve:%d\n",
		private_info->waveform_va,
		private_info->waveform_pa,
		private_info->waveform_size,
		hwtcon_device_info()->reserved_buf_ready);

	memset(private_info->pseudo_palette, 0,
		sizeof(private_info->pseudo_palette));

	private_info->temperature = EINK_DEFAULT_TEMPERATURE;
	private_info->pipeline_busy = false;
	mutex_init(&private_info->hwtcon_first_call_mutex);
	private_info->hwtcon_first_call = true;

	INIT_DELAYED_WORK(&private_info->read_sensor_work,
		hwtcon_fb_read_temperature_from_sensor);

	schedule_delayed_work(&private_info->read_sensor_work,
			msecs_to_jiffies(0));

	for (i = 0; i < MAX_LUT_REGION_COUNT; i++)
		hwtcon_fb_info()->lut_pic_order[i] = MIN_PIC_ORDER;

	spin_lock_init(&private_info->lut_pic_order_lock);

	private_info->mmsys_power_enable = false;
	mutex_init(&private_info->mmsys_power_enable_lock);
	private_info->hwtcon_clk_enable = false;
	spin_lock_init(&private_info->hwtcon_clk_enable_lock);

	spin_lock_init(&private_info->hwtcon_tcon_reg_lock);

	/* start timer for mmsys power down timeout. */
	timer_setup(&private_info->mmsys_power_timer,
		hwtcon_core_handle_mmsys_power_down_cb,
		0L);

	private_info->update_scheme = UPDATE_SCHEME_QUEUE;
	private_info->power_down_delay_ms = EINK_DEFAULT_POWER_DOWN_TIME;
	private_info->ignore_request = false;
	private_info->enable_night_mode = false;
	private_info->current_temp_zone = -1;
	private_info->current_night_mode = -1;
	private_info->current_wf_work_mode = -1;
	private_info->max_release_lut_count = MAX_RELEASED_LUT_COUNT;

	/* stylus mode init */
	private_info->stylus_mode = false;
	private_info->stylus_enabled = false;
	spin_lock_init(&private_info->frame_lut_lock);
	private_info->frame_trigger_lut = 0;
	private_info->frame_valid_lut = 0;
	private_info->task_in_scrolling = false;

	/* init timer for LUT release */
	for (i = 0; i < MAX_LUT_REGION_COUNT; i++)
		timer_setup(&private_info->timer_lut_release[i],
			hwtcon_core_handle_lut_release_timeout_cb, 0);

	/* init sw mitigation data */
	private_info->sw_algo.scan_lines = SCAN_LINE_TOTAL;
	private_info->sw_algo.count_thres = SCAN_LINE_THRESHOLD;
	private_info->sw_algo.pixel_thres = TRANSITION_THRESHOLD;
	private_info->sw_algo.str_thres = STRENGTH_THREASHOLD;
	private_info->sw_algo.scaled_width = 0;
	private_info->sw_algo.scaled_height = 0;
	private_info->sw_algo.scaled_factor = SCALED_FACTOR;

	/*init mark_store for MXCFB_WAIT_FOR_ANY_UPDATE_COMPLETE support */
	marker_store_init(&private_info->marker_store);
	private_info->update_flag_fast_mode = 0;

	/* init the stylus_detected variable */
	private_info->stylus_detected = false;
	private_info->wb_clearing = false;

	/* init debug_img_buffer_count to default value (10)*/
	private_info->debug_img_buffer_count = DEFAULT_DEBUG_IMAGE_BUFFER_COUNT;

	/* init debug_img_buffer_count to default value (10)*/
	private_info->debug_fb_buffer_count = DEFAULT_DEBUG_FB_BUFFER_COUNT;

	init_completion(&private_info->wb_frame_done_completion);

	spin_lock_init(&private_info->lut_free_lock);
	spin_lock_init(&private_info->lut_active_lock);
	spin_lock_init(&private_info->g_update_order_lock);

	private_info->scrolling_task = NULL;
	spin_lock_init(&private_info->scrolling_lock);

	private_info->wake_lock = wakeup_source_register(&pdev->dev, "hwtcon_wakelock");
	private_info->wake_timeout_lock = wakeup_source_register(&pdev->dev,
			"hwtcon_wake_timeout_lock");

	spin_lock_irqsave(&private_info->g_update_order_lock, flags);
	private_info->g_update_order = 0;
	private_info->g_update_cnt = 0;
	spin_unlock_irqrestore(&private_info->g_update_order_lock, flags);

	spin_lock_irqsave(&private_info->lut_free_lock, flags);
	private_info->lut_free = hw_tcon_get_all_lut();
	spin_unlock_irqrestore(&private_info->lut_free_lock, flags);

	spin_lock_irqsave(&private_info->lut_active_lock, flags);
	private_info->lut_active = 0LL;
	spin_unlock_irqrestore(&private_info->lut_active_lock, flags);

	/* init task list. */
	spin_lock_init(&private_info->fb_global_marker_list.lock);
	INIT_LIST_HEAD(&private_info->fb_global_marker_list.list);

	spin_lock_init(&private_info->free_task_list.lock);
	INIT_LIST_HEAD(&private_info->free_task_list.list);

	spin_lock_init(&private_info->wait_for_mdp_task_list.lock);
	INIT_LIST_HEAD(&private_info->wait_for_mdp_task_list.list);

	spin_lock_init(&private_info->mdp_processing_task_list.lock);
	INIT_LIST_HEAD(&private_info->mdp_processing_task_list.list);
	spin_lock_init(&private_info->mdp_done_task_list.lock);
	INIT_LIST_HEAD(&private_info->mdp_done_task_list.list);

	spin_lock_init(&private_info->pipeline_processing_task_list.lock);
	INIT_LIST_HEAD(&private_info->pipeline_processing_task_list.list);

	spin_lock_init(&private_info->pipeline_done_task_list.lock);
	INIT_LIST_HEAD(&private_info->pipeline_done_task_list.list);

	spin_lock_init(&private_info->collision_task_list.lock);
	INIT_LIST_HEAD(&private_info->collision_task_list.list);

	/* init wait queue */
	init_waitqueue_head(&private_info->power_state_change_wait_queue);
	init_waitqueue_head(&private_info->wf_lut_release_wait_queue);
	init_waitqueue_head(&private_info->mdp_trigger_wait_queue);
	init_waitqueue_head(&private_info->pipeline_trigger_wait_queue);
	init_waitqueue_head(&private_info->task_state_wait_queue);
	init_waitqueue_head(&private_info->clear_wb_wait_queue);

	/* init blank */
	private_info->blank = FB_BLANK_UNBLANK;

	/* init cfa_gain_enabled, default use gain2 */
	private_info->cfa_gain_enabled = true;

	/* init update queue */
	mutex_init(&private_info->update_queue_mutex);
	mutex_init(&private_info->image_buffer_access_mutex);

	/* create work queue. */
	private_info->wq_power_down_mmsys =
		create_singlethread_workqueue("power_down_mmsys_domain");
	private_info->wq_pipeline_written_done =
		create_singlethread_workqueue("handle_pieline_written_done");
	private_info->wq_wf_lut_display_done =
		create_singlethread_workqueue("handle_wf_lut_display_done");

	/* init work */
	INIT_WORK(&hwtcon_fb_info()->wk_power_down_mmsys,
		hwtcon_core_handle_mmsys_power_down);

	private_info->tchbst_opt_enabled = true;
	private_info->hist.index = 0;

	for (i = 0; i < MAX_INPUT_INDEX; i++) {
		private_info->hist.update[i].jiffies = 0;
		private_info->hist.update[i].width = 0;
		private_info->hist.update[i].height = 0;
		private_info->hist.update[i].time_gap = 0;
	}

	return 0;
}

static int hwtcon_fb_release_private_fb_info(
	struct fb_private_info *private_info, struct platform_device *pdev)
{
	int i = 0;

	if (private_info->regulator_vcore) {
		devm_regulator_put(private_info->regulator_vcore);
		private_info->regulator_vcore = NULL;
	}
	if (hw_tcon_get_epd_type()) {
		if (private_info->fb_cache_buf != NULL)
			hwtcon_fb_free_cached_mva_buf(private_info->fb_cache_buf);

		if (private_info->img_cache_buf != NULL)
			hwtcon_fb_free_cached_mva_buf(private_info->img_cache_buf);

		if (private_info->temp_img_cache_buf != NULL)
			hwtcon_fb_free_cached_mva_buf(private_info->temp_img_cache_buf);

		if (private_info->partial_regal_cache_buf2!= NULL)
			hwtcon_fb_free_cached_mva_buf(private_info->partial_regal_cache_buf2);

		if (private_info->color_cache_buf != NULL)
			hwtcon_fb_free_cached_mva_buf(private_info->color_cache_buf);

		if (private_info->pre_img_cache_buf != NULL)
			hwtcon_fb_free_cached_mva_buf(private_info->pre_img_cache_buf);
	} else {
		if (private_info->fb_buffer_va != NULL)
			dma_free_coherent(&pdev->dev, private_info->fb_buffer_size,
				private_info->fb_buffer_va,
				private_info->fb_buffer_pa);

		if (private_info->img_buffer_va != NULL)
			dma_free_coherent(&pdev->dev, private_info->img_buffer_size,
				private_info->img_buffer_va,
				private_info->img_buffer_pa);

		if (private_info->temp_img_buffer_va != NULL)
			dma_free_coherent(&pdev->dev, private_info->temp_img_buffer_size,
				private_info->temp_img_buffer_va,
				private_info->temp_img_buffer_pa);
	}

	/* release debug image buffer info */
	if (private_info->debug_img_buffer_available) {
		for (i = 0; i < private_info->debug_img_buffer_count; i++) {
			if (private_info->debug_img_buffer_va[i] != NULL)
				dma_free_coherent(&pdev->dev,
					private_info->debug_img_buffer_size[i],
					private_info->debug_img_buffer_va[i],
					private_info->debug_img_buffer_pa[i]);

			if (private_info->debug_img_buffer_name[i] != NULL)
				kfree(private_info->debug_img_buffer_name[i]);
		}
		private_info->debug_img_buffer_available = false;
		kvfree(private_info->debug_img_buffer_va);
		kvfree(private_info->debug_img_buffer_pa);
		kvfree(private_info->debug_img_buffer_name);
		kvfree(private_info->debug_img_buffer_size);
	}

	/* release debug fb buffer info */
	if (private_info->debug_fb_buffer_available) {
		for (i = 0; i < private_info->debug_fb_buffer_count; i++) {
			if (private_info->debug_fb_buffer_va[i] != NULL)
				dma_free_coherent(&pdev->dev,
					private_info->debug_fb_buffer_size[i],
					private_info->debug_fb_buffer_va[i],
					private_info->debug_fb_buffer_pa[i]);

			if (private_info->debug_fb_buffer_name[i] != NULL)
				kfree(private_info->debug_fb_buffer_name[i]);
		}
		private_info->debug_fb_buffer_available = false;
		kvfree(private_info->debug_fb_buffer_va);
		kvfree(private_info->debug_fb_buffer_pa);
		kvfree(private_info->debug_fb_buffer_name);
		kvfree(private_info->debug_fb_buffer_size);
	}

	if (!hwtcon_device_info()->reserved_buf_ready) {

			if (private_info->wb_buffer_va != NULL)
				dma_free_coherent(&pdev->dev,
				private_info->wb_buffer_size,
				private_info->wb_buffer_va,
				private_info->wb_buffer_pa);

		if (private_info->waveform_va != NULL)
			dma_free_coherent(&pdev->dev,
				private_info->waveform_size,
				private_info->waveform_va,
				private_info->waveform_pa);
	}

	for (i = 0; i < MAX_LUT_REGION_COUNT; i++)
		del_timer(&hwtcon_fb_info()->timer_lut_release[i]);

	if (private_info->wq_pipeline_written_done)
		destroy_workqueue(private_info->wq_pipeline_written_done);

	if (private_info->wq_power_down_mmsys)
		destroy_workqueue(private_info->wq_power_down_mmsys);

	if (private_info->wq_wf_lut_display_done)
		destroy_workqueue(private_info->wq_wf_lut_display_done);

	if (delayed_work_pending(&private_info->read_sensor_work))
		cancel_delayed_work(&private_info->read_sensor_work);

	return 0;
}

struct fb_private_info *hwtcon_fb_info(void)
{
	return (struct fb_private_info *)g_framebuffer_info->par;
}
EXPORT_SYMBOL(hwtcon_fb_info);

u32 hwtcon_fb_get_rotation(void)
{
	return g_framebuffer_info->var.rotate;
}
EXPORT_SYMBOL(hwtcon_fb_get_rotation);

void hwtcon_fb_get_resolution(u32 *width, u32 *height)
{
	if (width)
		*width = g_framebuffer_info->var.xres;
	if (height)
		*height = g_framebuffer_info->var.yres;
}

u32 hwtcon_fb_get_grayscale(void)
{
	return g_framebuffer_info->var.grayscale;
}

int hwtcon_fb_register_fb(struct platform_device *pdev)
{
	int status = 0;

	/* allocate fb_info & fb_private_info */
	g_framebuffer_info = framebuffer_alloc(sizeof(struct fb_private_info),
		&pdev->dev);
	if (g_framebuffer_info == NULL) {
		TCON_ERR("framebuffer_alloc fail");
		return HWTCON_STATUS_FB_STRUCT_ALLOC_FAIL;
	}

	status = hwtcon_fb_init_private_fb_info(
		(struct fb_private_info *)g_framebuffer_info->par, pdev);
	if (status != 0)
		return status;

	status = hwtcon_fb_init_fb_info(g_framebuffer_info);
	if (status != 0)
		return status;

	/* register frame buffer */
	status = register_framebuffer(g_framebuffer_info);
	if (status != 0) {
		TCON_ERR("register frame buffer fail:%d", status);
		return status;
	}

	status = hwtcon_fb_kthread_create();

	return status;
}

int hwtcon_fb_unregister_fb(struct platform_device *pdev)
{
	if (g_framebuffer_info == NULL)
		return 0;

	hwtcon_fb_kthread_destroy();

	/* unregister frame buffer */
	unregister_framebuffer(g_framebuffer_info);

	hwtcon_fb_release_private_fb_info(
		(struct fb_private_info *)g_framebuffer_info->par, pdev);

	/* release fb_info & fb_private_info */
	framebuffer_release(g_framebuffer_info);
	return 0;
}

struct hwtcon_cache_buf *hwtcon_fb_alloc_cached_mva_buf(struct device *dev,
	size_t size,
	const gfp_t flag)
{
	int i = 0;
	int status = 0;
	struct hwtcon_cache_buf *buf = NULL;
	int entry_count = 0;

	buf = vzalloc(sizeof(*buf));
	if (!buf) {
		TCON_ERR("vmalloc hwtcon cache buffer fail");
		return NULL;
	}

	buf->dev = dev;
	buf->size = size;
	buf->alloc_va = vzalloc(size);
	if (!buf->alloc_va) {
		TCON_ERR("allocate va size:%ld fail", (unsigned long)size);
		goto ALLOC_VA_FAIL;
	}

	buf->sgt = vzalloc(sizeof(struct sg_table));
	if (!buf->sgt) {
		TCON_ERR("alloc sgt fail");
		goto ALLOC_SGT_FAIL;
	}

	buf->n_pages = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	buf->pages = (struct page **)vmalloc(buf->n_pages * sizeof(struct page *));
	if (buf->pages == NULL) {
		TCON_ERR("allocate pages with size:%zu fail",
			buf->n_pages * sizeof(struct page *));
		goto ALLOC_PAGES_FAIL;
	}

	for (i = 0; i < buf->n_pages; i++)
		*(buf->pages + i) = vmalloc_to_page(buf->alloc_va + PAGE_SIZE * i);

	status = sg_alloc_table_from_pages(buf->sgt, buf->pages, buf->n_pages, 0UL, size, flag);
	if (status != 0) {
		TCON_ERR("alloc table from pages fail:%d", status);
		goto ALLOC_TABLE_FAIL;
	}

	entry_count = dma_map_sg_attrs(dev, buf->sgt->sgl, buf->sgt->nents, DMA_BIDIRECTIONAL, 0);
	if (entry_count <= 0) {
		TCON_ERR("fail to map scatterlist");
		goto MAP_SG_FAIL;
	}

	buf->dma_handle = sg_dma_address(buf->sgt->sgl);

	return buf;

MAP_SG_FAIL:
	sg_free_table(buf->sgt);
ALLOC_TABLE_FAIL:
	vfree(buf->pages);
ALLOC_PAGES_FAIL:
	vfree(buf->sgt);
ALLOC_SGT_FAIL:
	vfree(buf->alloc_va);
ALLOC_VA_FAIL:
	vfree(buf);
	return NULL;
}

void hwtcon_fb_free_cached_mva_buf(struct hwtcon_cache_buf *buf)
{
	dma_unmap_sg_attrs(buf->dev, buf->sgt->sgl, buf->sgt->nents, DMA_BIDIRECTIONAL, 0);
	sg_free_table(buf->sgt);

	vfree(buf->pages);
	vfree(buf->sgt);
	vfree(buf->alloc_va);
	vfree(buf);
}

/* invalid cache, set cache data invalid.
 * when CPU try to read this buffer, CPU will ignore cache content, and read from dram.
 * called after Hardware change the dram data, or before CPU read/write this buffer.
 */
void hwtcon_fb_invalid_cache(struct hwtcon_cache_buf *buf)
{
	dma_sync_sg_for_cpu(buf->dev, buf->sgt->sgl, buf->sgt->nents, DMA_FROM_DEVICE);
}

/* clean cache, flush cache content to dram.
 * called after CPU change the cache data. or before HW read/write this buffer.
 */
void hwtcon_fb_clean_cache(struct hwtcon_cache_buf *buf)
{
	dma_sync_sg_for_device(buf->dev, buf->sgt->sgl, buf->sgt->nents, DMA_TO_DEVICE);
}
