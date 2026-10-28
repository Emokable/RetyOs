// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Jiaguang Zhang <jiaguang.zhang@mediatek.com>
 */

#include <linux/types.h>
#include <linux/string.h>
#include "hwtcon_rect.h"
#include "hwtcon_def.h"

bool hwtcon_rect_check_valid(const struct rect *rect1)
{
	if (rect1->x < 0 ||
		rect1->y < 0 ||
		rect1->width <= 0 ||
		rect1->height <= 0) {
		TCON_ERR("invalid rectangle:[%d %d %d %d]",
			rect1->x,
			rect1->y,
			rect1->width,
			rect1->height);
		return false;
	}

	return true;
}

/* check if two rects have collision. */
bool hwtcon_rect_have_collision(const struct rect *rect0,
	const struct rect *rect1)
{
	return hwtcon_rect_calc_intersection(rect0, rect1, NULL);
}

/* calculate two rect's intersection */
bool hwtcon_rect_calc_intersection(const struct rect *rect0,
	const struct rect *rect1,
	struct rect *collision_rect)
{
	int collision_start_x = 0;
	int collision_start_y = 0;
	int collision_end_x = 0;
	int collision_end_y = 0;

	if (rect0->width == 0 ||
		rect0->height == 0 ||
		rect1->width == 0 ||
		rect1->height == 0)
		return false;

	collision_start_x = MAX(rect0->x, rect1->x);
	collision_start_y = MAX(rect0->y, rect1->y);
	collision_end_x = MIN(rect0->x + rect0->width,
		rect1->x + rect1->width);
	collision_end_y = MIN(rect0->y + rect0->height,
		rect1->y + rect1->height);

	if (collision_end_x - collision_start_x <= 0 ||
		collision_end_y - collision_start_y <= 0)
		return false;

	/* have intersection */
	if (collision_rect == NULL)
		return true;

	collision_rect->x = collision_start_x;
	collision_rect->y = collision_start_y;
	collision_rect->width = collision_end_x - collision_start_x;
	collision_rect->height = collision_end_y - collision_start_y;

	return true;
}

struct rects hwtcon_rect_minus_00(const struct rect *rect1,
	const struct rect *minus_rect)
{
	struct rect regions[MAX_RECT_SETS] = { {0} };
	int i = 0;
	struct rect rect_intersection = {0};
	struct rect *rect2 = &rect_intersection;
	struct rects result = {0};

	/* no intersection */
	if (!hwtcon_rect_calc_intersection(rect1, minus_rect, rect2))
		return result;

	/* left */
	regions[0].x = MIN(rect1->x, rect2->x);
	regions[0].y = MIN(rect1->y, rect2->y);
	regions[0].width = MAX(rect1->x, rect2->x) - regions[0].x;
	regions[0].height = MAX(rect1->y + rect1->height,
		rect2->y + rect2->height) - regions[0].y;

	/* upper */
	regions[1].x = MAX(rect1->x, rect2->x);
	regions[1].y = MIN(rect1->y, rect2->y);
	regions[1].width = MAX(rect1->x + rect1->width,
		rect2->x + rect2->width) - regions[1].x;
	regions[1].height = MAX(rect1->y,
		rect2->y) - regions[1].y;

	/* right */
	regions[2].x = MIN(rect1->x + rect1->width,
		rect2->x + rect2->width);
	regions[2].y = MAX(rect1->y, rect2->y);
	regions[2].width = MAX(rect1->x + rect1->width,
		rect2->x + rect2->width) - regions[2].x;
	regions[2].height = MAX(rect1->y + rect1->height,
		rect2->y + rect2->height) - regions[2].y;

	/* lower */
	regions[3].x = MAX(rect1->x, rect2->x);
	regions[3].y = MIN(rect1->y + rect1->height,
		rect2->y + rect2->height);
	regions[3].width = MIN(rect1->x + rect1->width,
		rect2->x + rect2->width) - regions[3].x;
	regions[3].height = MAX(rect1->y + rect1->height,
		rect2->y + rect2->height) - regions[3].y;

	for (i = 0; i < 4; i++)
		if (regions[i].width > 0 && regions[i].height > 0) {
			result.regions[result.count] = regions[i];
			result.count++;
		}
	return result;
}


