// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Jiaguang Zhang <jiaguang.zhang@mediatek.com>
 */
#include <linux/types.h>
#include <linux/module.h>
#include <linux/soc/mediatek/mtk-cmdq-ext.h>
#include <dt-bindings/gce/mt8512-gce.h>

#include "hwtcon_cmdq.h"
#include "hwtcon_def.h"
#include "hwtcon_hal.h"
#include "hwtcon_reg.h"

#define GPT_TIMER_PA (0x10008068)

struct hwtcon_cmdq_info {
	/* buffer slot for cmdq */
	dma_addr_t slot_buffer_pa;
	u32 *slot_buffer_va;

	/* client */
	struct device *dev;
	struct cmdq_client *trigger_clt;
	struct cmdq_pkt *trigger_pkt;
	struct cmdq_client *pipeline_clt;
	struct cmdq_client *wf_lut_clt;
	bool trigger_start;
};

static struct hwtcon_cmdq_info gCmdqInfo;

struct cmdq_client *hwtcon_cmdq_get_pipeline_clt(void)
{
	return gCmdqInfo.pipeline_clt;
}

struct cmdq_client *hwtcon_cmdq_get_wf_lut_clt(void)
{
	return gCmdqInfo.wf_lut_clt;
}

int hwtcon_cmdq_init(struct device *dev)
{
	int status = 0;

	gCmdqInfo.dev = dev;
	gCmdqInfo.trigger_start = false;

	/* allocate gce client */
	gCmdqInfo.trigger_clt = cmdq_mbox_create(dev, 0);
	if (IS_ERR(gCmdqInfo.trigger_clt)) {
		pr_err("create trigger clt fail");
		return HWTCON_STATUS_CREATE_GCE_CLT_FAIL;
	}

	gCmdqInfo.pipeline_clt = cmdq_mbox_create(dev, 1);
	if (IS_ERR(gCmdqInfo.pipeline_clt)) {
		pr_err("create pipeline_clt fail");
		status = HWTCON_STATUS_CREATE_GCE_CLT_FAIL;
		goto MBOX_DESTROY_TRIGGER;
	}
	gCmdqInfo.wf_lut_clt = cmdq_mbox_create(dev, 2);
	if (IS_ERR(gCmdqInfo.wf_lut_clt)) {
		pr_err("create wf_lut_clt fail");
		status = HWTCON_STATUS_CREATE_GCE_CLT_FAIL;
		goto MBOX_DESTROY_PIPELINE;
	}

	/* allocate gce slot buffer */
	gCmdqInfo.slot_buffer_va = cmdq_mbox_buf_alloc(gCmdqInfo.trigger_clt,
			&gCmdqInfo.slot_buffer_pa);
	if (!gCmdqInfo.slot_buffer_va) {
		pr_err("create gce slot buffer fail");
		status = HWTCON_STATUS_ALLOC_GCE_SLOT_FAIL;
		goto MBOX_DESTROY_WF_LUT;
	}
	return HWTCON_STATUS_OK;
MBOX_DESTROY_WF_LUT:
	cmdq_mbox_destroy(gCmdqInfo.wf_lut_clt);
MBOX_DESTROY_PIPELINE:
	cmdq_mbox_destroy(gCmdqInfo.pipeline_clt);
MBOX_DESTROY_TRIGGER:
	cmdq_mbox_destroy(gCmdqInfo.trigger_clt);
	return status;
}

