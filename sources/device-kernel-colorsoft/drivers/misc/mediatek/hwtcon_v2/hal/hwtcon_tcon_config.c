// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Jiaguang Zhang <jiaguang.zhang@mediatek.com>
 * Author: Xin Lin <xin.lin@mediatek.com>
 */

#include "hwtcon_tcon_config.h"
#include "hwtcon_fb.h"
#include "hwtcon_hal.h"
#include "hwtcon_epd.h"

void tcon_config_swtcon_pin(struct cmdq_pkt *pkt)
{

	/* T0 */
	pp_write_mask(pkt, PAPER_TCTOP_PIN_SWAP3,
		28 << 6,
		GENMASK(11, 6));

	/* T2 */
	pp_write_mask(pkt, PAPER_TCTOP_PIN_SWAP3,
		30 << 12,
		GENMASK(17, 12));

	/* T1 */
	pp_write_mask(pkt, PAPER_TCTOP_PIN_SWAP3,
		29 << 18,
		GENMASK(23, 18));
	/* T5 */
	pp_write_mask(pkt, PAPER_TCTOP_PIN_SWAP3,
		33 << 24,
		GENMASK(29, 24));

	/* T3 */
	pp_write_mask(pkt, PAPER_TCTOP_PIN_SWAP4,
		31,
		GENMASK(5, 0));

	/* T4 */
	pp_write_mask(pkt, PAPER_TCTOP_PIN_SWAP4,
		32 << 6,
		GENMASK(11, 6));
}

void tcon_config_pinmux_setting1(struct cmdq_pkt *pkt,
				 unsigned int original_value, unsigned int replace_value)
{
	unsigned int register_base = 0x00;
	unsigned int register_offset = 0x00;

	register_base = original_value / HWTCON_TCON_PINMUX_REGISTER_BIT_NUM;
	register_offset = original_value % HWTCON_TCON_PINMUX_REGISTER_BIT_NUM;

	pp_write_mask(pkt, PAPER_TCTOP_PIN_SWAP0 + 0x4 * register_base,
		      replace_value << (register_offset * HWTCON_TCON_PINMUX_ONE_BIT_OFFSET),
		      GENMASK((register_offset + 1) * HWTCON_TCON_PINMUX_ONE_BIT_OFFSET - 1,
			      register_offset * HWTCON_TCON_PINMUX_ONE_BIT_OFFSET));
}

void tcon_config_global_register(struct cmdq_pkt *pkt)
{
	pp_write(pkt, TCON_GR1, 0x0C000000);
	pp_write(pkt, TCON_GR0, 0x800003FF);
}

void tcon_config_timing0(struct cmdq_pkt *pkt, struct tcon_timing_para *timing_para)
{
	pp_write(pkt, TCON_TIM0R0,
		(timing_para->HE << 16) | timing_para->HS);
	pp_write(pkt, TCON_TIM0R3,
		(timing_para->VE << 16) | timing_para->VS);
	pp_write_mask(pkt, TCON_TIM0R6,
		timing_para->INV << 7,
		BIT_MASK(7));
}

void tcon_config_timing1(struct cmdq_pkt *pkt, struct tcon_timing_para *timing_para)
{
	pp_write(pkt, TCON_TIM1R0,
		(timing_para->HE << 16) | timing_para->HS);
	pp_write(pkt, TCON_TIM1R3,
		(timing_para->VE << 16) | timing_para->VS);
	pp_write_mask(pkt, TCON_TIM1R6,
		timing_para->INV << 7,
		BIT_MASK(7));
}

void tcon_config_timing2(struct cmdq_pkt *pkt, struct tcon_timing_para *timing_para)
{
	pp_write(pkt, TCON_TIM2R0,
		(timing_para->HE << 16) | timing_para->HS);
	pp_write(pkt, TCON_TIM2R3,
		(timing_para->VE << 16) | timing_para->VS);
	pp_write_mask(pkt, TCON_TIM2R1,
		((timing_para->HSPLCNT >> 8) & 0x00FF) |
		((timing_para->HSPLCNT & 0x00FF) << 8),
		GENMASK(15, 0));
	pp_write_mask(pkt, TCON_TIM2R4,
		timing_para->VACTSEL << 31,
		BIT_MASK(31));
	pp_write_mask(pkt, TCON_TIM2R6,
		timing_para->TCOPR,
		GENMASK(2, 0));
	pp_write_mask(pkt, TCON_TIM2R6,
		timing_para->INV << 7,
		BIT_MASK(7));
}

