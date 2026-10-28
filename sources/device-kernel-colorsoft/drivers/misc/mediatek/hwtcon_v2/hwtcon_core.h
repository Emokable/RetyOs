/* SPDX-License-Identifier: GPL-2.0-only*/
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Jiaguang Zhang <jiaguang.zhang@mediatek.com>
 */

#ifndef __HWTCON_CORE_H__
#define __HWTCON_CORE_H__

#include <linux/types.h>
#include <linux/mxcfb.h>
#include "hwtcon_regal_config.h"
#include "hwtcon_pipeline_config.h"
#include "hwtcon_rect.h"
#include "hwtcon_def.h"
#include "hwtcon_fb.h"

extern u32 lut_frame_count[64];
extern bool lut_crc_all_zero[64];

#define MAX_PIC_ORDER 0xffffffff
#define MIN_PIC_ORDER 0

enum MARKER_STATE_ENUM {
	MARKER_STATE_LUT_NOT_ASSIGN = 0,
	MARKER_STATE_LUT_ASSIGNED = 1,
	MARKER_STATE_COLLISION = 2,
};

/* marker info */
struct update_marker_struct {
	struct list_head global_list;
	struct list_head task_list;
	u32 update_marker;

	/* used for wait submit done & wait display done */
	struct completion update_completion;
	struct completion submit_completion;
	struct completion mdp_completion;

	spinlock_t marker_lock;

	/* lut_mask: current marker depend on which LUT release
	 * has_collision: current marker has collision
	 * or not(collision handled).
	 * only when lut_mask == 0 & marker_state == MARKER_STATE_LUT_ASSIGNED
	 * driver can signal completion.
	 */
	u64 lut_mask;
	enum MARKER_STATE_ENUM marker_state;
	HWTCON_TIME start_time;

	/* for marker memory life cycle manage,
	 * make sure marker is released at right time
	 * waiters: userspace process is waiting this marker's completion if 1 or greater
	 * need_release: need to free this marker in waiter thread
	 */
	int waiters;
	bool need_release;
};

struct hwtcon_task {
	/* passed from userspace. */
	struct mxcfb_update_data update_data;

	/* update sequence */
	u32 update_order;

	/* record every task' picture order */
	u32 pic_order;

	/* regal setting */
	enum REGAL_STATUS_ENUM regal_status;
	enum REGAL_MODE_ENUM regal_mode;

	/* swipe setting */
	struct swipe_info_struct swipe_info;

	/* record pipeline info */
	struct pipeline_info pipeline_info;
	enum HWTCON_TASK_STATE state;

	/* task depend on which lut release
	 * bit x = 1: task depend on LUT x release
	 */
	u64 lut_dependency;
	u32 assign_lut;

	/* save current task's marker info */
	struct hwtcon_task_list marker_info_list;
	struct list_head list;

	/* unique ID */
	HWTCON_TIME unique_id;

	/* record time */
	HWTCON_TIME time_submit;
	HWTCON_TIME time_trigger_mdp;
	HWTCON_TIME time_mdp_done;
	HWTCON_TIME time_trigger_pipeline;
	HWTCON_TIME time_pipeline_done;
	HWTCON_TIME time_wait_fiti_power_good;
	HWTCON_TIME time_trigger_wf_lut;
	HWTCON_TIME time_wf_lut_done;

	HWTCON_TIME time_cfa_start, time_cfa_end;
	HWTCON_TIME time_prebuffer_start, time_prebuffer_end;
	HWTCON_TIME time_csr_start, time_csr_end;

	/* for work queue release task */
	struct work_struct work_written_done;
	struct work_struct work_display_done;

	/* add a timer for debug */
	struct timer_list mdp_debug_timer;
	/* if the update region contains color pixels */
	bool is_color;
	bool csr_need_write_back;
};

enum INSERT_LIST_ENUM {
	INSERT_TO_TAIL = 0,
	INSERT_TO_HEAD = 1,
};

enum GET_LUT_STATUS_ENUM {
	GET_LUT_OK = 0,
	GET_LUT_BUSY = -1,
	GET_LUT_ERR = -2,
	GET_LUT_TIMEOUT = -3,
};

#define MAX_RELEASED_LUT_COUNT 20

