// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Amazon.com, Inc.  All rights reserved.
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

#include "hwtcon_fb.h"
#include "hwtcon_def.h"
/*
 *#include "hwtcon_debug.h"
 *#include "hwtcon_core.h"
 *#include "hwtcon_lightbox.h"
 *#include "fiti_core.h"
 *#include "hwtcon_wf_lut_config.h"
 */
#include <linux/platform_device.h>
#include <linux/fb.h>
#include <linux/dma-mapping.h>
#include <linux/uaccess.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/pm_qos.h>
#include <linux/device.h>
#include <linux/delay.h>


#include "hwtcon_epd.h"
#include "hwtcon_driver.h"


static DECLARE_WAIT_QUEUE_HEAD(wq);

/* width and height limit to replace with REAGL waveform*/
#define	REAGL_WIDTH_FACTOR	3
#define	REAGL_HEIGHT_FACTOR	4
int auto_waveform_replacement(struct mxcfb_update_data *upd_data)
{
	u32 xscrn, yscrn;
	struct fb_private_info *fb_priv = hwtcon_fb_info();

	if (upd_data->waveform_mode != WAVEFORM_MODE_DU
		&& upd_data->waveform_mode != WAVEFORM_MODE_GL16
		&& upd_data->waveform_mode != WAVEFORM_MODE_GC16_PARTIAL)
		return -1;	/*Olny replace DU and GL16, GC16_PARTIAL */

	if (hwtcon_core_use_night_mode())
		return -1;	/*day mode only */

	hwtcon_fb_get_resolution(&xscrn, &yscrn);
	if ((!(fb_priv->update_flag_fast_mode & UPDATE_FLAGS_MODE_FAST_FLAG)) &&
		(upd_data->update_region.width > xscrn / REAGL_WIDTH_FACTOR) &&
		(upd_data->update_region.height > yscrn / REAGL_HEIGHT_FACTOR)) {
		u32 wf, mode;

		wf = upd_data->waveform_mode;
		mode = upd_data->update_mode;
		upd_data->waveform_mode = WAVEFORM_MODE_GLR16;
		upd_data->update_mode = UPDATE_MODE_FULL;

		TCON_LOG("Replace waveform, update mode from [%d,%d] to [%d,%d]", wf, mode,
			upd_data->waveform_mode, upd_data->update_mode);
		return 0;
	}
	return -1;
}

int hwtcon_fb_ioctl_set_update_flags(struct fb_info *info, unsigned long arg)
{
	u32 new_flags;
	struct fb_private_info *fb_priv =
		info ? (struct fb_private_info *)info->par : hwtcon_fb_info();

	if (get_user(new_flags, (__u32 __user *) arg))
		return -EFAULT;

	switch (new_flags & UPDATE_FLAGS_MASK_PARAM) {
	case UPDATE_FLAGS_FAST_MODE:
		fb_priv->update_flag_fast_mode = new_flags & UPDATE_FLAGS_FAST_MODE_PARAM;
		TCON_LOG("Set fast_mode_flag = 0x%2x\n", fb_priv->update_flag_fast_mode);
		break;
	default:
		//print warning message for invalid parameter
		TCON_LOG("Unsupported update flag set new_flags = %x", new_flags);
		break;
	}

	return 0;
}


int hwtcon_fb_ioctl_get_update_flags(struct fb_info *info, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct fb_private_info *fb_priv =
		info ? (struct fb_private_info *)info->par : hwtcon_fb_info();
	u32 param = fb_priv->update_flag_fast_mode & UPDATE_FLAGS_FAST_MODE_PARAM;

	TCON_LOG("fast_mode_flag = 0x%2x\n",
		 fb_priv->update_flag_fast_mode & UPDATE_FLAGS_FAST_MODE_PARAM);

	if (put_user(param, (u32 __user *) argp))
		return -EFAULT;

	return 0;
}

int marker_store_reset(struct completed_marker_store *p_marker_store)
{
	p_marker_store->num_marker = 0;
	p_marker_store->head = 0;
	p_marker_store->tail = 0;
	return 0;
}

int marker_store_init(struct completed_marker_store *p_marker_store)
{
	/* init the completed_marker_info structure */
	marker_store_reset(p_marker_store);
	mutex_init(&p_marker_store->store_mutex);
	return 0;
}


int complete_markers_avail(struct completed_marker_store *p_marker_store)
{
	struct fb_private_info *fb_priv = hwtcon_fb_info();

	if (hwtcon_fb_get_grayscale() == GRAYSCALE_8BIT_INVERTED ||
		hwtcon_fb_info()->stylus_enabled ||
		(fb_priv->update_flag_fast_mode & UPDATE_FLAGS_MODE_FAST_FLAG)) {
		/*bypass if DARK mode or fast mode */
		return -1;
	}
	if (p_marker_store->num_marker > 0) {
		wake_up_interruptible(&wq);
		TCON_LOG("head = %d, tail = %d, num = %d, first marker %d\n",
			p_marker_store->head,
			p_marker_store->tail,
			p_marker_store->num_marker,
			p_marker_store->marker[p_marker_store->head]);
	}
	return 0;
}

