// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Jiaguang Zhang <jiaguang.zhang@mediatek.com>
 * Author: Xin Lin <xin.lin@mediatek.com>
 */

#include "hwtcon_epd.h"
#include "hwtcon_driver.h"
#include "hwtcon_debug.h"
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>

#define HWTCON_EPD_DRIVER_NAME "epd"

struct edp_driver g_edp_driver;

static struct hwtcon_device_info_struct *hwtcon_device;

int hwtcon_epd_init_device_info(struct device_node *node)
{
	u32 count = 0, i = 0;
	hwtcon_device = hwtcon_device_info();
	if (of_property_read_u32(node, "cfa_panel", &g_edp_driver.cfa_panel)) {
		g_edp_driver.cfa_panel = 0;
		TCON_ERR("hwtcon parse dts for cfa_panel failed,use default 0, B/W");
	} else {
		TCON_ERR("hwtcon parse dts for cfa_panel:%d",
			g_edp_driver.cfa_panel);
	}
	if (g_edp_driver.cfa_panel) {
		of_property_read_u8(node, "cfa_array_width",
			&g_edp_driver.cfa_array_w);
		of_property_read_u8(node, "cfa_array_hight",
			&g_edp_driver.cfa_array_h);

		count = g_edp_driver.cfa_array_w * g_edp_driver.cfa_array_h;

		g_edp_driver.panel_cfa_array = (u8 *)kmalloc(count, GFP_KERNEL);
		of_property_read_u8_array(node, "panel_cfa_array",
			g_edp_driver.panel_cfa_array, count);
	}
	of_property_read_u32(node, "panel_dpi_clk",
		&g_edp_driver.panel_dpi_clk);
	of_property_read_u32(node, "panel_width",
		&g_edp_driver.panel_width);
	of_property_read_u32(node, "panel_height",
		&g_edp_driver.panel_height);
	of_property_read_u32(node, "panel_area_w",
		&g_edp_driver.panel_area_width);
	of_property_read_u32(node, "panel_area_h",
		&g_edp_driver.panel_area_height);
	of_property_read_u32(node, "panel_waveform_type",
		&g_edp_driver.panel_waveform_type);
	of_property_read_u32(node, "panel_output_8bit",
		&g_edp_driver.panel_output_8bit);
	if (of_property_read_u32(node, "1t2p", &g_edp_driver.enable_1t2p))
		g_edp_driver.enable_1t2p = 0;
	of_property_read_u32(node, "ddr_select",
		&g_edp_driver.ddr_select);
	of_property_read_u32(node, "dual_mode",
		&g_edp_driver.dual_mode);
	of_property_read_u32_array(node, "panel_tcon0",
			(unsigned int *)&g_edp_driver.panel_tcon[0], 8);
	of_property_read_u32_array(node, "panel_tcon1",
			(unsigned int *)&g_edp_driver.panel_tcon[1], 8);
	of_property_read_u32_array(node, "panel_tcon2",
			(unsigned int *)&g_edp_driver.panel_tcon[2], 8);
	of_property_read_u32_array(node, "panel_tcon3",
			(unsigned int *)&g_edp_driver.panel_tcon[3], 8);
	of_property_read_u32_array(node, "panel_tcon4",
			(unsigned int *)&g_edp_driver.panel_tcon[4], 8);
	of_property_read_u32_array(node, "panel_tcon5",
			(unsigned int *)&g_edp_driver.panel_tcon[5], 8);
	of_property_read_u32_array(node, "panel_tcon6",
			(unsigned int *)&g_edp_driver.panel_tcon[6], 8);
	of_property_read_u32_array(node, "panel_tcon7",
			(unsigned int *)&g_edp_driver.panel_tcon[7], 8);
	of_property_read_u32_array(node, "panel_tcon8",
			(unsigned int *)&g_edp_driver.panel_tcon[8], 8);
	of_property_read_u32_array(node, "panel_tcon9",
		(unsigned int *)&g_edp_driver.panel_tcon[9], 8);
	of_property_read_u32_array(node, "panel_dpi",
		(unsigned int *)&g_edp_driver.panel_dpi, 7);

	if (of_property_read_u32(node, "display_width", &g_edp_driver.display_width) ||
		g_edp_driver.display_width > g_edp_driver.panel_width) {
		g_edp_driver.display_width = g_edp_driver.panel_width;
	}
	if (of_property_read_u32(node, "display_height", &g_edp_driver.display_height) ||
		g_edp_driver.display_height > g_edp_driver.panel_height) {
		g_edp_driver.display_height = g_edp_driver.panel_height;
	}

	if (of_property_read_u32(node, "wb_buffer_start_x", &g_edp_driver.wb_buffer_start_x))
		g_edp_driver.wb_buffer_start_x = 0;
	if (of_property_read_u32(node, "wb_buffer_start_y", &g_edp_driver.wb_buffer_start_y))
		g_edp_driver.wb_buffer_start_y = 0;

	if (of_property_read_u32(node, "panel_hflip", &g_edp_driver.panel_hflip))
		g_edp_driver.panel_hflip = 0;

	TCON_ERR("panel_hflip = %d", g_edp_driver.panel_hflip);
	g_edp_driver.panel_rotate = 0;
	of_property_read_u32(node, "panel_rotate", &g_edp_driver.panel_rotate);
	g_edp_driver.panel_rotate %= 4;

	g_edp_driver.modify_wf_mode_vcounter = 0x400;
	of_property_read_u32(node, "modify_wf_mode_counter",
		&g_edp_driver.modify_wf_mode_vcounter);

	g_edp_driver.panel_material_type = 0x4;
	of_property_read_u32(node, "panel_material_type",
		&g_edp_driver.panel_material_type);
	if (g_edp_driver.cfa_panel) {
		TCON_LOG("It is a cfa panel");
		TCON_LOG("cfa_array_w:%d,cfa_array_h:%d",
			g_edp_driver.cfa_array_w, g_edp_driver.cfa_array_h);
		for (i = 0; i < count; i++)
			TCON_LOG("cfa_array[%d]:%d", i, g_edp_driver.panel_cfa_array[i]);
	} else
		TCON_LOG("It is a B/W panel");

	TCON_LOG("dpi_clk:%d,width:%d,height:%d,waveform_type:%d",
		g_edp_driver.panel_dpi_clk, g_edp_driver.panel_width,
		g_edp_driver.panel_height, g_edp_driver.panel_waveform_type);

	TCON_LOG("panel_material_type:%d",
		g_edp_driver.panel_material_type);
	TCON_LOG("ddr_select:%d, dual_mode:%d",
		g_edp_driver.ddr_select, g_edp_driver.dual_mode);

	TCON_LOG("panel_output_8bit:%d", g_edp_driver.panel_output_8bit);

	TCON_LOG("tcon0_VS:%d,tcon0_VE:%d,tcon0_HS:%d,tcon0_HE:%d",
		g_edp_driver.panel_tcon[0].VS, g_edp_driver.panel_tcon[0].VE,
		g_edp_driver.panel_tcon[0].HS, g_edp_driver.panel_tcon[0].HE);

	TCON_LOG("tcon0_TCOPR:%d,INV:%d,VACTSEL:%d,HSPLCNT:%d",
		g_edp_driver.panel_tcon[0].INV, g_edp_driver.panel_tcon[0].INV,
		g_edp_driver.panel_tcon[0].VACTSEL,
		g_edp_driver.panel_tcon[0].HSPLCNT);

	TCON_LOG("tcon1_VS:%d,tcon1_VE:%d,tcon1_HS:%d,tcon1_HE:%d",
		g_edp_driver.panel_tcon[1].VS, g_edp_driver.panel_tcon[1].VE,
		g_edp_driver.panel_tcon[1].HS, g_edp_driver.panel_tcon[1].HE);

	TCON_LOG("tcon1_TCOPR:%d,INV:%d,VACTSEL:%d,HSPLCNT:%d",
		g_edp_driver.panel_tcon[1].INV, g_edp_driver.panel_tcon[1].INV,
		g_edp_driver.panel_tcon[1].VACTSEL,
		g_edp_driver.panel_tcon[1].HSPLCNT);

	TCON_LOG("HSA:%d,HFP:%d,HBP:%d,VSA:%d,VFP:%d,VBP:%d,CK_POL:%d",
		g_edp_driver.panel_dpi.HSA, g_edp_driver.panel_dpi.HFP,
		g_edp_driver.panel_dpi.HBP, g_edp_driver.panel_dpi.VSA,
		g_edp_driver.panel_dpi.VFP, g_edp_driver.panel_dpi.VBP,
		g_edp_driver.panel_dpi.CK_POL);
	return 0;
}

