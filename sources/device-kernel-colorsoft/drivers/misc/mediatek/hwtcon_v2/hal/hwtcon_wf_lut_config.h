/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Jiaguang Zhang <jiaguang.zhang@mediatek.com>
 * Author: Xin Lin <xin.lin@mediatek.com>
 */

#ifndef __HWTCON_WF_LUT_CONFIG_H__
#define __HWTCON_WF_LUT_CONFIG_H__
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/irqreturn.h>
#include <linux/mailbox/mtk-cmdq-mailbox-ext.h>
#include "hwtcon_hal.h"

#define TEMPERATURE_NUM		32
#define WAVEFORM_MODE_TOTAL_NUM			16
#define WAVEFORM_4BITS_MODE_TOTAL_NUM			36
#define WAVEFORM_SIZE (3*1024*1024)
#define WAVEFORM_SIZE_4BIT_COLOR (9*1024*1024)
#define WAVEFORM_SIZE_4BIT_BW (7*1024*1024)

#define WAVEFORM_ADDR_OFFSET_TO_BEGIN	0xC0
#define WAVEFORM_4BITS_ADDR_OFFSET_TO_BEGIN	0x3C0
#define WAVEFORM_LEN_OFFSET_TO_BEGIN	0x8C0
#define WAVEFORM_4BITS_LEN_OFFSET_TO_BEGIN  0x1BC0

#define WAVEFORM_ADDR_OFFSET_PER_TEMP   0x40
#define WAVEFORM_4BITS_ADDR_OFFSET_PER_TEMP   0xC0

#define WAVEFORM_LEN_OFFSET_PER_TEMP	0x40
#define WAVEFORM_4BITS_LEN_OFFSET_PER_TEMP	0xC0

#define WAVEFORM_TS_TO_BEGIN			0x80
#define WAVEFORM_TS_NUM_TO_BEGIN		0xA2
#define MAX_WF_FILE_NAME 100
/* TL: The new waveform file: include swipe & du night mode
 * TB: the old waveform file: no swipe & du night mode
 */
#define WF_MODE_VERSION_TL 0x59
#define WF_MODE_VERSION_JB 0x71
#define WF_MODE_VERSION_TEST 0xFF

#define	WF_LUT_WDMA_ADD		0x15006000
/* WF_LUT_WDMA: 0x15006000 */
#define WDMA_INTEN               (WF_LUT_WDMA_ADD + 0x0)
#define WDMA_INTSTA              (WF_LUT_WDMA_ADD + 0x4)
#define WDMA_EN                  (WF_LUT_WDMA_ADD + 0x8)
#define WDMA_RST                 (WF_LUT_WDMA_ADD + 0xC)
#define WDMA_SMI_CON             (WF_LUT_WDMA_ADD + 0x10)
#define WDMA_CFG                 (WF_LUT_WDMA_ADD + 0x14)
#define WDMA_SRC_SIZE            (WF_LUT_WDMA_ADD + 0x18)
#define WDMA_CLIP_SIZE           (WF_LUT_WDMA_ADD + 0x1C)
#define WDMA_CLIP_COORD          (WF_LUT_WDMA_ADD + 0x20)
#define WDMA_DST_W_IN_BYTE       (WF_LUT_WDMA_ADD + 0x28)
#define WDMA_ALPHA               (WF_LUT_WDMA_ADD + 0x2C)
#define WDMA_BUF_CON1            (WF_LUT_WDMA_ADD + 0x38)
#define WDMA_BUF_CON2            (WF_LUT_WDMA_ADD + 0x3C)
#define WDMA_DST_UV_PITCH        (WF_LUT_WDMA_ADD + 0x78)
#define WDMA_DST_ADDR_OFFSET0    (WF_LUT_WDMA_ADD + 0x80)
#define WDMA_FLOW_CTRL_DBG       (WF_LUT_WDMA_ADD + 0xA0)
#define WDMA_EXEC_DBG            (WF_LUT_WDMA_ADD + 0xA4)
#define WDMA_CT_DBG              (WF_LUT_WDMA_ADD + 0xA8)
#define WDMA_SMI_TRAFFIC_DBG     (WF_LUT_WDMA_ADD + 0xAC)
#define WDMA_PROC_TRACK_DBG_0    (WF_LUT_WDMA_ADD + 0xB0)
#define WDMA_PROC_TRACK_DBG_1    (WF_LUT_WDMA_ADD + 0xB4)
#define WDMA_DEBUG               (WF_LUT_WDMA_ADD + 0xB8)
#define WDMA_DUMMY               (WF_LUT_WDMA_ADD + 0x100)
#define WDMA_DST_ADDR0           (WF_LUT_WDMA_ADD + 0xF00)

