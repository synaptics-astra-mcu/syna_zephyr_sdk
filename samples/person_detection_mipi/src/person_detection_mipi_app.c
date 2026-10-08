/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "person_detection_mipi_app.h"

#include <errno.h>
#include <stdbool.h>

#include "person_detection_mipi_capture.h"
#include "person_detection_mipi_inference.h"
#include "person_detection_mipi_metadata.h"
#include "person_detection_mipi_stream.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

enum person_detection_mipi_state {
	APP_STATE_UNINITIALIZED = 0,
	APP_STATE_CREATED,
	APP_STATE_RUNNING,
	APP_STATE_STOPPED,
};

struct person_detection_mipi_context {
	struct k_mutex lock;
	struct k_sem inference_ready_sem;
	struct k_sem inference_stopped_sem;
	struct k_thread inference_thread;
	enum person_detection_mipi_state state;
	bool stop_requested;
	bool inference_running;
	bool initialized;
};

static struct person_detection_mipi_context app_ctx;

K_THREAD_STACK_DEFINE(person_detection_mipi_inference_stack, 4096);

static void app_context_init(struct person_detection_mipi_context *ctx)
{
	if (ctx->initialized) {
		return;
	}

	k_mutex_init(&ctx->lock);
	k_sem_init(&ctx->inference_ready_sem, 0, 1);
	k_sem_init(&ctx->inference_stopped_sem, 0, 1);
	ctx->state = APP_STATE_UNINITIALIZED;
	ctx->initialized = true;
}

static void person_detection_mipi_inference_worker(void *arg1, void *arg2, void *arg3)
{
	struct person_detection_mipi_context *ctx = arg1;
	struct person_detection_mipi_capture_frame frame;
	struct person_detection_mipi_inference_result result;
	uint8_t *scratch_frame = person_detection_mipi_capture_scratch_frame_buffer();
	uint32_t last_sequence = 0U;
	uint32_t loops_without_frame = 0U;
	uint32_t loops_without_new_frame = 0U;
	uint32_t inference_count = 0U;
	bool ready_signaled = false;

	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	printk("person_detection_mipi: inference worker: started\n");

	while (true) {
		bool stop_requested;
		int ret;

		k_mutex_lock(&ctx->lock, K_FOREVER);
		stop_requested = ctx->stop_requested;
		k_mutex_unlock(&ctx->lock);

		if (stop_requested) {
			break;
		}

		ret = person_detection_mipi_capture_get_latest(&frame, scratch_frame,
							       PERSON_DETECTION_MIPI_MODEL_RAW_FRAME_SIZE);
		if (ret != 0) {
			loops_without_frame++;
			if ((loops_without_frame == 1U) || ((loops_without_frame % 100U) == 0U)) {
				printk("person_detection_mipi: inference worker: no frame yet (ret=%d loops=%u)\n",
				       ret, loops_without_frame);
			}
			k_msleep(10);
			continue;
		}
		loops_without_frame = 0U;

		if ((frame.sequence == 0U) || (frame.sequence == last_sequence) || !frame.valid) {
			loops_without_new_frame++;
			if ((loops_without_new_frame == 1U) ||
			    ((loops_without_new_frame % 100U) == 0U)) {
				printk("person_detection_mipi: inference worker: waiting for new frame (seq=%u last=%u valid=%d loops=%u)\n",
				       frame.sequence, last_sequence, frame.valid ? 1 : 0,
				       loops_without_new_frame);
			}
			k_msleep(10);
			continue;
		}
		loops_without_new_frame = 0U;

		printk("person_detection_mipi: inference worker: running frame seq=%u bytes=%u\n",
		       frame.sequence, frame.bytesused);

		ret = person_detection_mipi_inference_run_raw8_frame(frame.data, frame.bytesused,
								     &result);
		if (ret != 0) {
			printk("person_detection_mipi: inference worker: inference failed for seq=%u ret=%d\n",
			       frame.sequence, ret);
			continue;
		}

		inference_count++;
		printk("person_detection_mipi: inference worker: frame seq=%u done invoke=%ums detections=%d total=%u\n",
		       frame.sequence, result.invoke_time_ms, result.detection_count, inference_count);
		if (result.detection_count > 0) {
			for (int i = 0; i < result.detection_count; i++) {
				const detection_t *det = &result.detections[i];
				float x1 = det->x;
				float y1 = det->y;
				float x2 = det->x + det->w;
				float y2 = det->y + det->h;

				printk("UC Person detection - [Frame %u] Detection number %d: Class [%d], Box [%0.3f, %0.3f, %0.3f, %0.3f], Score [%0.3f]\n",
				       frame.sequence, i + 1, det->class_id,
				       (double)x1, (double)y1, (double)x2, (double)y2,
				       (double)det->score);
			}
		}

		ret = person_detection_mipi_metadata_update(frame.sequence,
							   CONFIG_PERSON_DETECTION_MIPI_USECASE_ID,
							   &result);
		if (ret != 0) {
			printk("person_detection_mipi: inference worker: metadata update failed for seq=%u ret=%d\n",
			       frame.sequence, ret);
			continue;
		}
		printk("person_detection_mipi: inference worker: metadata updated for seq=%u\n",
		       frame.sequence);

		last_sequence = frame.sequence;

		if (!ready_signaled) {
			ready_signaled = true;
			k_sem_give(&ctx->inference_ready_sem);
		}
	}

	k_mutex_lock(&ctx->lock, K_FOREVER);
	ctx->inference_running = false;
	k_mutex_unlock(&ctx->lock);

	printk("person_detection_mipi: inference worker: stopped after %u successful inference(s)\n",
	       inference_count);

	if (!ready_signaled) {
		k_sem_give(&ctx->inference_ready_sem);
	}

	k_sem_give(&ctx->inference_stopped_sem);
}

