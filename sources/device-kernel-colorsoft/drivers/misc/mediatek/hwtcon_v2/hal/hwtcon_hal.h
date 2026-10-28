/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Jiaguang Zhang <jiaguang.zhang@mediatek.com>
 */

#ifndef __HWTCON_HAL_H__
#define __HWTCON_HAL_H__
#include <linux/types.h>
#include <linux/mailbox/mtk-cmdq-mailbox-ext.h>
#include <linux/soc/mediatek/mtk-cmdq-ext.h>
#include "hwtcon_reg.h"
#include "hwtcon_def.h"

u32 *hwtcon_hal_convert_pa_2_va(u32 pa);
u32 pp_read(void *va);
u32 pp_read_pa(u32 pa);
void pp_write(struct cmdq_pkt *pkt, u32 pa, u32 value);
void pp_write_mask(struct cmdq_pkt *pkt, u32 pa, u32 value, u32 mask);
void pp_poll(struct cmdq_pkt *pkt, u32 pa, u32 value, u32 mask);
HWTCON_TIME timeofday_ms(void);
int hwtcon_hal_get_time_in_ms(HWTCON_TIME start, HWTCON_TIME end);
int hwtcon_hal_get_gpt_time_in_unit(HWTCON_TIME start, HWTCON_TIME end);
int hwtcon_hal_ffs(u64 data);
int hwtcon_hal_bit_set_cnt(u64 data);

#endif				/* __HWTCON_HAL_H__ */
