/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 Amazon.com, Inc.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */
#ifndef __HWTCON_LIGHTBOX_H__
#define __HWTCON_LIGHTBOX_H__

#include "hwtcon_core.h"

int hwtcon_lightbox_ioctl_set_lightbox_ctrl(void *arg);
void hwtcon_lightbox_apply_lightbox(struct hwtcon_task *task);
void hwtcon_lightbox_init_static_value(void);

#endif		/* __HWTCON_LIGHTBOX_H__ */
