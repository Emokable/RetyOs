/* SPDX-License-Identifier: GPL-2.0-only*/
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Jiaguang Zhang <jiaguang.zhang@mediatek.com>
 */

#ifndef __HWTCON_FB_H__
#define __HWTCON_FB_H__
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/pm_wakeup.h>
#include <linux/pm_qos.h>
#include <linux/pm_opp.h>
#include <linux/fb.h>
#include "hwtcon_ioctl_cmd.h"
#include "hwtcon_def.h"
#include "hwtcon_rect.h"
#include "hwtcon_pipeline_config.h"
#include <linux/slab.h>

#define FB_W_ALIGN 16
/* read temperature from sensor every 1 mins */
#define SENSOR_READ_INTERVAL_MS 60000
#define MAX_TEMP_READ  3
/* try it again in 1 second if the previous reading fails */
#define SENSOR_READ_SHORT_INTERVAL_MS	1000

#define MAX_DEBUG_IMAGE_BUFFER_COUNT 40
#define DEFAULT_DEBUG_IMAGE_BUFFER_COUNT 10
#define MAX_DEBUG_FB_BUFFER_COUNT 40
#define DEFAULT_DEBUG_FB_BUFFER_COUNT 10

#define MAX_FILE_NAME_LEN 100

#define ONE_GRAY_LEVEL 15
#define TRANSITION_THRESHOLD 49
#define STRENGTH_THREASHOLD 10
#define SCAN_LINE_TOTAL 10
#define SCAN_LINE_THRESHOLD 4
#define SCALED_FACTOR 375
#define MAX_INPUT_INDEX 2
#define PT_INT_MS 2000 /* minimum page turn interval */
#define MAX_GAP_MS 500 /* max time gap between updates belong to the same page turn */

struct hwtcon_task_list {
	spinlock_t lock;
	struct list_head list;
};

struct wf_lut_info {
	bool busy;
	struct rect region;
	int priority;
	enum WAVEFORM_MODE_ENUM waveform_mode;
	struct hwtcon_task *task;
};

struct hwtcon_cache_buf {
	struct device *dev;
	size_t size;
	char *alloc_va;
	dma_addr_t dma_handle;
	struct sg_table *sgt;
	unsigned int n_pages;
	struct page **pages;
};
/* data structure for sw mitigation (in-rush current reduction) */
struct sw_mitigation {
	__u32 pixel_thres;	/* percentage of pixel changed each line in % */
	__u32 count_thres;	/* line exceed above threshold in % */
	__u32 scaled_width;	/* resample width */
	__u32 scaled_height;	/* resample height */
	__u32 scan_lines;	/* lines to be scanned */
	/* average scanned line pixel value changed threshold in % */
	__u32 str_thres;
	/* used to calculate scaled width and height in %o */
	__u32 scaled_factor;
};
/* Store completed marker for MXCFB_WAIT_FOR_ANY_UPDATE_COMPLETE support */
struct completed_marker_store {
	__u32 marker[STORE_MARKER_NUM];
	struct mutex store_mutex;
	unsigned int num_marker;
	unsigned int head;
	unsigned int tail;
};

struct update_info {
	unsigned long jiffies;
	u32 width;
	u32 height;
	u32 time_gap;
};

struct update_hist {
	int index;
	struct update_info update[MAX_INPUT_INDEX];
};

struct color_scan_info {
	bool enable;
	bool *result_ptr;
	char *buffer_va;
	struct rect region;
};

struct fb_private_info {
	struct regulator *regulator_vcore;

	/* frame buffer info: MDP's input */
	struct hwtcon_cache_buf *fb_cache_buf;
	dma_addr_t fb_buffer_pa;
	char *fb_buffer_va;
	size_t fb_buffer_size;

	/* tmp buffer info*/
	dma_addr_t tmp_buffer_pa;
	char *tmp_buffer_va;
	size_t tmp_buffer_size;

	/* color buffer info: color mapping's output, MDP's input */
	struct hwtcon_cache_buf *color_cache_buf;
	dma_addr_t color_buffer_pa;
	char *color_buffer_va;
	size_t color_buffer_size;

	/* img buffer info: MDP's output, PIPELINE's input */

	struct hwtcon_cache_buf *img_cache_buf;
	dma_addr_t img_buffer_pa;
	char *img_buffer_va;
	size_t img_buffer_size;

	/* pre_img buffer info */
	struct hwtcon_cache_buf *pre_img_cache_buf;
	dma_addr_t pre_img_buffer_pa;
	char *pre_img_buffer_va;
	size_t pre_img_buffer_size;