void hwtcon_cmdq_destroy(void)
{
	if (gCmdqInfo.slot_buffer_va) {
		cmdq_mbox_buf_free(gCmdqInfo.trigger_clt,
				gCmdqInfo.slot_buffer_va, gCmdqInfo.slot_buffer_pa);
		gCmdqInfo.slot_buffer_va = NULL;
		gCmdqInfo.slot_buffer_pa = 0;
	}
	if (gCmdqInfo.trigger_clt) {
		cmdq_mbox_destroy(gCmdqInfo.trigger_clt);
		gCmdqInfo.trigger_clt = NULL;
	}
	if (gCmdqInfo.pipeline_clt) {
		cmdq_mbox_destroy(gCmdqInfo.pipeline_clt);
		gCmdqInfo.pipeline_clt = NULL;
	}
	if (gCmdqInfo.wf_lut_clt) {
		cmdq_mbox_destroy(gCmdqInfo.wf_lut_clt);
		gCmdqInfo.wf_lut_clt = NULL;
	}

	if (gCmdqInfo.trigger_pkt) {
		cmdq_pkt_destroy(gCmdqInfo.trigger_pkt);
		gCmdqInfo.trigger_pkt = NULL;
	}
}

int hwtcon_cmdq_write_lut_release_slot(int index, u32 value)
{
	if (!gCmdqInfo.slot_buffer_va)
		return HWTCON_STATUS_CMDQ_INIT_FAIL;
	gCmdqInfo.slot_buffer_va[index] = value;
	return 0;
}

int hwtcon_cmdq_read_lut_release_slot(int index, u32 *pReadBackVal)
{
	if (!gCmdqInfo.slot_buffer_va)
		return HWTCON_STATUS_CMDQ_INIT_FAIL;
	*pReadBackVal = gCmdqInfo.slot_buffer_va[index];
	return 0;
}

static struct cmdq_pkt *hwtcon_cmdq_build_lut_release_cmd(void)
{
	struct cmdq_pkt *pkt = cmdq_pkt_create(gCmdqInfo.trigger_clt);


	/* pseudocode: C
	 * u32 value0, value1;
	 * value0 = readl(WF_LUT_END_IRQ_STA0);
	 * for (int i = 0; i < 32; i++) {
	 *      if (value0 & (1 << i))
	 *              gCmdqInfo.slot_buffer_va[i] = 1;
	 * }
	 * writel(value0, WF_LUT_END_IRQ_STA0);
	 *
	 * value1 = readl(WF_LUT_END_IRQ_STA1);
	 * for (int i = 0; i < 31; i++) {
	 *      if (value1 & (1 << i))
	 *              gCmdqInfo.slot_buffer_va[i+ 32] = 1;
	 * }
	 * writel(value1, WF_LUT_END_IRQ_STA1);
	 */

	/*
	 * pseudocode: GCE
	 * xPR_value0 = 0
	 * xPR_value1 = 0
	 * xPR_index = 0
	 * xPR_temp = 0
	 * xPR_JMP_POS_HANDLE_STA0_DONE = 0
	 * xPR_JMP_POS_CHECK_FAIL0 = 0
	 * xPR_JMP_POS_HANDLE_STA1_DONE = 0
	 * xPR_JMP_POS_CHECK_FAIL1 = 0
	 *
	 * xPR_value0 = gce_read WF_LUT_END_IRQ_STA0
	 * xPR_value1 = gce_read WF_LUT_END_IRQ_STA1
	 * xPR_index = 0
	 * JMP_POS_HANDLE_STA0:
	 * if (xPR_index >= 32) jump xPR_JMP_POS_HANDLE_STA0_DONE
	 *      // for xPR_index < 32 case
	 *      xPR_temp = xPR_value0 & (1 << i)
	 *      if (xPR_temp == 0) jump xPR_JMP_POS_CHECK_FAIL0
	 *              // check pass
	 *              gCmdqInfo.slot_buffer_va[i] = 1;
	 *      xPR_JMP_POS_CHECK_FAIL0:
	 *      xPR_index += 1
	 *      jump JMP_POS_HANDLE_STA0
	 * xPR_JMP_POS_HANDLE_STA0_DONE:
	 * xPR_index = 0
	 * xPR_temp = 0
	 * JMP_POS_HANDLE_STA1:
	 * if (xPR_index >= 31) jump xPR_JMP_POS_HANDLE_STA1_DONE
	 *      // for xPR_index < 31 case
	 *      xPR_temp = 1 << i
	 *      xPR_temp = xPR_value1 & xPR_temp
	 *      if (xPR_temp == 0) jump xPR_JMP_POS_CHECK_FAIL1
	 *              // check pass
	 *              gCmdqInfo.slot_buffer_va[i + 32] = 1;
	 *      xPR_JMP_POS_CHECK_FAIL1:
	 *      xPR_index += 1
	 *      jump xPR_JMP_POS_HANDLE_STA1
	 * xPR_JMP_POS_HANDLE_STA1_DONE
	 * writel(xPR_value0, WF_LUT_END_IRQ_STA0);
	 * writel(xPR_value1, WF_LUT_END_IRQ_STA1);
	 */

