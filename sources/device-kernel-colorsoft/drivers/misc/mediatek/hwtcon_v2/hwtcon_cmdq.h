/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Jiaguang Zhang <jiaguang.zhang@mediatek.com>
 */
#ifndef __HWTCON_CMDQ_H__
#define __HWTCON_CMDQ_H__
#include <linux/types.h>
#include <linux/device.h>

enum LUT_RELEASE_ENUM {
	/* reserve for LUT 0 ~ 63 */
	LUT_RELEASE_TIME_START = 64,
	LUT_RELEASE_TIME_END = 65,
	LUT_RELEASE_MAX,
};

typedef void (*cmdq_cb) (struct cmdq_cb_data cb_data);

int hwtcon_cmdq_init(struct device *dev);
void hwtcon_cmdq_destroy(void);

struct cmdq_client *hwtcon_cmdq_get_pipeline_clt(void);
struct cmdq_client *hwtcon_cmdq_get_wf_lut_clt(void);

int hwtcon_cmdq_write_lut_release_slot(int index, u32 value);
int hwtcon_cmdq_read_lut_release_slot(int index, u32 *pReadBackVal);

int hwtcon_cmdq_lut_release_start_loop(cmdq_cb cb, struct cmdq_cb_data *cb_data);
void hwtcon_cmdq_lut_release_stop_loop(void);
#endif				/* endof __HWTCON_CMDQ_H__ */