	/* temp image buffer info: regal buffer */
	struct hwtcon_cache_buf *temp_img_cache_buf;
	dma_addr_t temp_img_buffer_pa;
	char *temp_img_buffer_va;
	size_t temp_img_buffer_size;

	/* regal buffer 2 for csr2 partial */
	struct hwtcon_cache_buf *partial_regal_cache_buf2;
	dma_addr_t partial_regal_buffer2_pa;
	char *partial_regal_buffer2_va;
	size_t partial_regal_buffer2_size;

	/* debug image buffer info:
	 * reserve 10 image buffers for buffer save.
	 * only for debug issue.
	 */
	dma_addr_t *debug_img_buffer_pa;
	char **debug_img_buffer_va;
	size_t *debug_img_buffer_size;
	char **debug_img_buffer_name;
	int debug_img_buffer_counter;
	bool debug_img_buffer_available;
	int debug_img_buffer_count;

	dma_addr_t *debug_fb_buffer_pa;
	char **debug_fb_buffer_va;
	size_t *debug_fb_buffer_size;
	char **debug_fb_buffer_name;
	int debug_fb_buffer_counter;
	bool debug_fb_buffer_available;
	int debug_fb_buffer_count;

	/* record LUT's picture order */
	u32 lut_pic_order[MAX_LUT_REGION_COUNT];
	spinlock_t lut_pic_order_lock;

	/* working buffer info */
	dma_addr_t wb_buffer_pa;
	char *wb_buffer_va;
	size_t wb_buffer_size;

	/* waveform buffer info */
	dma_addr_t waveform_pa;
	char *waveform_va;
	size_t waveform_size;

	/* LUT management */
	spinlock_t lut_free_lock;
	u64 lut_free;
	spinlock_t lut_active_lock;
	u64 lut_active;

	/* marker management: record every marker data in hwtcon. */
	struct hwtcon_task_list fb_global_marker_list;
	struct completion wb_frame_done_completion;
	struct completion wf_lut_release_completion;

	/* update sequence */
	spinlock_t g_update_order_lock;
	u32 g_update_order;
	u32 g_update_cnt;

	/* wake lock for pm_ops */
	struct wakeup_source *wake_lock;
	struct wakeup_source *wake_timeout_lock;

	/* Fake palette of 16 colors */
	u32 pseudo_palette[17];

	/* eink TEMPERATURE */
	int temperature;
	struct delayed_work read_sensor_work;

	/* first ioctl call */
	bool hwtcon_first_call;
	struct mutex hwtcon_first_call_mutex;

	/*record whether power enabled */
	struct mutex mmsys_power_enable_lock;
	bool mmsys_power_enable;

	/* record whether clock enabled */
	spinlock_t hwtcon_clk_enable_lock;
	bool hwtcon_clk_enable;

	/* add a lock for wf_lut debug reg */
	spinlock_t hwtcon_tcon_reg_lock;

	/* for mmsys power close. */
	struct timer_list mmsys_power_timer;
	/* power down delay time.
	 * time elapse need to power down after HW frame done.
	 */
	int power_down_delay_ms;

	/* current temperature zone */
	int current_temp_zone;

	/* current night mode */
	int current_night_mode;

	/* current wf work mode */
	int current_wf_work_mode;

	/* pipeline hw busy */
	bool pipeline_busy;

	/* ignore update request. */
	bool ignore_request;

	/* blank state */
	int blank;

	/* night mode or not */
	bool enable_night_mode;

	/* frequency for clear working buffer
	 * driver will clear working buffer
	 * when released LUT count > max_release_lut_count.
	 */
	int max_release_lut_count;

	/* handwriting scenario */
	bool stylus_detected;	/* pen detected */
	bool stylus_mode;	/* enable /disable stylus mode */
	bool stylus_enabled;	/* record whether stylus is enabled or not */
	bool wb_clearing;
	bool cfa_gain_enabled;
	/* add spin lock for valid_lut & trigger_lut */
	spinlock_t frame_lut_lock;
	/* WF_LUT start to show valid_lut in current frame */
	int frame_valid_lut;
	/* driver has triggered this lut id, will take effect on next DPI SOF */
	int frame_trigger_lut;
	/* add timer for each LUT update for error detect and recovery */
	struct timer_list timer_lut_release[MAX_LUT_REGION_COUNT];

	enum hwtcon_update_scheme update_scheme;