	u32 xPR_value0 = CMDQ_CPR_HWTCON_SLOT_00;
	u32 xPR_value1 = CMDQ_CPR_HWTCON_SLOT_01;
	u32 xPR_index = CMDQ_CPR_HWTCON_SLOT_02;
	u32 xPR_temp = CMDQ_CPR_HWTCON_SLOT_03;
	u32 xPR_JMP_POS_HANDLE_STA0_DONE = CMDQ_CPR_HWTCON_SLOT_04;
	u32 xPR_JMP_POS_CHECK_FAIL0 = CMDQ_CPR_HWTCON_SLOT_05;
	u32 xPR_JMP_POS_HANDLE_STA1_DONE = CMDQ_CPR_HWTCON_SLOT_06;
	u32 xPR_JMP_POS_CHECK_FAIL1 = CMDQ_CPR_HWTCON_SLOT_07;
	u32 xPR_slot_pa = CMDQ_CPR_HWTCON_SLOT_08;

	u64 *inst_JMP_POS_HANDLE_STA0_DONE = NULL;
	u64 *inst_JMP_POS_CHECK_FAIL0 = NULL;
	u64 *inst_JMP_POS_HANDLE_STA1_DONE = NULL;
	u64 *inst_JMP_POS_CHECK_FAIL1 = NULL;

	s32 mark_JMP_POS_HANDLE_STA0_DONE = 0;
	s32 mark_JMP_POS_CHECK_FAIL0 = 0;
	s32 mark_JMP_POS_HANDLE_STA1_DONE = 0;
	s32 mark_JMP_POS_CHECK_FAIL1 = 0;

	struct cmdq_operand l_op = { 0 };
	struct cmdq_operand r_op = { 0 };
	dma_addr_t JMP_POS_START = 0;
	dma_addr_t JMP_POS_HANDLE_STA0 = 0;
	dma_addr_t JMP_POS_HANDLE_STA1 = 0;

	JMP_POS_START = cmdq_pkt_get_pa_by_offset(pkt, pkt->cmd_buf_size);

	/* wait condition */
	cmdq_clear_event(gCmdqInfo.trigger_clt->chan, CMDQ_EVENT_DPI0_FRAME_DONE);
	cmdq_pkt_wfe(pkt, CMDQ_EVENT_DPI0_FRAME_DONE);

	cmdq_pkt_read(pkt, NULL, GPT_TIMER_PA, xPR_temp);
	cmdq_pkt_write_indriect(pkt, NULL, gCmdqInfo.slot_buffer_pa + 4 * LUT_RELEASE_TIME_START,
			xPR_temp, ~0);

	/* initialize gce xPR */
	cmdq_pkt_assign_command(pkt, xPR_value0, 0);
	cmdq_pkt_assign_command(pkt, xPR_value1, 0);
	cmdq_pkt_assign_command(pkt, xPR_index, 0);
	cmdq_pkt_assign_command(pkt, xPR_temp, 0);
	cmdq_pkt_assign_command(pkt, xPR_JMP_POS_HANDLE_STA0_DONE, 0);
	cmdq_pkt_assign_command(pkt, xPR_JMP_POS_CHECK_FAIL0, 0);
	cmdq_pkt_assign_command(pkt, xPR_JMP_POS_HANDLE_STA1_DONE, 0);
	cmdq_pkt_assign_command(pkt, xPR_JMP_POS_CHECK_FAIL1, 0);
	cmdq_pkt_assign_command(pkt, xPR_slot_pa, gCmdqInfo.slot_buffer_pa);

