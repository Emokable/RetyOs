// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Jiaguang Zhang <jiaguang.zhang@mediatek.com>
 */

#include "hwtcon_hal.h"
#include "hwtcon_def.h"
#include "hwtcon_driver.h"
#include "hwtcon_epd.h"

#include <linux/dma-mapping.h>
#include <linux/ktime.h>


int hwtcon_hal_get_time_in_ms(HWTCON_TIME start, HWTCON_TIME end)
{
	HWTCON_TIME duration = end - start;

	return duration;
}

HWTCON_TIME timeofday_ms(void)
{
	return ktime_to_ms(ktime_get_real());
}

HWTCON_TIME timeofday_us(void)
{
	return ktime_to_us(ktime_get_real());
}

int hwtcon_hal_get_gpt_time_in_unit(HWTCON_TIME start, HWTCON_TIME end)
{
	/* 13 unit = 1us */
	HWTCON_TIME duration = 0;

	if (start > end)
		end += 0x100000000;
	duration = end - start;
	return duration;
}

u32 *hwtcon_hal_convert_pa_2_va(u32 pa)
{
	if (pa >= hwtcon_device_info()->mmsys_reg_pa &&
		pa < hwtcon_device_info()->mmsys_reg_pa + 0x10000)
		return (u32 *) ((pa - hwtcon_device_info()->mmsys_reg_pa) +
				hwtcon_device_info()->mmsys_reg_va);

	if (pa >= hwtcon_device_info()->imgsys_reg_pa &&
		pa < hwtcon_device_info()->imgsys_reg_pa + 0x10000)
		return (u32 *) ((pa - hwtcon_device_info()->imgsys_reg_pa) +
				hwtcon_device_info()->imgsys_reg_va);

	return NULL;
}

int hwtcon_hal_ffs(u64 data)
{
	int i = 0;
	if (hw_tcon_get_dual_mode()) {
		for (i = 2; i < 64; i++)
			if (data & (1LL << i))
				return i;
	} else {
		for (i = 0; i < 64; i++)
			if (data & (1LL << i))
				return i;
	}
	/* not found */
	return -1;
}

int hwtcon_hal_bit_set_cnt(u64 data)
{
	int i = 0;
	int count = 0;

	if (hw_tcon_get_dual_mode()) {
		for (i = 2; i < 64; i++)
			if (data & (1LL << i))
				count++;
	} else {
		for (i = 0; i < 64; i++)
			if (data & (1LL << i))
				count++;
	}
	return count;
}


u32 pp_read(void *va)
{
	return readl(va);
}

u32 pp_read_pa(u32 pa)
{
	u32 *va = ioremap(pa, sizeof(u32));
	u32 value = 0;

	value = readl(va);

	iounmap(va);
	return value;
}

void pp_write(struct cmdq_pkt *pkt, u32 pa, u32 value)
{
	if (!pkt) {
		u32 *va = hwtcon_hal_convert_pa_2_va(pa);
		bool register_remap = false;

		if (va == NULL) {
			va = ioremap(pa, sizeof(u32));
			register_remap = true;
		}

		writel(value, va);

		if (register_remap)
			iounmap(va);
	} else {
		/* use gce */
#if 0
		cmdq_pkt_assign_command(pkt, GCE_SPR0, pa);
		cmdq_pkt_store_value(pkt, GCE_SPR0, value, 0xFFFFFFFF);
#else
		cmdq_pkt_write(pkt, NULL, pa, value, ~0);
#endif
	}
}

void pp_write_mask(struct cmdq_pkt *pkt, u32 pa, u32 value, u32 mask)
{

	if (!pkt) {
		/* only update mask bit = 1, for 0 case, do not update. */
		u32 *va = hwtcon_hal_convert_pa_2_va(pa);
		bool register_remap = false;
		u32 read_back = 0;

		if (va == NULL) {
			va = ioremap(pa, sizeof(u32));
			register_remap = true;
		}
		read_back = pp_read(va);
		if (register_remap)
			iounmap(va);
		pp_write(pkt, pa, (read_back & ~mask) | (value & mask));
	} else {
		/* use gce */
#if 0
		cmdq_pkt_assign_command(pkt, GCE_SPR0, pa);
		cmdq_pkt_store_value(pkt, GCE_SPR0, value, mask);
#else
		cmdq_pkt_write(pkt, NULL, pa, value, mask);
#endif
	}
}

void pp_poll(struct cmdq_pkt *pkt, u32 pa, u32 value, u32 mask)
{
	/* TODO: */
}
