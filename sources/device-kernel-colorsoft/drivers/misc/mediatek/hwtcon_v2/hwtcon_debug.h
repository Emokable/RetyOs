/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Jiaguang Zhang <jiaguang.zhang@mediatek.com>
 */

#ifndef __HWTCON_DEBUG_H__
#define __HWTCON_DEBUG_H__
#include "hwtcon_def.h"

struct hwtcon_debug_info {
	bool log_level;		/* enable TCON_LOG ? */
	bool epdc_debug;	/* enable epdc debug */
	/* fixed temperature instead of read from sesor */
	int fixed_temperature;
	int debug[10];		/* only for debug use. */
	int enable_wf_lut_dpi_checksum;
	int enable_wf_lut_checksum;
	bool enable_dump_next_buffer;
	bool enable_dump_image_buffer;
	bool enable_dump_fb_buffer;
	bool fiti_power_always_on;
	char golden_file_name[100];
	unsigned char *debug_va;
	int collision_debug;
	int mdp_merge_debug;
	bool sw_mitigation;
	bool swipe_color;		/* enable color handle in swipe */
	bool swipe_color_phase2;		/* enable phase2 */
	bool swipe_color_clear;		/* enable P2SW clear algorithm ? */
	bool swipe_gray_phase2;		/* enable phase2 */
	bool swipe_gray_clear;		/* enable P2SW clear algorithm ? */
};

struct hwtcon_debug_info *hwtcon_debug_get_info(void);

void hwtcon_debug_err_printf(const char *print_msg, ...);
void hwtcon_debug_fiti_printf(const char *print_msg, ...);
void hwtcon_debug_lut_info_printf(const char *print_msg, ...);
void hwtcon_debug_record_printf(const char *print_msg, ...);

int hwtcon_debug_create_procfs(void);
int hwtcon_debug_destroy_procfs(void);

#endif				/* __HWTCON_DEBUG_H__ */