	/* xPR_value0 = gce_read WF_LUT_END_IRQ_STA0
	 * xPR_value1 = gce_read WF_LUT_END_IRQ_STA1
	 */
	cmdq_pkt_read(pkt, NULL, WF_LUT_END_IRQ_STA0, xPR_value0);
	cmdq_pkt_read(pkt, NULL, WF_LUT_END_IRQ_STA1, xPR_value1);

	// jump position: JMP_POS_HANDLE_STA0
	JMP_POS_HANDLE_STA0 = cmdq_pkt_get_pa_by_offset(pkt, pkt->cmd_buf_size);
	// if (xPR_index >= 32) jump xPR_JMP_POS_HANDLE_STA0_DONE
	mark_JMP_POS_HANDLE_STA0_DONE = pkt->cmd_buf_size;
	inst_JMP_POS_HANDLE_STA0_DONE = cmdq_pkt_get_curr_buf_va(pkt);
	cmdq_pkt_assign_command(pkt, xPR_JMP_POS_HANDLE_STA0_DONE, 0);
	l_op.reg = true;
	l_op.idx = xPR_index;
	r_op.reg = false;
	r_op.idx = 32;
	cmdq_pkt_cond_jump(pkt, xPR_JMP_POS_HANDLE_STA0_DONE, &l_op, &r_op,
			CMDQ_GREATER_THAN_AND_EQUAL);
	// fail case xPR_index < 32
	// xPR_temp = xPR_value0 & (1 << xPR_index)
	l_op.reg = false;
	l_op.idx = 1;
	r_op.reg = true;
	r_op.idx = xPR_index;
	cmdq_pkt_logic_command(pkt, CMDQ_LOGIC_LEFT_SHIFT, xPR_temp, &l_op, &r_op);
	l_op.reg = true;
	l_op.idx = xPR_value0;
	r_op.reg = true;
	r_op.idx = xPR_temp;
	cmdq_pkt_logic_command(pkt, CMDQ_LOGIC_AND, xPR_temp, &l_op, &r_op);
	// if (xPR_temp == 0) jump xPR_JMP_POS_CHECK_FAIL0
	mark_JMP_POS_CHECK_FAIL0 = pkt->cmd_buf_size;
	inst_JMP_POS_CHECK_FAIL0 = cmdq_pkt_get_curr_buf_va(pkt);
	cmdq_pkt_assign_command(pkt, xPR_JMP_POS_CHECK_FAIL0, 0);
	l_op.reg = true;
	l_op.idx = xPR_temp;
	r_op.reg = false;
	r_op.idx = 0;
	cmdq_pkt_cond_jump(pkt, xPR_JMP_POS_CHECK_FAIL0, &l_op, &r_op, CMDQ_EQUAL);
	// fail case xPR_temp != 0
	// gCmdqInfo.slot_buffer_va[xPR_index] = 1;
	l_op.reg = true;
	l_op.idx = xPR_index;
	r_op.reg = false;
	r_op.idx = 4;
	cmdq_pkt_logic_command(pkt, CMDQ_LOGIC_MULTIPLY, xPR_temp, &l_op, &r_op);
	l_op.reg = true;
	l_op.idx = xPR_temp;
	r_op.reg = true;
	r_op.idx = xPR_slot_pa;
	cmdq_pkt_logic_command(pkt, CMDQ_LOGIC_ADD, xPR_temp, &l_op, &r_op);
	cmdq_pkt_write_mem_with_cpr(pkt, xPR_temp, 1);