/* IMGSYS CONFIG: 0x15000000 */
#define	DISP_WDMA0_SEL_IN		0x15000F6C

enum OUTPUT_FORMAT_ENUM {
	OUTPUT_FORMAT_16BIT = 0,
	OUTPUT_FORMAT_8BIT = 1,
};

enum WF_8_16BIT_ENUM {
	WF_16BIT = 0,
	WF_8BIT = 1,
};

enum WF_TEST_FLAG_ENUM {
	WF_TEST_FLAG_NONE = 0,
	WF_TEST_FLAG_LUT_ASYNC = 1,
	WF_TEST_FLAG_CHECKSUM = 2,
	WF_TEST_FLAG_LUTINFO = 3,
	WF_TEST_FLAG_CONF_ADDR = 4,
	WF_TEST_FLAG_CHECK_SPARE = 5,
	WF_TEST_FLAG_AUTO = 6,
	WF_TEST_FLAG_TRIGGER_ALL = 7,
	WF_TEST_FLAG_CONTINUE_TRIGGER = 8,
	WF_TEST_FLAG_RESET = 9,
};

enum WF_WORK_MODE_ENUM {
	WF_WORK_MODE_INIT = 0,
	WF_WORK_MODE_A2,
	WF_WORK_MODE_4BIT_DAY,
	WF_WORK_MODE_4BIT_NIGHT,
	WF_WORK_MODE_DAY,
	WF_WORK_MODE_NIGHT,
	WF_WORK_MODE_COUNT,
};

enum WF_SLOT_ENUM {
	WF_SLOT_0 = 0,
	WF_SLOT_1,
	WF_SLOT_2,
	WF_SLOT_3,
	WF_SLOT_4,
	WF_SLOT_5,
	WF_SLOT_6,
	WF_SLOT_7,
	WF_SLOT_8,
	WF_SLOT_9,
	WF_SLOT_10,
	WF_SLOT_11,
	WF_SLOT_12,
	WF_SLOT_13,
	WF_SLOT_14,
	WF_SLOT_15,
	WF_SLOT_COUNT,
};

enum WF_FILE_MODE_ENUM {
	WF_FILE_MODE_INIT = 0,
	WF_FILE_MODE_DU = 1,
	WF_FILE_MODE_GC16 = 2,
	WF_FILE_MODE_GL16 = 3,
	WF_FILE_MODE_GLR16 = 4,
	/* 5 is empty */
	WF_FILE_MODE_A2 = 6,
	/* 7 is empty */
	WF_FILE_MODE_GCK16 = 8,
	WF_FILE_MODE_GLKW16 = 9,
	WF_FILE_MODE_GC16_PARTIAL = 10,
	WF_FILE_MODE_GCK16_PARTIAL = 11,
	WF_FILE_MODE_DUNM = 12,
	WF_FILE_MODE_P2SW = 13,
	WF_FILE_MODE_GCC16 = 14,
	WF_FILE_MODE_GLRC16 = 15,
	WF_FILE_MODE_GCCK16 = 16,
	WF_FILE_MODE_GLRCK16 = 17,
	WF_FILE_MODE_AUTO = 257,
};

enum WF_FILE_4BITS_MODE_ENUM {
	WF_FILE_4BITS_MODE_INIT = 0,
	WF_FILE_4BITS_MODE_INIT_1 = 1,
	WF_FILE_4BITS_MODE_DU = 2,
	WF_FILE_4BITS_MODE_DU_1 = 3,
	WF_FILE_4BITS_MODE_GC16 = 4,
	WF_FILE_4BITS_MODE_GC16_1 = 5,
	WF_FILE_4BITS_MODE_GL16 = 6,
	WF_FILE_4BITS_MODE_GL16_1 = 7,
	WF_FILE_4BITS_MODE_GLR16 = 8,
	WF_FILE_4BITS_MODE_GLR16_1 = 9,
	/* 5 is empty:10 11 */
	WF_FILE_4BITS_MODE_A2 = 12,
	WF_FILE_4BITS_MODE_A2_1 = 13,
	/* 7 is empty:14 15 */
	WF_FILE_4BITS_MODE_GCK16 = 16,
	WF_FILE_4BITS_MODE_GCK16_1 = 17,
	WF_FILE_4BITS_MODE_GLKW16 = 18,
	WF_FILE_4BITS_MODE_GLKW16_1 = 19,
	WF_FILE_4BITS_MODE_GC16_PARTIAL = 20,
	WF_FILE_4BITS_MODE_GC16_PARTIAL_1 = 21,
	WF_FILE_4BITS_MODE_GCK16_PARTIAL = 22,
	WF_FILE_4BITS_MODE_GCK16_PARTIAL_1 = 23,
	WF_FILE_4BITS_MODE_DUNM = 24,
	WF_FILE_4BITS_MODE_DUNM_1 = 25,
	WF_FILE_4BITS_MODE_P2SW = 26,
	WF_FILE_4BITS_MODE_P2SW_1 = 27,
	WF_FILE_4BITS_MODE_GCC16 = 28,
	WF_FILE_4BITS_MODE_GCC16_1 = 29,
	WF_FILE_4BITS_MODE_GLRC16 = 30,
	WF_FILE_4BITS_MODE_GLRC16_1 = 31,
	WF_FILE_4BITS_MODE_GCCK16 = 32,
	WF_FILE_4BITS_MODE_GCCK16_1 = 33,
	WF_FILE_4BITS_MODE_GLRCK16 = 34,
	WF_FILE_4BITS_MODE_GLRCK16_1 = 35,
};