int person_detection_mipi_app_create(void)
{
	struct person_detection_mipi_context *ctx = &app_ctx;
	int ret;

	app_context_init(ctx);
	k_mutex_lock(&ctx->lock, K_FOREVER);

	if (ctx->state != APP_STATE_UNINITIALIZED) {
		k_mutex_unlock(&ctx->lock);
		return 0;
	}

	printk("person_detection_mipi: create: initializing inference\n");
	ret = person_detection_mipi_inference_init();
	if (ret != 0) {
		printk("person_detection_mipi: create: inference init failed: %d\n", ret);
		k_mutex_unlock(&ctx->lock);
		return ret;
	}

	printk("person_detection_mipi: create: initializing MIPI capture\n");
	ret = person_detection_mipi_capture_init();
	if (ret != 0) {
		printk("person_detection_mipi: create: MIPI capture init failed: %d\n", ret);
		k_mutex_unlock(&ctx->lock);
		return ret;
	}

	printk("person_detection_mipi: create: initializing JPEG stream\n");
	ret = person_detection_mipi_stream_init();
	if (ret != 0) {
		printk("person_detection_mipi: create: JPEG stream disabled: %d\n", ret);
	}

	person_detection_mipi_metadata_reset();

	ctx->state = APP_STATE_CREATED;

	k_mutex_unlock(&ctx->lock);
	printk("person_detection_mipi: create: complete\n");
	return 0;
}