struct rects hwtcon_rect_minus_10(const struct rects *rect_set,
	const struct rect *minus_rect)
{
	int i = 0;
	struct rects result = {0};

	for (i = 0; i < rect_set->count; i++) {
		struct rects ret = {0};

		if (hwtcon_rect_have_collision(&rect_set->regions[i], minus_rect)) {
			ret = hwtcon_rect_minus_00(&rect_set->regions[i], minus_rect);
			result = hwtcon_rect_unit_11(&ret, &result);
		} else	/* no collision */ {
			result = hwtcon_rect_unit_01(&rect_set->regions[i], &result);
		}
	}
	return result;
}


struct rects hwtcon_rect_minus_01(const struct rect *region,
	const struct rects *minus_rect_set)
{
	int i = 0;
	struct rects result = {0};

	result.count = 1;
	result.regions[0] = *region;
	for (i = 0; i < minus_rect_set->count; i++)
		result = hwtcon_rect_minus_10(&result, &minus_rect_set->regions[i]);
	return result;
}

bool hwtcon_rect_check_merge(const struct rect *rect1,
	const struct rect *rect2,
	struct rect *result)
{
	bool merge = false;
	int x1 = rect1->x;
	int y1 = rect1->y;
	int X1 = rect1->x + rect1->width;
	int Y1 = rect1->y + rect1->height;
	int x2 = rect2->x;
	int y2 = rect2->y;
	int X2 = rect2->x + rect2->width;
	int Y2 = rect2->y + rect2->height;

	if ((rect1->width == rect2->width) &&
		(rect1->x == rect2->x)) {
		if (((y2 >= y1) && (y2 <= Y1)) ||
			((Y2 >= y1) && (Y2 <= Y1))) {
			merge = true;
			result->x = MIN(x1, x2);
			result->y = MIN(y1, y2);
			result->width = rect1->width;
			result->height = MAX(Y1, Y2) - MIN(y1, y2);
		}
	}

	if ((rect1->height == rect2->height) &&
		(rect1->y == rect2->y)) {
		if (((x2 >= x1) && (x2 <= X1)) ||
			((X2 >= y1) && (X2 <= Y1))) {
			merge = true;
			result->x = MIN(x1, x2);
			result->y = MIN(y1, y2);
			result->width = MAX(X1, X2) -  MIN(x1, x2);
			result->height = rect1->height;
		}
	}

	return merge;
}

struct rects hwtcon_rect_unit_00(const struct rect *rect1, const struct rect *rect2)
{
	struct rects result = { 0 };
	struct rect merge_rect = { 0 };

	if (hwtcon_rect_check_merge(rect1, rect2, &merge_rect)) {
		result.count = 1;
		result.regions[0] = merge_rect;
	} else {
		result.count = 2;
		result.regions[0] = *rect1;
		result.regions[1] = *rect2;
	}
	return result;
}

struct rects hwtcon_rect_unit_01(const struct rect *rect1, const struct rects *rects)
{
	struct rects result = { 0 };
	struct rect merge_rect = *rect1;
	bool merge_flag[MAX_RECT_SETS] = { false };
	bool match = true;
	int i = 0;

	while (match == true) {
		match = false;
		for (i = 0; i < rects->count; i++) {
			if ((merge_flag[i] == false) &&
			    hwtcon_rect_check_merge(&merge_rect, &rects->regions[i], &merge_rect)) {
				merge_flag[i] = true;
				match = true;
			}
		}
	}

	result.count = 1;
	result.regions[0] = merge_rect;

	for (i = 0; i < rects->count; i++) {
		if (merge_flag[i] == false) {
			result.regions[result.count] = rects->regions[i];
			result.count++;
		}
	}

	if (result.count > MAX_RECT_SETS) {
		TCON_ERR("rect count overflow:%d %d",
			result.count, rects->count);
		dump_stack();
	}
	return result;
}

struct rects hwtcon_rect_unit_11(const struct rects *rects1, const struct rects *rects2)
{
	struct rects result = *rects1;
	int i = 0;

	for (i = 0; i < rects2->count; i++)
		result = hwtcon_rect_unit_01(&rects2->regions[i], &result);

	if (result.count > MAX_RECT_SETS) {
		TCON_ERR("rect count overflow:%d %d %d",
			result.count, rects1->count, rects2->count);
		dump_stack();
	}
	return result;
}


/* check rect1 & rect2 are same. */
bool hwtcon_rect_compare(const struct rect *rect1,
	const struct rect *rect2)
{
	if (rect1 && rect2) {
		return ((rect1->x == rect2->x) &&
			(rect1->y == rect2->y) &&
			(rect1->width == rect2->width) &&
			(rect1->height == rect2->height));
	}
	return false;
}

