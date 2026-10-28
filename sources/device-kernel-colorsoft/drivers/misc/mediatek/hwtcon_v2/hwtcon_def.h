/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Jiaguang Zhang <jiaguang.zhang@mediatek.com>
 */

#ifndef __HWTCON_DEF_H__
#define __HWTCON_DEF_H__
#include <linux/printk.h>
#include <linux/types.h>

/* HWTCON error code */
enum HWTCON_STATUS {
	HWTCON_STATUS_OK = 0,
	HWTCON_STATUS_CREATE_FS_FAIL = -1,
	HWTCON_STATUS_FB_STRUCT_ALLOC_FAIL = -2,
	HWTCON_STATUS_FB_ALLOC_FAIL = -3,
	HWTCON_STATUS_INVALID_IOCTL_CMD = -4,
	HWTCON_STATUS_COPY_FROM_USER_FAIL = -5,
	HWTCON_STATUS_COPY_TO_USER_FAIL = -6,
	HWTCON_STATUS_INVALID_UPDATE_SCHEME = -7,
	HWTCON_STATUS_INVALID_WB_INDEX = -8,
	HWTCON_STATUS_INVALID_PARAM = -9,
	HWTCON_STATUS_GET_TASK_FAIL = -10,
	HWTCON_STATUS_REGION_NOT_MATCH = -11,
	HWTCON_STATUS_OPEN_FILE_FAIL = -12,
	HWTCON_STATUS_GET_RESOURCE_FAIL = -13,
	HWTCON_STATUS_OF_IOMAP_FAIL = -14,
	HWTCON_STATUS_CREAT_THREAD_FAIL = -15,
	HWTCON_STATUS_GET_IRQ_ID_FAIL = -16,
	HWTCON_STATUS_REGISTER_IRQ_FAIL = -17,
	HWTCON_STATUS_WAIT_TASK_STATE_TIMEOUT = -18,
	HWTCON_STATUS_PARSE_CLOCK_FAIL = -19,
	HWTCON_STATUS_ENABLE_CLOCK_FAIL = -20,

	HWTCON_STATUS_CREATE_GCE_CLT_FAIL = -21,
	HWTCON_STATUS_ALLOC_GCE_SLOT_FAIL = -22,
	HWTCON_STATUS_ALLOC_GCE_PKT_FAIL = -23,
	HWTCON_STATUS_CMDQ_INIT_FAIL = -24,
	HWTCON_STATUS_CMDQ_FLUSH_FAIL = -25,
	HWTCON_STATUS_PIPELINE_INFO_NOT_SYNC = -26,
};

enum HISTOGRAM_GREY_LEVEL {
	HISTOGRAM_GREY_LEVEL_Y2 = 0x40000001,
	HISTOGRAM_GREY_LEVEL_Y4 = 0x1010101,
	HISTOGRAM_GREY_LEVEL_Y8 = 0x11111111,
	HISTOGRAM_GREY_LEVEL_Y16 = 0x55555555,
};

enum HWTCON_ROTATE_ENUM {
	HWTCON_ROTATE_0 = 0,	/* 0 degree rotate */
	HWTCON_ROTATE_90 = 1,	/* 90 degree rotate */
	HWTCON_ROTATE_180 = 2,	/*180 degree rotate */
	HWTCON_ROTATE_270 = 3,	/* 270 degree rotate */
};

enum HWTCON_IRQ_TYPE {
	IRQ_PIPELINE_LUT_ILLEGAL = 0,
	IRQ_PIPELINE_REGION_ILLEGAL = 1,
	IRQ_PIPELINE_LUT_ASSIGN_DONE = 2,
	IRQ_PIPELINE_COLLISION = 3,
	IRQ_PIPELINE_PIXEL_LUT_COLLISION = 4,
	IRQ_WF_LUT_TCON_END = 5,
	IRQ_PIPELINE_DPI_UPDATE_DONE = 6,
	IRQ_PIPELINE_WB_FRAME_DONE = 7,
	IRQ_WF_LUT_FRAME_DONE = 8,
	IRQ_WF_LUT_RELEASE = 9,
	IRQ_DPI_SOF = 10,
	IRQ_HWTCON_MAX,
};

enum HWTCON_TASK_STATE {
	TASK_STATE_FREE = 0,
	/* acqure task done, wait for mdp process */
	TASK_STATE_WAIT_MDP_HANDLE = 1,
	/* begin to trigger mdp. */
	TASK_STAT_MDP_PROCESS = 2,
	/* end to trigger mdp. */
	TASK_STAT_MDP_DONE = 3,
	/* pipeline is processing */
	TASK_STATE_PIPELINE_PROCESS = 4,
	/* pipeline write done, wait wf_lut display done. */
	TASK_STATE_PIPELINE_DONE = 5,

	/* collision task */
	TASK_STATE_COLLISION = 6,
	/* wf_lut display done, OK to release task to free */
	/* same with TASK_STATE_FREE */
	TASK_STATE_SCROLLING = 7,
	TASK_STATE_DISPLAYED = 0,
};

#define HWTCON_WAIT_WF_LUT_RELEASE_TIMEOUT (5000)	/* ms */

#define AUTO_WAVEFORM_TABLE_CNT 25

#define HWTCON_DRIVER_NAME "hwtcon_v2"
#define HWTCON_TASK_TIMEOUT_MS 10000
#define HWTCON_TASK_WAIT_MARKER_TIMEOUT_MS 5000
#define HWTCON_IRQ_CLEAR_TIMEOUT_MS 10
#define HWTCON_TASK_MDP_COMPL_WAIT_MARKER_TIMEOUT_MS 100
#define HWTCON_MAX_QUEUE_ITEM 1000

#define HWTCON_TIME unsigned long long

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) > (b) ? (b) : (a))

/* control log level */
bool hwtcon_get_log_level(void);
bool hwtcon_get_epdc_debug_level(void);


/* print log to /proc/hwtcon/error */
void hwtcon_debug_err_printf(const char *print_msg, ...);

#define TCON_ERR_SAVE(string, args...) do {\
	hwtcon_debug_err_printf("[HWTCON ERR]"string" @%s,%u\n", \
		##args, __func__, __LINE__); \
	} while (0)

#define TCON_ERR(string, args...) \
	pr_notice("[HWTCON ERR]"string" @%s,%u\n", ##args, __func__, __LINE__)


#define TCON_WARN(string, args...) \
	pr_notice("[HWTCON WARN]"string" @%s,%u\n", ##args, __func__, __LINE__)

#define TCON_EPDC(string, args...) \
do { \
	if (hwtcon_get_epdc_debug_level()) \
		pr_notice("[HWTCON EPDC]"string"\n", ##args); \
} while (0)


#define TCON_LOG(string, args...) \
do { \
	if (hwtcon_get_log_level()) \
		pr_notice("[HWTCON LOG]"string"\n", ##args); \
} while (0)

#define EINK_DEFAULT_TEMPERATURE 25
#define EINK_DEFAULT_POWER_DOWN_TIME 0
#define EINK_NO_POWER_DOWN (-1)
#define MAX_LUT_REGION_COUNT 63

#define BIT_ENABLE(x) (1 << (x))
#define BIT_DISABLE(x) (0 << (x))
#define BIT_USE_AUTO_SOF(x) (0 << (x))
#define BIT_USE_SW_SOF(x) (1 << (x))

/* frame buffer count: 2, for alternate buffer */
#define FB_VIRTUAL_FRAME_COUNT 2

#endif				/* __HWTCON_DEF_H__ */