	// true case xPR_temp == 0
	*inst_JMP_POS_CHECK_FAIL0 |=
		CMDQ_REG_SHIFT_ADDR((s32) pkt->cmd_buf_size - mark_JMP_POS_CHECK_FAIL0 -
				CMDQ_INST_SIZE);
	// xPR_index += 1
	l_op.reg = true;
	l_op.idx = xPR_index;
	r_op.reg = false;
	r_op.idx = 1;
	cmdq_pkt_logic_command(pkt, CMDQ_LOGIC_ADD, xPR_index, &l_op, &r_op);
	cmdq_pkt_jump_addr(pkt, JMP_POS_HANDLE_STA0);

	// true case xPR_index >= 32
	*inst_JMP_POS_HANDLE_STA0_DONE |=
		CMDQ_REG_SHIFT_ADDR((s32) pkt->cmd_buf_size - mark_JMP_POS_HANDLE_STA0_DONE -
				CMDQ_INST_SIZE);

	cmdq_pkt_assign_command(pkt, xPR_index, 0);
	cmdq_pkt_assign_command(pkt, xPR_temp, 0);

	// jump position: JMP_POS_HANDLE_STA1
	JMP_POS_HANDLE_STA1 = cmdq_pkt_get_pa_by_offset(pkt, pkt->cmd_buf_size);
	// if (xPR_index >= 31) jump xPR_JMP_POS_HANDLE_STA1_DONE
	mark_JMP_POS_HANDLE_STA1_DONE = pkt->cmd_buf_size;
	inst_JMP_POS_HANDLE_STA1_DONE = cmdq_pkt_get_curr_buf_va(pkt);
	cmdq_pkt_assign_command(pkt, xPR_JMP_POS_HANDLE_STA1_DONE, 0);
	l_op.reg = true;
	l_op.idx = xPR_index;
	r_op.reg = false;
	r_op.idx = 31;
	cmdq_pkt_cond_jump(pkt, xPR_JMP_POS_HANDLE_STA1_DONE, &l_op, &r_op,
			CMDQ_GREATER_THAN_AND_EQUAL);
	// fail case xPR_index < 31
	// xPR_temp = xPR_value1 & (1 << xPR_index)
	l_op.reg = false;
	l_op.idx = 1;
	r_op.reg = true;
	r_op.idx = xPR_index;
	cmdq_pkt_logic_command(pkt, CMDQ_LOGIC_LEFT_SHIFT, xPR_temp, &l_op, &r_op);
	l_op.reg = true;
	l_op.idx = xPR_value1;
	r_op.reg = true;
	r_op.idx = xPR_temp;
	cmdq_pkt_logic_command(pkt, CMDQ_LOGIC_AND, xPR_temp, &l_op, &r_op);
	// if (xPR_temp == 0) jump xPR_JMP_POS_CHECK_FAIL1
	mark_JMP_POS_CHECK_FAIL1 = pkt->cmd_buf_size;
	inst_JMP_POS_CHECK_FAIL1 = cmdq_pkt_get_curr_buf_va(pkt);
	cmdq_pkt_assign_command(pkt, xPR_JMP_POS_CHECK_FAIL1, 0);
	l_op.reg = true;
	l_op.idx = xPR_temp;
	r_op.reg = false;
	r_op.idx = 0;
	cmdq_pkt_cond_jump(pkt, xPR_JMP_POS_CHECK_FAIL1, &l_op, &r_op, CMDQ_EQUAL);
	// fail case xPR_temp != 0
	// gCmdqInfo.slot_buffer_va[xPR_index + 32] = 1;
	l_op.reg = true;
	l_op.idx = xPR_index;
	r_op.reg = false;
	r_op.idx = 32;
	cmdq_pkt_logic_command(pkt, CMDQ_LOGIC_ADD, xPR_temp, &l_op, &r_op);
	l_op.reg = true;
	l_op.idx = xPR_temp;
	r_op.reg = false;
	r_op.idx = 4;
	cmdq_pkt_logic_command(pkt, CMDQ_LOGIC_MULTIPLY, xPR_temp, &l_op, &r_op);
	l_op.reg = true;
	l_op.idx = xPR_temp;
	r_op.reg = true;
	r_op.idx = xPR_slot_pa;
	cmdq_pkt_logic_command(pkt, CMDQ_LOGIC_ADD, xPR_temp, &l_op, &r_op);
	cmdq_pkt_write_mem_with_cpr(pkt, xPR_temp, 1);