void tcon_config_timing3(struct cmdq_pkt *pkt, struct tcon_timing_para *timing_para)
{
	pp_write(pkt, TCON_TIM3R0,
		(timing_para->HE << 16) | timing_para->HS);
	pp_write(pkt, TCON_TIM3R3,
		(timing_para->VE << 16) | timing_para->VS);
	pp_write_mask(pkt, TCON_TIM3R6,
		timing_para->INV << 7,
		BIT_MASK(7));
}

#if 1
void tcon_config_timing4(struct cmdq_pkt *pkt, struct tcon_timing_para *timing_para)
{
	pp_write(pkt, TCON_TIM4R0,
		(timing_para->HE<<16) | timing_para->HS);
	pp_write(pkt, TCON_TIM4R3,
		(timing_para->VE<<16) | timing_para->VS);
	pp_write_mask(pkt, TCON_TIM4R1,
		((timing_para->HSPLCNT >> 8) & 0x00FF) |
		((timing_para->HSPLCNT & 0x00FF) << 8),
		GENMASK(15, 0));
	pp_write_mask(pkt, TCON_TIM4R4,
		timing_para->VACTSEL << 31,
		BIT_MASK(31));
	pp_write_mask(pkt, TCON_TIM4R6,
		timing_para->TCOPR,
		GENMASK(2, 0));
	pp_write_mask(pkt, TCON_TIM4R6,
		timing_para->INV << 7,
		BIT_MASK(7));
}
#endif

void tcon_config_timing5(struct cmdq_pkt *pkt, struct tcon_timing_para *timing_para)
{
	pp_write(pkt, TCON_TIM5R0,
		(timing_para->HE << 16) | timing_para->HS);
	pp_write(pkt, TCON_TIM5R3,
		(timing_para->VE << 16) | timing_para->VS);
	pp_write_mask(pkt, TCON_TIM5R1,
		((timing_para->HSPLCNT >> 8) & 0x00FF) |
		((timing_para->HSPLCNT & 0x00FF) << 8),
		GENMASK(15, 0));
	pp_write_mask(pkt, TCON_TIM5R4,
		timing_para->VACTSEL << 31,
		BIT_MASK(31));
	pp_write_mask(pkt, TCON_TIM5R6,
		timing_para->TCOPR,
		GENMASK(2, 0));
	pp_write_mask(pkt, TCON_TIM5R6,
		timing_para->INV << 7,
		BIT_MASK(7));
}

void tcon_config_timing6(struct cmdq_pkt *pkt, struct tcon_timing_para *timing_para)
{
	pp_write(pkt, TCON_TIM6R0,
		(timing_para->HE << 16) | timing_para->HS);
	pp_write(pkt, TCON_TIM6R3,
		(timing_para->VE << 16) | timing_para->VS);
	pp_write_mask(pkt, TCON_TIM6R1,
		((timing_para->HSPLCNT >> 8) & 0x00FF) |
		((timing_para->HSPLCNT & 0x00FF) << 8),
		GENMASK(15, 0));
	pp_write_mask(pkt, TCON_TIM6R4,
		timing_para->VACTSEL << 31,
		BIT_MASK(31));
	pp_write_mask(pkt, TCON_TIM6R6,
		timing_para->TCOPR,
		GENMASK(2, 0));
	pp_write_mask(pkt, TCON_TIM6R6,
		timing_para->INV << 7,
		BIT_MASK(7));
}

void tcon_config_timing7(struct cmdq_pkt *pkt, struct tcon_timing_para *timing_para)
{
	pp_write(pkt, TCON_TIM7R0,
		(timing_para->HE << 16) | timing_para->HS);
	pp_write(pkt, TCON_TIM7R3,
		(timing_para->VE << 16) | timing_para->VS);
	pp_write_mask(pkt, TCON_TIM7R1,
		((timing_para->HSPLCNT >> 8) & 0x00FF) |
		((timing_para->HSPLCNT & 0x00FF) << 8),
		GENMASK(15, 0));
	pp_write_mask(pkt, TCON_TIM7R4,
		timing_para->VACTSEL << 31,
		BIT_MASK(31));
	pp_write_mask(pkt, TCON_TIM7R6,
		timing_para->TCOPR,
		GENMASK(2, 0));
	pp_write_mask(pkt, TCON_TIM7R6,
		timing_para->INV << 7,
		BIT_MASK(7));
}