/* check if big rect contains small rect. */
bool hwtcon_rect_contain_00(const struct rect *big_rect,
	const struct rect *small_rect)
{
	if (!hwtcon_rect_check_valid(big_rect) ||
		!hwtcon_rect_check_valid(small_rect))
		return false;

	return (small_rect->x >= big_rect->x) &&
		(small_rect->x + small_rect->width <=
			big_rect->x + big_rect->width) &&
		(small_rect->y >= big_rect->y) &&
		(small_rect->y + small_rect->height <=
			big_rect->y + big_rect->height);
}

bool hwtcon_rect_contain_10(const struct rects *rects,
	const struct rect *small_rect)
{
	struct rects result = hwtcon_rect_minus_10(rects, small_rect);
	return result.count != 0;
}

bool hwtcon_rect_contain_01(const struct rect *big_rect,
	const struct rects *rects)
{
	struct rects result = hwtcon_rect_minus_01(big_rect, rects);
	return result.count != 0;
}

enum RECT_RELATION_ENUM hwtcon_rect_check_relationship(const struct rect *rect1,
	const struct rect *rect2,
	struct rect *merge_region)
{
	if (!hwtcon_rect_check_valid(rect1) ||
		!hwtcon_rect_check_valid(rect2)) {
		if (merge_region)
			memset(merge_region, 0, sizeof(struct rect));
		return RECT_RELATION_NON_INTERSECT;
	}

	if (hwtcon_rect_contain_00(rect1, rect2) ||
		hwtcon_rect_contain_00(rect2, rect1)) {
		if (merge_region) {
			int start_x = MIN(rect1->x, rect2->x);
			int start_y = MIN(rect1->y, rect2->y);
			int end_x = MAX(rect1->x + rect1->width,
					rect2->x + rect2->width);
			int end_y = MAX(rect1->y + rect1->height,
					rect2->y + rect2->height);

			merge_region->x = start_x;
			merge_region->y = start_y;
			merge_region->width = end_x - start_x;
			merge_region->height = end_y - start_y;
		}
		return RECT_RELATION_CONTAIN;
	}

	if (hwtcon_rect_calc_intersection(rect1, rect2, NULL)) {
		if (merge_region) {
			int start_x = MIN(rect1->x, rect2->x);
			int start_y = MIN(rect1->y, rect2->y);
			int end_x = MAX(rect1->x + rect1->width,
					rect2->x + rect2->width);
			int end_y = MAX(rect1->y + rect1->height,
					rect2->y + rect2->height);

			merge_region->x = start_x;
			merge_region->y = start_y;
			merge_region->width = end_x - start_x;
			merge_region->height = end_y - start_y;
		}
		return RECT_RELATION_INTERSECT;
	}

	if (merge_region)
		memset(merge_region, 0, sizeof(struct rect));
	return RECT_RELATION_NON_INTERSECT;
}

/* Computes the collection of regions of two graphs*/
bool hwtcon_rect_merge_disjoint(const struct rect *rect0,
	const struct rect *rect1,
	struct rect *collision_rect)
{
	int collision_start_x = 0;
	int collision_start_y = 0;
	int collision_end_x = 0;
	int collision_end_y = 0;

	if (rect0->width == 0 ||
		rect0->height == 0 ||
		rect1->width == 0 ||
		rect1->height == 0)
		return false;

	collision_start_x = MIN(rect0->x, rect1->x);
	collision_start_y = MIN(rect0->y, rect1->y);
	collision_end_x = MAX(rect0->x + rect0->width,
		rect1->x + rect1->width);
	collision_end_y = MAX(rect0->y + rect0->height,
		rect1->y + rect1->height);

	if (collision_end_x - collision_start_x <= 0 ||
		collision_end_y - collision_start_y <= 0)
		return false;

	collision_rect->x = collision_start_x;
	collision_rect->y = collision_start_y;
	collision_rect->width = collision_end_x - collision_start_x;
	collision_rect->height = collision_end_y - collision_start_y;

	return true;
}

bool hwtcon_core_merge_scrolling_region(const struct rect *rect1,
	const struct rect *rect2,
	struct rect *merge_region)
{
	if (hwtcon_rect_merge_disjoint(rect1, rect2, merge_region) == true)
		return true;

	return false;
}