int person_detection_mipi_app_start(void)
{
	struct person_detection_mipi_context *ctx = &app_ctx;
	int ret = 0;

	app_context_init(ctx);
	k_mutex_lock(&ctx->lock, K_FOREVER);

	if (ctx->state == APP_STATE_RUNNING) {
		k_mutex_unlock(&ctx->lock);
		return 0;
	}

	if (ctx->state == APP_STATE_STOPPED) {
		k_mutex_unlock(&ctx->lock);
		return person_detection_mipi_app_resume();
	}

	if (ctx->state != APP_STATE_CREATED) {
		k_mutex_unlock(&ctx->lock);
		return -EPERM;
	}

	k_mutex_unlock(&ctx->lock);

	ret = person_detection_mipi_stream_start();
	if (ret != 0) {
		printk("person_detection_mipi: start: JPEG stream disabled: %d\n", ret);
	}

	printk("person_detection_mipi: start: starting MIPI capture\n");
	ret = person_detection_mipi_capture_start();
	if (ret != 0) {
		(void)person_detection_mipi_stream_stop();
		printk("person_detection_mipi: start: MIPI capture failed: %d\n", ret);
		return ret;
	}

	person_detection_mipi_metadata_reset();

	k_mutex_lock(&ctx->lock, K_FOREVER);
	ctx->stop_requested = false;
	ctx->inference_running = true;
	k_sem_reset(&ctx->inference_ready_sem);
	k_sem_reset(&ctx->inference_stopped_sem);
	k_mutex_unlock(&ctx->lock);

	k_thread_create(&ctx->inference_thread, person_detection_mipi_inference_stack,
			K_THREAD_STACK_SIZEOF(person_detection_mipi_inference_stack),
			person_detection_mipi_inference_worker, ctx, NULL, NULL,
			CONFIG_HOST_API_TASK_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&ctx->inference_thread, "pd_mipi_infer");

	printk("person_detection_mipi: start: waiting for first inference\n");
	ret = k_sem_take(&ctx->inference_ready_sem, K_SECONDS(5));
	if (ret != 0) {
		printk("person_detection_mipi: start: first inference timeout: %d\n", ret);
		k_mutex_lock(&ctx->lock, K_FOREVER);
		ctx->stop_requested = true;
		k_mutex_unlock(&ctx->lock);
		(void)person_detection_mipi_stream_stop();
		(void)person_detection_mipi_capture_stop();
		(void)k_sem_take(&ctx->inference_stopped_sem, K_SECONDS(5));
		return ret;
	}

	k_mutex_lock(&ctx->lock, K_FOREVER);
	ctx->state = APP_STATE_RUNNING;
	k_mutex_unlock(&ctx->lock);
	printk("person_detection_mipi: start: complete\n");
	return 0;
}

int person_detection_mipi_app_stop(void)
{
	struct person_detection_mipi_context *ctx = &app_ctx;
	int ret = 0;

	app_context_init(ctx);
	k_mutex_lock(&ctx->lock, K_FOREVER);

	if (ctx->state == APP_STATE_STOPPED) {
		k_mutex_unlock(&ctx->lock);
		return 0;
	}

	if (ctx->state != APP_STATE_RUNNING) {
		k_mutex_unlock(&ctx->lock);
		return -EPERM;
	}

	ctx->stop_requested = true;
	k_mutex_unlock(&ctx->lock);

	ret = person_detection_mipi_stream_pause();
	if (ret != 0) {
		printk("person_detection_mipi: stop: JPEG stream pause failed: %d\n", ret);
		return ret;
	}

	ret = k_sem_take(&ctx->inference_stopped_sem, K_SECONDS(5));
	if (ret != 0) {
		return ret;
	}

	k_mutex_lock(&ctx->lock, K_FOREVER);
	ctx->state = APP_STATE_STOPPED;
	k_mutex_unlock(&ctx->lock);
	printk("person_detection_mipi: stop: complete\n");
	return 0;
}