void tcon_config_timing8(struct cmdq_pkt *pkt, struct tcon_timing_para *timing_para)
{
	pp_write(pkt, TCON_TIM8R0,
		(timing_para->HE << 16) | timing_para->HS);
	pp_write(pkt, TCON_TIM8R3,
		(timing_para->VE << 16) | timing_para->VS);
	pp_write_mask(pkt, TCON_TIM8R1,
		((timing_para->HSPLCNT >> 8) & 0x00FF) |
		((timing_para->HSPLCNT & 0x00FF) << 8),
		GENMASK(15, 0));
	pp_write_mask(pkt, TCON_TIM8R4,
		timing_para->VACTSEL << 31,
		BIT_MASK(31));
	pp_write_mask(pkt, TCON_TIM8R6,
		timing_para->TCOPR,
		GENMASK(2, 0));
	pp_write_mask(pkt, TCON_TIM8R6,
		timing_para->INV << 7,
		BIT_MASK(7));
}

void tcon_config_timing9(struct cmdq_pkt *pkt,
	struct tcon_timing_para *timing_para)
{
	pp_write(pkt, TCON_TIM9R0,
		(timing_para->HE << 16) | timing_para->HS);
	pp_write(pkt, TCON_TIM9R3,
		(timing_para->VE << 16) | timing_para->VS);
	pp_write_mask(pkt, TCON_TIM9R1,
		((timing_para->HSPLCNT >> 8) & 0x00FF) |
		((timing_para->HSPLCNT & 0x00FF) << 8),
		GENMASK(15, 0));
	pp_write_mask(pkt, TCON_TIM9R4,
		timing_para->VACTSEL << 31,
		BIT_MASK(31));
	pp_write_mask(pkt, TCON_TIM9R6,
		timing_para->TCOPR,
		GENMASK(2, 0));
	pp_write_mask(pkt, TCON_TIM9R6,
		timing_para->INV << 7,
		BIT_MASK(7));
}

void tcon_setting(struct cmdq_pkt *pkt)
{
	tcon_config_timing0(pkt, hw_tcon_get_edp_tcon0_para());
	tcon_config_timing1(pkt, hw_tcon_get_edp_tcon1_para());
	tcon_config_timing2(pkt, hw_tcon_get_edp_tcon2_para());
	tcon_config_timing3(pkt, hw_tcon_get_edp_tcon3_para());
	tcon_config_timing4(pkt, hw_tcon_get_edp_tcon4_para());
	tcon_config_timing5(pkt, hw_tcon_get_edp_tcon5_para());
	tcon_config_timing6(pkt, hw_tcon_get_edp_tcon6_para());
	tcon_config_timing7(pkt, hw_tcon_get_edp_tcon7_para());
	tcon_config_timing8(pkt, hw_tcon_get_edp_tcon8_para());
	tcon_config_timing9(pkt, hw_tcon_get_edp_tcon9_para());
	if (hw_tcon_get_dual_mode()) {
		tcon_config_pinmux_setting1(pkt, 0, 14);
		tcon_config_pinmux_setting1(pkt, 1, 15);
		tcon_config_pinmux_setting1(pkt, 2, 6);
		tcon_config_pinmux_setting1(pkt, 3, 7);
		tcon_config_pinmux_setting1(pkt, 4, 12);
		tcon_config_pinmux_setting1(pkt, 5, 13);
		tcon_config_pinmux_setting1(pkt, 6, 4);
		tcon_config_pinmux_setting1(pkt, 7, 5);
		tcon_config_pinmux_setting1(pkt, 8, 10);
		tcon_config_pinmux_setting1(pkt, 9, 11);
		tcon_config_pinmux_setting1(pkt, 10, 2);
		tcon_config_pinmux_setting1(pkt, 11, 3);
		tcon_config_pinmux_setting1(pkt, 12, 8);
		tcon_config_pinmux_setting1(pkt, 13, 9);
		tcon_config_pinmux_setting1(pkt, 14, 0);
		tcon_config_pinmux_setting1(pkt, 15, 1);
	}
}


void tcon_disable(struct cmdq_pkt *pkt)
{
	pp_write(pkt, TCON_GR1, 0x00000000);
	pp_write(pkt, TCON_GR0, 0x00000000);
}