bool hwtcon_core_use_night_mode(void);
bool hwtcon_core_task_pending(void);
bool hwtcon_core_check_hwtcon_idle(void);
int hwtcon_core_wait_power_down(void);
char *hwtcon_core_get_wf_mode_name(enum WAVEFORM_MODE_ENUM mode);
int hwtcon_core_convert_bit_count_2_grey_level(u32 histogram);
int hwtcon_core_read_temperature(void);
int hwtcon_core_convert_temperature(int temp);
int hwtcon_core_submit_task(struct mxcfb_update_data *update_data);
int hwtcon_core_wait_for_task_triggered(u32 update_marker);
int hwtcon_core_wait_for_task_displayed(u32 update_marker);
u32 hwtcon_core_get_waveform_type(void);
bool hwtcon_core_string_ends_with_gz(char *file_name);
int hwtcon_core_load_init_setting_from_file(void);
int hwtcon_core_read_temp_zone(void);
int hwtcon_core_dispatch_pipeline(void *ignore);
int hwtcon_core_dispatch_mdp(void *ignore);
void hwtcon_core_handle_mmsys_power_down_cb(struct timer_list *timer);
void hwtcon_core_handle_mmsys_power_down(struct work_struct *work_item);
void hwtcon_core_handle_clock_disable(void);
void hwtcon_core_put_task(struct hwtcon_task *task);
void hwtcon_core_put_task_callback(struct hwtcon_task *task);
void hwtcon_core_put_task_with_lut_release(struct hwtcon_task *task);
int hwtcon_core_trigger_pipeline(struct hwtcon_task *task);
void hwtcon_core_handle_release_lut(int lut_id);
enum GET_LUT_STATUS_ENUM hwtcon_core_get_free_lut(bool *need_do_clear, int *acquired_id);
void hwtcon_core_dump_task_info(struct hwtcon_task *task);
void hwtcon_core_update_collision_list_on_release_lut(u64 released_lut);
struct rect hwtcon_core_rotate_region(const struct mxcfb_rect *src_region, u32 rotation);
struct rect hwtcon_core_get_update_data_region(const struct mxcfb_update_data *update_data);
struct rect hwtcon_core_get_task_region(const struct hwtcon_task *task);
struct rect hwtcon_core_get_mdp_region(const struct hwtcon_task *task);
u64 hwtcon_core_get_released_lut(void);
int hwtcon_core_wait_all_task_done(void);
int hwtcon_core_wait_all_wf_lut_release(void);
int hwtcon_core_get_task_count(struct list_head *header);
void hwtcon_core_config_timing(struct cmdq_pkt *pkt);
bool hwtcon_core_use_regal(struct mxcfb_update_data *update_data, enum REGAL_MODE_ENUM *regal_mode);
struct update_marker_struct *hwtcon_core_alloc_update_marker(void);
void hwtcon_core_handle_task_written_done(struct work_struct *work_item);
void hwtcon_core_handle_auto_waveform(struct hwtcon_task *task);
void hwtcon_core_reset_pipeline(void);
void hwtcon_core_reset_mmsys(void);
void hwtcon_core_get_mdp_input_buffer_info(struct hwtcon_task *task,
		u32 *buffer_pa, u32 *buffer_width,
		u32 *buffer_height);
void hwtcon_core_get_task_buffer_info(const struct hwtcon_task *task, u32 *buffer_pa,
		u32 *buffer_width, u32 *buffer_height);
void hwtcon_core_fiti_power_enable(bool enable);
 int hwtcon_core_wait_for_mdp_triggered(u32 update_marker);

/* stylus mode & other update can't coexist
 * userspace must ensure no pixel collision during stylus mode update.
 * will have 1 frame drop
 * any update during stylus enable will
 * use DU mode to update.(0 -> 15 / 15 -> 0)
 */
int hwtcon_core_enable_stylus_mode(void);
void hwtcon_core_disable_stylus_mode(void);

void hwtcon_core_handle_lut_release_timeout_cb(struct timer_list *timer);
void hwtcon_core_init_warn_flag(void);
struct rect  hwtcon_core_get_cfa_color_region(void);
char *hwtcon_core_get_cfa_input_address(void);
int hwtcon_core_use_cfa_color_mapping(struct hwtcon_task *task);
int hwtcon_core_use_cfa_color_and_regal(struct hwtcon_task *task);

void hwtcon_core_set_task_region(struct hwtcon_task *task, struct rect region);
int hwtcon_core_is_scrolling_task(struct hwtcon_task *task);
bool is_page_turn(void);
void color_detect(const uint8_t *start_addr, uint32_t width, uint32_t stride, uint32_t height, bool *is_color,
		uint8_t *c_info_addr);
void hwtcon_core_create_collision_task_for_csr_partial(struct hwtcon_task *task);

#endif				/* __HWTCON_CORE_H__ */