int person_detection_mipi_app_resume(void)
{
	struct person_detection_mipi_context *ctx = &app_ctx;
	int ret;

	app_context_init(ctx);
	k_mutex_lock(&ctx->lock, K_FOREVER);
	if (ctx->state != APP_STATE_STOPPED) {
		k_mutex_unlock(&ctx->lock);
		return -EPERM;
	}
	k_mutex_unlock(&ctx->lock);

	printk("person_detection_mipi: resume: resuming paused usecase\n");

	ret = person_detection_mipi_stream_resume();
	if (ret != 0) {
		printk("person_detection_mipi: resume: JPEG stream resume failed: %d\n", ret);
		return ret;
	}

	k_mutex_lock(&ctx->lock, K_FOREVER);
	ctx->stop_requested = false;
	ctx->inference_running = true;
	k_sem_reset(&ctx->inference_ready_sem);
	k_sem_reset(&ctx->inference_stopped_sem);
	k_mutex_unlock(&ctx->lock);

	k_thread_create(&ctx->inference_thread, person_detection_mipi_inference_stack,
			K_THREAD_STACK_SIZEOF(person_detection_mipi_inference_stack),
			person_detection_mipi_inference_worker, ctx, NULL, NULL,
			CONFIG_HOST_API_TASK_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&ctx->inference_thread, "pd_mipi_infer");

	printk("person_detection_mipi: resume: waiting for first inference\n");
	ret = k_sem_take(&ctx->inference_ready_sem, K_SECONDS(5));
	if (ret != 0) {
		printk("person_detection_mipi: resume: first inference timeout: %d\n", ret);
		k_mutex_lock(&ctx->lock, K_FOREVER);
		ctx->stop_requested = true;
		k_mutex_unlock(&ctx->lock);
		(void)person_detection_mipi_stream_pause();
		(void)k_sem_take(&ctx->inference_stopped_sem, K_SECONDS(5));
		return ret;
	}

	k_mutex_lock(&ctx->lock, K_FOREVER);
	ctx->state = APP_STATE_RUNNING;
	k_mutex_unlock(&ctx->lock);

	printk("person_detection_mipi: resume: complete\n");
	return 0;
}

int person_detection_mipi_app_kill(void)
{
	struct person_detection_mipi_context *ctx = &app_ctx;
	enum person_detection_mipi_state state;
	int ret;

	app_context_init(ctx);
	k_mutex_lock(&ctx->lock, K_FOREVER);
	state = ctx->state;
	k_mutex_unlock(&ctx->lock);

	if (state == APP_STATE_RUNNING) {
		ret = person_detection_mipi_app_stop();
		if (ret != 0) {
			printk("person_detection_mipi: kill: stop failed: %d\n", ret);
			return ret;
		}
		state = APP_STATE_STOPPED;
	}

	if (state == APP_STATE_STOPPED) {
		ret = person_detection_mipi_stream_stop();
		if (ret != 0) {
			printk("person_detection_mipi: kill: stream stop failed: %d\n", ret);
			return ret;
		}

		ret = person_detection_mipi_capture_stop();
		if (ret != 0) {
			printk("person_detection_mipi: kill: capture stop failed: %d\n", ret);
			return ret;
		}
	}

	person_detection_mipi_metadata_reset();

	k_mutex_lock(&ctx->lock, K_FOREVER);
	ctx->stop_requested = false;
	ctx->inference_running = false;
	ctx->state = APP_STATE_UNINITIALIZED;
	k_mutex_unlock(&ctx->lock);

	printk("person_detection_mipi: kill: complete\n");
	return 0;
}

int person_detection_mipi_app_get_latest_metadata(char *dst, size_t dst_size)
{
	return person_detection_mipi_metadata_get(dst, dst_size);
}

int person_detection_mipi_app_get_latest_snapshot(
	struct person_detection_mipi_metadata_snapshot *snapshot)
{
	return person_detection_mipi_metadata_snapshot_get(snapshot);
}

bool person_detection_mipi_app_is_running(void)
{
	struct person_detection_mipi_context *ctx = &app_ctx;
	bool running;

	app_context_init(ctx);
	k_mutex_lock(&ctx->lock, K_FOREVER);
	running = (ctx->state == APP_STATE_RUNNING);
	k_mutex_unlock(&ctx->lock);

	return running;
}