u8 *hw_tcon_get_edp_cfa(void)
{
	return g_edp_driver.panel_cfa_array;
}
EXPORT_SYMBOL(hw_tcon_get_edp_cfa);
u32 hw_tcon_get_epd_type(void)
{
	return g_edp_driver.cfa_panel;
}
EXPORT_SYMBOL(hw_tcon_get_epd_type);
void hw_tcon_set_edp_width(u32 width)
{
	g_edp_driver.panel_width = width;
}

void hw_tcon_set_edp_height(u32 height)
{
	g_edp_driver.panel_height = height;
}


u32 hw_tcon_get_edp_clk(void)
{
	return g_edp_driver.panel_dpi_clk;
}

u8 hw_tcon_get_cfa_array_width(void)
{
	return g_edp_driver.cfa_array_w;
}
EXPORT_SYMBOL(hw_tcon_get_cfa_array_width);

u8 hw_tcon_get_cfa_array_hight(void)
{
	return g_edp_driver.cfa_array_h;
}
EXPORT_SYMBOL(hw_tcon_get_cfa_array_hight);
u32 hw_tcon_get_edp_width(void)
{
	return g_edp_driver.panel_width;
}
EXPORT_SYMBOL(hw_tcon_get_edp_width);

u32 hw_tcon_get_edp_height(void)
{
	return g_edp_driver.panel_height;
}
EXPORT_SYMBOL(hw_tcon_get_edp_height);

