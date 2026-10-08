/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "person_detection_mipi_metadata.h"

#include <errno.h>
#include <string.h>
#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define PERSON_DETECTION_MIPI_METADATA_MAX_LEN 1024U

static int32_t scale_float_to_milli(float value)
{
	return (int32_t)((value * 1000.0f) + ((value >= 0.0f) ? 0.5f : -0.5f));
}

static int metadata_append_char(char *buf, size_t buf_size, size_t *pos, char c)
{
	if ((buf == NULL) || (pos == NULL) || (*pos + 1U >= buf_size)) {
		return -EINVAL;
	}

	buf[*pos] = c;
	(*pos)++;
	buf[*pos] = '\0';
	return 0;
}

static int metadata_append_str(char *buf, size_t buf_size, size_t *pos, const char *src)
{
	if ((buf == NULL) || (pos == NULL) || (src == NULL)) {
		return -EINVAL;
	}

	while (*src != '\0') {
		int ret = metadata_append_char(buf, buf_size, pos, *src++);

		if (ret != 0) {
			return ret;
		}
	}

	return 0;
}

static int metadata_append_u32(char *buf, size_t buf_size, size_t *pos, uint32_t value)
{
	char tmp[10];
	size_t len = 0U;

	do {
		tmp[len++] = (char)('0' + (value % 10U));
		value /= 10U;
	} while ((value != 0U) && (len < sizeof(tmp)));

	while (len > 0U) {
		int ret = metadata_append_char(buf, buf_size, pos, tmp[--len]);

		if (ret != 0) {
			return ret;
		}
	}

	return 0;
}

static int metadata_append_i32(char *buf, size_t buf_size, size_t *pos, int32_t value)
{
	uint32_t magnitude;
	int ret;

	if (value < 0) {
		ret = metadata_append_char(buf, buf_size, pos, '-');
		if (ret != 0) {
			return ret;
		}
		magnitude = (uint32_t)(-(value + 1)) + 1U;
	} else {
		magnitude = (uint32_t)value;
	}

	return metadata_append_u32(buf, buf_size, pos, magnitude);
}

struct metadata_context {
	struct k_mutex lock;
	bool initialized;
	char latest_payload[PERSON_DETECTION_MIPI_METADATA_MAX_LEN];
	uint32_t latest_frame_sequence;
	uint32_t latest_invoke_time_ms;
	int latest_detection_count;
	detection_t latest_detections[MAX_DETECTIONS];
	uint8_t latest_usecase_id;
	bool valid;
};

static struct metadata_context app_ctx;

static void metadata_context_init(struct metadata_context *ctx)
{
	if (ctx->initialized) {
		return;
	}

	k_mutex_init(&ctx->lock);
	ctx->initialized = true;
}