	// true case xPR_temp == 0
	*inst_JMP_POS_CHECK_FAIL1 |=
		CMDQ_REG_SHIFT_ADDR((s32) pkt->cmd_buf_size - mark_JMP_POS_CHECK_FAIL1 -
				CMDQ_INST_SIZE);
	// xPR_index += 1
	l_op.reg = true;
	l_op.idx = xPR_index;
	r_op.reg = false;
	r_op.idx = 1;
	cmdq_pkt_logic_command(pkt, CMDQ_LOGIC_ADD, xPR_index, &l_op, &r_op);
	cmdq_pkt_jump_addr(pkt, JMP_POS_HANDLE_STA1);

	// true case xPR_index >= 31
	*inst_JMP_POS_HANDLE_STA1_DONE |=
		CMDQ_REG_SHIFT_ADDR((s32) pkt->cmd_buf_size - mark_JMP_POS_HANDLE_STA1_DONE -
				CMDQ_INST_SIZE);

	/* writel(xPR_value0, WF_LUT_END_IRQ_STA0);
	 * writel(xPR_value1, WF_LUT_END_IRQ_STA1);
	 */
	cmdq_pkt_write_indriect(pkt, NULL, WF_LUT_END_IRQ_CLR0, xPR_value0, ~0);
	cmdq_pkt_write(pkt, NULL, WF_LUT_END_IRQ_CLR0, 0, ~0);
	cmdq_pkt_write_indriect(pkt, NULL, WF_LUT_END_IRQ_CLR1, xPR_value1, ~0);
	cmdq_pkt_write(pkt, NULL, WF_LUT_END_IRQ_CLR1, 0, ~0);

	cmdq_pkt_read(pkt, NULL, GPT_TIMER_PA, xPR_temp);
	cmdq_pkt_write_indriect(pkt, NULL, gCmdqInfo.slot_buffer_pa + 4 * LUT_RELEASE_TIME_END,
				xPR_temp, ~0);

	cmdq_pkt_insert_eoc(pkt);
	// JUMP to START;
	cmdq_pkt_jump_addr(pkt, JMP_POS_START);
	/* mark pkt as loop */
	pkt->loop = true;

	return pkt;
}

int hwtcon_cmdq_lut_release_start_loop(cmdq_cb cb, struct cmdq_cb_data *cb_data)
{
	int status = 0;

	if (!gCmdqInfo.slot_buffer_va)
		return HWTCON_STATUS_CMDQ_INIT_FAIL;

	if (gCmdqInfo.trigger_start)
		return 0;

	/* build trigger loop command */
	if (!gCmdqInfo.trigger_pkt)
		gCmdqInfo.trigger_pkt = hwtcon_cmdq_build_lut_release_cmd();
	cmdq_mbox_enable(gCmdqInfo.trigger_clt->chan);
	status = cmdq_pkt_flush_async(gCmdqInfo.trigger_pkt, cb, cb_data);
	if (status < 0) {
		pr_err("cmdq_pkt_flush_async fail:%d", status);
		return HWTCON_STATUS_CMDQ_FLUSH_FAIL;
	}

	gCmdqInfo.trigger_start = true;
	return 0;
}

void hwtcon_cmdq_lut_release_stop_loop(void)
{
	if (gCmdqInfo.trigger_start) {
		pr_debug("stop gce lut_release loop");
		cmdq_mbox_stop(gCmdqInfo.trigger_clt);
		cmdq_mbox_disable(gCmdqInfo.trigger_clt->chan);
		cmdq_pkt_wait_complete(gCmdqInfo.trigger_pkt);
		cmdq_pkt_destroy(gCmdqInfo.trigger_pkt);
		gCmdqInfo.trigger_pkt = NULL;
		gCmdqInfo.trigger_start = false;
	}
}

MODULE_LICENSE("GPL v2");