enum WAVEFORM_4BITS_MODE_ENUM {
	WAVEFORM_MODE_INIT_1 = 512,
	WAVEFORM_MODE_DU_1,
	WAVEFORM_MODE_GC16_1,
	WAVEFORM_MODE_GL16_1,
	WAVEFORM_MODE_GLR16_1,
	WAVEFORM_MODE_REAGL_1,
	WAVEFORM_MODE_GLD16_1,
	WAVEFORM_MODE_A2_1,
	WAVEFORM_MODE_GCK16_1,
	WAVEFORM_MODE_GLKW16_1,
	WAVEFORM_MODE_GC16_PARTIAL_1,
	WAVEFORM_MODE_GCK16_PARTIAL_1,
	WAVEFORM_MODE_DUNM_1,
	WAVEFORM_MODE_P2SW_1,
	WAVEFORM_MODE_DU4_1,
	WAVEFORM_MODE_GCC16_1,
	WAVEFORM_MODE_GLRC16_1,
	WAVEFORM_MODE_GCCK16_1,
	WAVEFORM_MODE_GLRCK16_1,
};

enum WF_LUT_MOUT_ENUM {
	WF_LUT_MOUT_DPI = 0x01,
	WF_LUT_MOUT_WDMA = 0x02,
};

enum WF_FILE_OFFSET_ENUM {
	WF_OFFSET_MODE_VERSION = 0x50,
	WF_OFFSET_WF_TYPE = 0x53,
	WF_OFFSET_WFM_REV = 0x56,
};

struct wf_mode_map_t{
	enum WAVEFORM_MODE_ENUM wf_mode;
	enum WF_FILE_MODE_ENUM wf_file_mode;
};

struct wf_file_info {
	bool wf_file_ready;	/* parse the waveform file, ready to use */
	/* offset: 0x00 size: 64 bytes */
	char wf_file_name[MAX_WF_FILE_NAME];
	int mode_version;	/* offset: 0x50 size: 1 bytes */
	int wf_type;		/* offset: 0x53 size: 1 bytes */
	int wfm_rev;		/* offset: 0x56 size: 1 bytes */
};

struct wf_mode_relation {
	enum WAVEFORM_MODE_ENUM wf_mode;
	enum WF_SLOT_ENUM slot;
	enum WF_FILE_MODE_ENUM wf_file_mode;
};

struct lut_info_para {
	u8 valid;
	u8 waveform_mode;
	u16 x;
	u16 y;
	u16 w;
	u16 h;
	//insert position 0xFFFF replace not open this judgement
	u16 frame_insert;
	//record frame index
	u16 frame_index;
};

struct update_info_para {
	u64 cur_status;
	u32 merge_x;
	u32 merge_y;
	u32 merge_w;
	u32 merge_h;
	u32 frame_index[64];
	u32 frame_mode[64];
};


struct region_info {
	u16 x;
	u16 y;
	u16 w;
	u16 h;
};


struct wf_lut_waveform {
	unsigned int start_addr;
	unsigned char *start_addr_va;
	unsigned int len;
	unsigned int waveform_mode;
	unsigned int temperature_zone;
};

struct wf_lut_wb_rdma {
	unsigned int start_addr;
	unsigned int x;
	unsigned int y;
	unsigned int width;
	unsigned int height;
};

struct wf_lut_con_config {
	unsigned int gray_mode;
	unsigned int width;
	unsigned int height;
	unsigned int rdma_enable_mask;
	unsigned int DECFMT;	//decoder format 1T1pixel or 1T2pixel
	unsigned int layer_greq_num;
	unsigned int checksum_sel;
	unsigned int checksum_mode;
	unsigned int rg_lut_end_sel;
	unsigned int layer_smi_id_en;
	unsigned int checksum_en;
	unsigned int H_FLIP_EN;
	unsigned int V_FLIP_EN;
	unsigned int wf_lut_en;
	unsigned int wf_lut_inten;
	unsigned int base_addr;
	unsigned int base_addr1;
	unsigned int rg_8b_out;
	unsigned int rg_partial_up_en;
	unsigned int rg_partial_up_val;
	unsigned int rg_default_val;
	/* 1: WF_LUT->TCON. 2: WF_LUT->DRAM */
	enum WF_LUT_MOUT_ENUM wf_lut_mout;
	unsigned int byte_swap;
	unsigned int direct_link;
	unsigned int rg_de_sel;
	struct wf_lut_wb_rdma wb_rdma[4];
	unsigned int dual_mode;
};