int person_detection_mipi_metadata_update(
	uint32_t frame_sequence, uint8_t usecase_id,
	const struct person_detection_mipi_inference_result *result)
{
	struct metadata_context *ctx = &app_ctx;
	size_t pos = 0U;
	int ret;

	if (result == NULL) {
		return -EINVAL;
	}

	metadata_context_init(ctx);

	k_mutex_lock(&ctx->lock, K_FOREVER);
	memset(ctx->latest_payload, 0, sizeof(ctx->latest_payload));

	ret = metadata_append_str(ctx->latest_payload, sizeof(ctx->latest_payload), &pos,
				  "BoundingBox ");
	if (ret != 0) {
		goto out;
	}

	if (result->detection_count <= 0) {
		ret = metadata_append_char(ctx->latest_payload, sizeof(ctx->latest_payload), &pos, '[');
		if (ret != 0) {
			goto out;
		}

		ret = metadata_append_u32(ctx->latest_payload, sizeof(ctx->latest_payload), &pos,
					  frame_sequence);
		if (ret != 0) {
			goto out;
		}

		ret = metadata_append_str(ctx->latest_payload, sizeof(ctx->latest_payload), &pos,
					  ", 0.000, 0.000, 0.000, 0.000, 0.000, 0, ");
		if (ret != 0) {
			goto out;
		}

		ret = metadata_append_u32(ctx->latest_payload, sizeof(ctx->latest_payload), &pos,
					  usecase_id);
		if (ret != 0) {
			goto out;
		}

		ret = metadata_append_str(ctx->latest_payload, sizeof(ctx->latest_payload), &pos,
					  "] ");
		if (ret != 0) {
			goto out;
		}
	} else {
		for (int i = 0; i < result->detection_count; i++) {
			const detection_t *det = &result->detections[i];
			int32_t score_milli = scale_float_to_milli(det->score);
			int32_t x_milli = scale_float_to_milli(det->x);
			int32_t y_milli = scale_float_to_milli(det->y);
			int32_t w_milli = scale_float_to_milli(det->w);
			int32_t h_milli = scale_float_to_milli(det->h);

			ret = metadata_append_char(ctx->latest_payload, sizeof(ctx->latest_payload),
						   &pos, '[');
			if (ret != 0) {
				goto out;
			}

			ret = metadata_append_u32(ctx->latest_payload, sizeof(ctx->latest_payload),
					  &pos, frame_sequence);
			if (ret != 0) {
				goto out;
			}

			ret = metadata_append_str(ctx->latest_payload, sizeof(ctx->latest_payload),
					  &pos, ", ");
			if (ret != 0) {
				goto out;
			}

			ret = metadata_append_i32(ctx->latest_payload, sizeof(ctx->latest_payload),
					  &pos, score_milli);
			if (ret != 0) {
				goto out;
			}

			ret = metadata_append_str(ctx->latest_payload, sizeof(ctx->latest_payload),
					  &pos, ", ");
			if (ret != 0) {
				goto out;
			}

			ret = metadata_append_i32(ctx->latest_payload, sizeof(ctx->latest_payload),
					  &pos, x_milli);
			if (ret != 0) {
				goto out;
			}

			ret = metadata_append_str(ctx->latest_payload, sizeof(ctx->latest_payload),
					  &pos, ", ");
			if (ret != 0) {
				goto out;
			}

			ret = metadata_append_i32(ctx->latest_payload, sizeof(ctx->latest_payload),
					  &pos, y_milli);
			if (ret != 0) {
				goto out;
			}

			ret = metadata_append_str(ctx->latest_payload, sizeof(ctx->latest_payload),
					  &pos, ", ");
			if (ret != 0) {
				goto out;
			}

			ret = metadata_append_i32(ctx->latest_payload, sizeof(ctx->latest_payload),
					  &pos, w_milli);
			if (ret != 0) {
				goto out;
			}

			ret = metadata_append_str(ctx->latest_payload, sizeof(ctx->latest_payload),
					  &pos, ", ");
			if (ret != 0) {
				goto out;
			}

			ret = metadata_append_i32(ctx->latest_payload, sizeof(ctx->latest_payload),
					  &pos, h_milli);
			if (ret != 0) {
				goto out;
			}

			ret = metadata_append_str(ctx->latest_payload, sizeof(ctx->latest_payload),
					  &pos, ", ");
			if (ret != 0) {
				goto out;
			}

			ret = metadata_append_i32(ctx->latest_payload, sizeof(ctx->latest_payload),
					  &pos, result->detection_count);
			if (ret != 0) {
				goto out;
			}

			ret = metadata_append_str(ctx->latest_payload, sizeof(ctx->latest_payload),
					  &pos, ", ");
			if (ret != 0) {
				goto out;
			}

			ret = metadata_append_u32(ctx->latest_payload, sizeof(ctx->latest_payload),
					  &pos, usecase_id);
			if (ret != 0) {
				goto out;
			}

			ret = metadata_append_str(ctx->latest_payload, sizeof(ctx->latest_payload),
					  &pos, "] ");
			if (ret != 0) {
				goto out;
			}
		}
	}

	ret = metadata_append_str(ctx->latest_payload, sizeof(ctx->latest_payload), &pos, "END");
	if (ret != 0) {
		goto out;
	}

	ctx->latest_frame_sequence = frame_sequence;
	ctx->latest_invoke_time_ms = result->invoke_time_ms;
	ctx->latest_detection_count = result->detection_count;
	memset(ctx->latest_detections, 0, sizeof(ctx->latest_detections));
	if (result->detection_count > 0) {
		int copy_count = result->detection_count;

		if (copy_count > MAX_DETECTIONS) {
			copy_count = MAX_DETECTIONS;
		}

		memcpy(ctx->latest_detections, result->detections,
		       (size_t)copy_count * sizeof(detection_t));
	}
	ctx->latest_usecase_id = usecase_id;
	ctx->valid = true;

	if ((result->detection_count > 0) || (frame_sequence <= 3U) ||
	    ((frame_sequence % 30U) == 0U)) {
		printk("person_detection_mipi: metadata: seq=%u usecase=%u detections=%d invoke=%ums\n",
		       frame_sequence, usecase_id, result->detection_count,
		       result->invoke_time_ms);
	}

	ret = 0;

out:
	k_mutex_unlock(&ctx->lock);
	return ret;
}

void person_detection_mipi_metadata_reset(void)
{
	struct metadata_context *ctx = &app_ctx;

	metadata_context_init(ctx);

	k_mutex_lock(&ctx->lock, K_FOREVER);
	memset(ctx->latest_payload, 0, sizeof(ctx->latest_payload));
	ctx->latest_frame_sequence = 0U;
	ctx->latest_invoke_time_ms = 0U;
	ctx->latest_detection_count = 0;
	memset(ctx->latest_detections, 0, sizeof(ctx->latest_detections));
	ctx->latest_usecase_id = 0U;
	ctx->valid = false;
	k_mutex_unlock(&ctx->lock);
}

int person_detection_mipi_metadata_snapshot_get(
	struct person_detection_mipi_metadata_snapshot *snapshot)
{
	struct metadata_context *ctx = &app_ctx;

	if (snapshot == NULL) {
		return -EINVAL;
	}

	metadata_context_init(ctx);

	k_mutex_lock(&ctx->lock, K_FOREVER);
	snapshot->frame_sequence = ctx->latest_frame_sequence;
	snapshot->invoke_time_ms = ctx->latest_invoke_time_ms;
	snapshot->detection_count = ctx->latest_detection_count;
	memcpy(snapshot->detections, ctx->latest_detections, sizeof(snapshot->detections));
	snapshot->usecase_id = ctx->latest_usecase_id;
	snapshot->valid = ctx->valid;
	k_mutex_unlock(&ctx->lock);

	return 0;
}

int person_detection_mipi_metadata_get(char *dst, size_t dst_size)
{
	struct metadata_context *ctx = &app_ctx;
	int ret = 0;

	if ((dst == NULL) || (dst_size == 0U)) {
		return -EINVAL;
	}

	metadata_context_init(ctx);

	k_mutex_lock(&ctx->lock, K_FOREVER);
	if (!ctx->valid) {
		ret = -EAGAIN;
		goto out;
	}

	strncpy(dst, ctx->latest_payload, dst_size);
	dst[dst_size - 1U] = '\0';

out:
	k_mutex_unlock(&ctx->lock);
	return ret;
}