u32 hw_tcon_get_display_width(void)
{
	return g_edp_driver.display_width;
}
EXPORT_SYMBOL(hw_tcon_get_display_width);

u32 hw_tcon_get_display_height(void)
{
	return g_edp_driver.display_height;
}
EXPORT_SYMBOL(hw_tcon_get_display_height);

u32 hw_tcon_get_ddr_select(void)
{
	return g_edp_driver.ddr_select;
}

u32 hw_tcon_get_dual_mode(void)
{
	return g_edp_driver.dual_mode;
}

u32 hw_tcon_get_wb_buffer_start_x(void)
{
	return g_edp_driver.wb_buffer_start_x;
}

u32 hw_tcon_get_wb_buffer_start_y(void)
{
	return g_edp_driver.wb_buffer_start_y;
}

u32 hw_tcon_get_edp_area_width(void)
{
	return g_edp_driver.panel_area_width;
}

u32 hw_tcon_get_edp_area_height(void)
{
	return g_edp_driver.panel_area_height;
}

u32 hw_tcon_get_edp_rotate(void)
{
	return g_edp_driver.panel_rotate;
}

u32 hw_tcon_get_edp_out_8bit(void)
{
	return g_edp_driver.panel_output_8bit;
}

u32 hw_tcon_get_edp_material_type(void)
{
	return g_edp_driver.panel_material_type;
}

u32 hw_tcon_get_edp_blank_vtotal(void)
{
	return g_edp_driver.panel_dpi.VSA +
		g_edp_driver.panel_dpi.VFP +
		g_edp_driver.panel_dpi.VBP;
}

u32 hw_tcon_get_edp_blank_hsa(void)
{
	return g_edp_driver.panel_dpi.HSA;
}

u32 hw_tcon_get_edp_blank_vsa(void)
{
	return g_edp_driver.panel_dpi.VSA;
}
u32 hw_tcon_get_edp_dpi_cnt_off(void)
{
	int deviation = 0;

	if (hwtcon_driver_get_dpi_clock_rate() > g_edp_driver.panel_dpi_clk)
		deviation = hwtcon_driver_get_dpi_clock_rate() - g_edp_driver.panel_dpi_clk;
	else
		deviation = g_edp_driver.panel_dpi_clk - hwtcon_driver_get_dpi_clock_rate();

	/* Must set MMSYS clock to 312MHz */
	WARN(((hwtcon_driver_get_mmsys_clock_rate() != hwtcon_device->mmsys_clk_rate) ||
		(deviation >= g_edp_driver.panel_dpi_clk >> 10)),
		"Clock wrong MMSYS[%ld] DPI[%ld] panel_dpi_clk[%d]",
			hwtcon_driver_get_mmsys_clock_rate(),
			hwtcon_driver_get_dpi_clock_rate(),
			g_edp_driver.panel_dpi_clk);
	/* 6 dpi counter */
	return (g_edp_driver.panel_dpi.VFP * hwtcon_device->mmsys_clk_rate) /
		((hw_tcon_get_edp_blank_vtotal() + g_edp_driver.panel_height) * 85);
}

u32 hw_tcon_get_wf_lut_modify_vcounter(void)
{
	return g_edp_driver.modify_wf_mode_vcounter;
}

u32 hw_tcon_get_wf_lut_1t2p_mode(void)
{

	return g_edp_driver.enable_1t2p;
}

u32 hw_tcon_get_edp_hflip(void)
{
	return g_edp_driver.panel_hflip;
}

void hw_tcon_set_edp_hflip(u32 hflip)
{
	g_edp_driver.panel_hflip = hflip ? 1 : 0;
}

struct dpi_timing_para *hw_tcon_get_edp_dpi_para(void)
{
	return &g_edp_driver.panel_dpi;
}

struct tcon_timing_para *hw_tcon_get_edp_tcon0_para(void)
{
	return &g_edp_driver.panel_tcon[0];
}

struct tcon_timing_para *hw_tcon_get_edp_tcon1_para(void)
{
	return &g_edp_driver.panel_tcon[1];
}

struct tcon_timing_para *hw_tcon_get_edp_tcon2_para(void)
{
	return &g_edp_driver.panel_tcon[2];
}

struct tcon_timing_para *hw_tcon_get_edp_tcon3_para(void)
{
	return &g_edp_driver.panel_tcon[3];
}

struct tcon_timing_para *hw_tcon_get_edp_tcon4_para(void)
{
	return &g_edp_driver.panel_tcon[4];
}

struct tcon_timing_para *hw_tcon_get_edp_tcon5_para(void)
{
	return &g_edp_driver.panel_tcon[5];
}

struct tcon_timing_para *hw_tcon_get_edp_tcon6_para(void)
{
	return &g_edp_driver.panel_tcon[6];
}

struct tcon_timing_para *hw_tcon_get_edp_tcon7_para(void)
{
	return &g_edp_driver.panel_tcon[7];
}

struct tcon_timing_para *hw_tcon_get_edp_tcon8_para(void)
{
	return &g_edp_driver.panel_tcon[8];
}

struct tcon_timing_para *hw_tcon_get_edp_tcon9_para(void)
{
	return &g_edp_driver.panel_tcon[9];
}