u32 wf_lut_get_irq_status(void);
void wf_lut_clear_irq_status(struct cmdq_pkt *pkt);
unsigned int wf_lut_get_rdma0_checksum(void);
unsigned int wf_lut_get_rdma1_checksum(void);
unsigned int wf_lut_get_rdma2_checksum(void);
unsigned int wf_lut_get_rdma3_checksum(void);
unsigned int wf_lut_get_wf_lut_output_checksum(void);
void wf_lut_clear_lut_end_irq_status(struct cmdq_pkt *pkt);
void swtcon_config_context(struct cmdq_pkt *pkt);
void swdata_hwtcon_config_context(struct cmdq_pkt *pkt);
void wf_lut_waveform_select_by_temp(struct cmdq_pkt *pkt, int temp);

void hwtcon_edp_pinmux_control(struct platform_device *pdev);
void hwtcon_edp_pinmux_active(void);
void hwtcon_edp_pinmux_inactive(void);
void hwtcon_edp_pinmux_release(void);

unsigned int wf_lut_get_waveform_len(int temp, enum WAVEFORM_MODE_ENUM mode);

void wf_lut_get_waveform_mode_in_hardware(void);
void wf_lut_waveform_replace(struct cmdq_pkt *pkt,
			     enum WF_SLOT_ENUM hw_slot, enum WF_FILE_MODE_ENUM wf_file_mode);
int wf_lut_waveform_get_temperature_index(int temperature);

void wf_lut_waveform_slot_association(struct cmdq_pkt *pkt,
		enum WF_WORK_MODE_ENUM work_mode, unsigned int temp);

void wf_lut_common_8bit_setting(struct wf_lut_con_config *wf_lut_config);

void wf_lut_common_16bit_setting(struct wf_lut_con_config *wf_lut_config);

void wf_lut_config_lut_enable(struct cmdq_pkt *pkt,
		unsigned int lut0_value, unsigned int lut1_value);

u8 wf_lut_get_8bit(void);

u8 wf_lut_get_hflip(void);

u8 wf_lut_get_vflip(void);

void Wf_Lut_Wdma_addr(struct cmdq_pkt *pkt, unsigned int addr);

void wf_lut_wdma_clear_irq(void);
void wf_lut_config_show_picture_by_frame(int *lut_id_array, int counter,
		enum WF_SLOT_ENUM waveform_slot);
void wf_lut_config_context_init_for_pipeline(void);
void TS_WF_LUT_set_lut_info(struct cmdq_pkt *pkt,
		int x, int y, int w, int h, int lut_id, enum WF_SLOT_ENUM slot);

void wf_lut_set_lut_info(struct cmdq_pkt *pkt, int lut_id, int x, int y, int w, int h);
void wf_lut_waveform_table_init(void);
void wf_lut_set_lut_id_info(struct cmdq_pkt *pkt, int lut_id, enum WF_SLOT_ENUM lut_mode);
void wf_lut_all_end_clear_irq(void);
void wf_lut_config_context_test_without_trigger(struct cmdq_pkt *pkt,
		struct wf_lut_con_config *wf_lut_config,
		struct lut_info_para *lut_info);

void wf_lut_config_context_test_trigger(struct cmdq_pkt *pkt, struct lut_info_para *lut_info);
void wf_lut_common_16bit_setting_old(struct wf_lut_con_config *wf_lut_config);
irqreturn_t hwtcon_core_disp_rdma_irq_handle(int irq, void *dev);
irqreturn_t hwtcon_core_wf_lut_wdma_irq_handle(int irq, void *dev);
void TS_WF_LUT_disable_wf_lut(void);

irqreturn_t hwtcon_core_wf_lut_dpi_irq_handle(int irq, void *dev);

void Wf_Lut_Wdma_Config(struct cmdq_pkt *pkt, unsigned int width, unsigned int height);
void wf_lut_print_current_loaded_wavefrom_info(void);

enum WF_SLOT_ENUM wf_lut_get_waveform_mode_slot(enum WAVEFORM_MODE_ENUM wf_mode, int night_mode);
void wf_lut_parse_wf_file(char *waveform_va);
struct wf_file_info *wf_lut_get_wf_info(void);

#endif		/* __HWTCON_WF_LUT_CONFIG_H__ */