int store_completed_markers(struct completed_marker_store *p_marker_store, __u32 done_marker)
{
#define  MARKER_STORE_THRESHOLD		(STORE_MARKER_NUM/2)
	bool need_to_wake_up_wq;
	struct fb_private_info *fb_priv = hwtcon_fb_info();

	if (hwtcon_fb_get_grayscale() == GRAYSCALE_8BIT_INVERTED ||
		hwtcon_fb_info()->stylus_enabled ||
		(fb_priv->update_flag_fast_mode & UPDATE_FLAGS_MODE_FAST_FLAG)) {
		/*bypass if DARK mode or fast mode */
		return -1;
	}

	/* update the completed update store */
	mutex_lock(&p_marker_store->store_mutex);
	if (p_marker_store->num_marker < STORE_MARKER_NUM) {
		p_marker_store->marker[p_marker_store->tail] = done_marker;
		TCON_LOG("store completed markers [%d]=%d", p_marker_store->tail, done_marker);
		p_marker_store->tail = (p_marker_store->tail + 1) % STORE_MARKER_NUM;
		p_marker_store->num_marker++;
	} else {
		TCON_LOG("Marker Store overflow. head = %d, tail = %d, num = %d, first marker %d ",
			p_marker_store->head,
			p_marker_store->tail,
			p_marker_store->num_marker,
			p_marker_store->marker[p_marker_store->head]);
	}

	/* to prevent store overflow,
	 * wakeup wq as long as the number in the store greater than a thrshold:
	 * p_marker_store->num_marker > MARKER_STORE_THRESHOLD
	 */
	need_to_wake_up_wq = p_marker_store->num_marker > MARKER_STORE_THRESHOLD;

	mutex_unlock(&p_marker_store->store_mutex);

	if (need_to_wake_up_wq) {/*wake up wq if reach threshold */
		wake_up_interruptible(&wq);
		TCON_WARN(
"Marker Store reach threshold %d head = %d, tail = %d, num = %d, first marker %d\n",
			MARKER_STORE_THRESHOLD,
			p_marker_store->head,
			p_marker_store->tail,
			p_marker_store->num_marker,
			p_marker_store->marker[p_marker_store->head]);
	}

	return 0;
}


int hwtcon_fb_ioctl_wait_for_any_update_complete(struct fb_info *info, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int ret = -EINVAL;

	u32 flag;
	int ix;
	unsigned int index;
	char __user *buf = (char __user *)argp;
	struct fb_private_info *fb_priv =
		info ? (struct fb_private_info *)info->par : hwtcon_fb_info();

	if (!get_user(flag, (__u32 __user *) arg)) {
		if (flag == FLAG_CHECK) {
			/* Indicate that this platform
			 * can do REAGL with collision
			 */
			ret = REAGL_FEATURE_1;
			TCON_LOG("FLAG_CHECK ret = %d", ret);
			return ret;
		}
	} else {
		TCON_ERR("FLAG_CHECK get_user error");
		ret = -EFAULT;
		return ret;
	}
	if (info)
		unlock_fb_info(info);

	wait_event_interruptible_timeout(
		wq, fb_priv->marker_store.num_marker > 0, msecs_to_jiffies(2000));

	if (fb_priv->marker_store.num_marker)
		TCON_LOG("copying %d completed markers to user space, head = %d, tail = %d\n",
			fb_priv->marker_store.num_marker, fb_priv->marker_store.head,
			fb_priv->marker_store.tail);

	mutex_lock(&fb_priv->marker_store.store_mutex);


	for (ix = 0; ix < fb_priv->marker_store.num_marker && ix < MAX_NUM_PENDING_UPDATES; ix++) {
		index = (fb_priv->marker_store.head + ix) % STORE_MARKER_NUM;

		if (copy_to_user(buf, &fb_priv->marker_store.marker[index], sizeof(__u32))) {
			TCON_ERR("Error copying marker %d to user",
				fb_priv->marker_store.marker[index]);
			ret = -EFAULT;
			goto hwtcon_core_wait_for_any_update_complete_end;
		}
		buf += sizeof(__u32);
	}
	if (ix) {
		fb_priv->marker_store.num_marker -= ix;
		TCON_LOG("copying %d markers [%d-%d] to user ret=%d, num_marker=%d", ix,
			 fb_priv->marker_store.marker[(fb_priv->marker_store.head) %
				STORE_MARKER_NUM],
			 fb_priv->marker_store.marker[(fb_priv->marker_store.head + ix - 1) %
				STORE_MARKER_NUM], ret,
			 fb_priv->marker_store.num_marker);
		fb_priv->marker_store.head = (fb_priv->marker_store.head + ix) % STORE_MARKER_NUM;
	}

	if (ret != -EFAULT)
		ret = ix * sizeof(__u32);

hwtcon_core_wait_for_any_update_complete_end:
	mutex_unlock(&fb_priv->marker_store.store_mutex);
	if (info)
		lock_fb_info(info);
	return ret;

}