	/* all task list in hwtcon driver. */
	/* free task list */
	struct hwtcon_task_list free_task_list;
	/* acquire task done, wait for mdp process  */
	struct hwtcon_task_list wait_for_mdp_task_list;
	/* mdp is processing */
	struct hwtcon_task_list mdp_processing_task_list;

	/* mdp process done */
	struct hwtcon_task_list mdp_done_task_list;

	struct hwtcon_task *scrolling_task;
	spinlock_t scrolling_lock;
	bool task_in_scrolling;
	spinlock_t scrolling_enable_lock;
	/* pipeilne is processing */
	struct hwtcon_task_list pipeline_processing_task_list;
	/* pipeline process done */
	struct hwtcon_task_list pipeline_done_task_list;
	/* collision task list */
	struct hwtcon_task_list collision_task_list;

	/* wait queue for power state change */
	wait_queue_head_t power_state_change_wait_queue;

	/* wait queue for wait wf_lut release */
	wait_queue_head_t wf_lut_release_wait_queue;

	/* wait queue for trigger MDP & collision detect */
	wait_queue_head_t mdp_trigger_wait_queue;
	wait_queue_head_t pipeline_trigger_wait_queue;
	wait_queue_head_t clear_wb_wait_queue;

	/* wait for task state change to spefic state. */
	wait_queue_head_t task_state_wait_queue;

	/* work for power down mmsys */
	struct work_struct wk_power_down_mmsys;
	/* thread for power down mmsys domain */
	struct workqueue_struct *wq_power_down_mmsys;

	/* thread for handle pipeline work done */
	struct workqueue_struct *wq_pipeline_written_done;
	/* thread for handle wf_lut display done. */
	struct workqueue_struct *wq_wf_lut_display_done;

	struct mutex update_queue_mutex;
	struct mutex image_buffer_access_mutex;

	struct device *dev;

	struct sw_mitigation sw_algo;
	struct completed_marker_store marker_store;
	/*For convert large AUTO (GL16/DU to REAGL, in case not fast mode */
	u32 update_flag_fast_mode;
	struct update_hist hist;
	bool   tchbst_opt_enabled;
	struct color_scan_info color_scan;
};

int hwtcon_fb_register_fb(struct platform_device *pdev);
int hwtcon_fb_unregister_fb(struct platform_device *pdev);
struct fb_private_info *hwtcon_fb_info(void);
u32 hwtcon_fb_get_rotation(void);
void hwtcon_fb_get_resolution(u32 *width, u32 *height);
u32 hwtcon_fb_get_grayscale(void);
void hwtcon_fb_flush_update(void);
int hwtcon_fb_check_update_data_invalid(
	struct mxcfb_update_data *update_data);
int hwtcon_fb_get_virtual_width(void);
int hwtcon_fb_get_width(void);
int hwtcon_fb_get_height(void);

int hwtcon_fb_ioctl_set_update_flags(struct fb_info *info, unsigned long arg);
int hwtcon_fb_ioctl_get_update_flags(struct fb_info *info, unsigned long arg);
int hwtcon_fb_ioctl_wait_for_any_update_complete(struct fb_info *info, unsigned long arg);
int marker_store_reset(struct completed_marker_store *p_marker_store);
int marker_store_init(struct completed_marker_store *p_marker_store);
int store_completed_markers(struct completed_marker_store *p_marker_store, __u32 done_marker);
int complete_markers_avail(struct completed_marker_store *p_marker_store);
int auto_waveform_replacement(struct mxcfb_update_data *upd_data);
int hwtcon_fb_alloc_debug_img_buffer(struct fb_private_info *private_info);
int hwtcon_fb_alloc_debug_fb_buffer(struct fb_private_info *private_info);
struct hwtcon_cache_buf *hwtcon_fb_alloc_cached_mva_buf(struct device *dev,
	size_t size,
	const gfp_t flag);
void hwtcon_fb_free_cached_mva_buf(struct hwtcon_cache_buf *buf);
void hwtcon_fb_invalid_cache(struct hwtcon_cache_buf *buf);
void hwtcon_fb_clean_cache(struct hwtcon_cache_buf *buf);
u32 hwtcon_fb_get_cur_color_format(void);
int hwtcon_image_mmap(struct file *flip, struct vm_area_struct *vma);
int hwtcon_tmpbuf_mmap(struct file *flip, struct vm_area_struct *vma);
int hwtcon_pre_image_mmap(struct file *flip, struct vm_area_struct *vma);
int hwtcon_regal_buf2_mmap(struct file *flip, struct vm_area_struct *vma);

#endif /* __HWTCON_FB_H__ */
