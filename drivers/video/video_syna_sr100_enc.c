/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT syna_enc_video

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/devicetree/port-endpoint.h>
#include <zephyr/cache.h>
#include <zephyr/dt-bindings/memory-attr/memory-attr-arm.h>
#include <zephyr/drivers/video.h>
#include <zephyr/drivers/video-controls.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/barrier.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>
#include <soc.h>

#include <lp_enc.h>

LOG_MODULE_REGISTER(video_syna_sr100_enc, CONFIG_VIDEO_LOG_LEVEL);

/* Register definitions */

#define LPS_LP_BASE_ADDRESS 0xB4800000U

#define LPS_LP_STATUS_OFFSET      0x1CU
#define LPS_LP_DMA_STATUS2_OFFSET 0x24U
#define LPS_LP_FVF_STATUS1_OFFSET 0x38U

#define LPS_LP_STATUS_FRAME_PROCESSING_DONE BIT(0)
#define LPS_LP_STATUS_FMIN0_FRAME_STORED    BIT(1)
#define LPS_LP_STATUS_FMIN1_FRAME_STORED    BIT(2)
#define LPS_LP_STATUS_FMOUT_STREAMING_DONE  BIT(3)
#define LPS_LP_STATUS_STAT_RANGE_EXCEEDED   BIT(8)
#define LPS_LP_STATUS_PROG_COPY_DONE        BIT(10)
#define LPS_LP_STATUS_CSI_AHB_ERROR         BIT(19)
#define LPS_LP_DMA_STATUS2_ERR_SET_ALL GENMASK(4, 0)

/* Devicetree-driven defaults */

#define VIDEO_SYNA_SR100_ENC_DEFAULT_WIDTH         480U
#define VIDEO_SYNA_SR100_ENC_DEFAULT_HEIGHT        270U
#define VIDEO_SYNA_SR100_ENC_DEFAULT_MIN_VBUFS     1U
#define VIDEO_SYNA_SR100_ENC_DEFAULT_ALIGNMENT     64U
#define VIDEO_SYNA_SR100_ENC_DEFAULT_MAX_JPEG_SIZE (128U * 1024U)
#define VIDEO_SYNA_SR100_ENC_DEFAULT_CSI_TIMING    0U
#define VIDEO_SYNA_SR100_ENC_DEFAULT_CSI_INTERFACE 1U
#define VIDEO_SYNA_SR100_ENC_DEFAULT_CSI_ID        0U
#define VIDEO_SYNA_SR100_ENC_DEFAULT_CSI_LANES     1U
#define VIDEO_SYNA_SR100_ENC_DEFAULT_CSI_VIRT_CH   0U
#define VIDEO_SYNA_SR100_ENC_DEFAULT_CSI_AUTO_FLUSH 1U
#define VIDEO_SYNA_SR100_ENC_DEFAULT_FRMIVAL_NUM   1U
#define VIDEO_SYNA_SR100_ENC_DEFAULT_FRMIVAL_DEN   30U

/* Pipeline modes (devicetree property: mode) */

#define VIDEO_SYNA_SR100_ENC_MODE_SENSOR_TO_MEMORY 0U
#define VIDEO_SYNA_SR100_ENC_MODE_MEMORY_INPUT 1U

#define VIDEO_SYNA_SR100_ENC_FRAME_DONE_MASK LPS_LP_STATUS_FRAME_PROCESSING_DONE
#define VIDEO_SYNA_SR100_ENC_FMIN1_STORED_MASK LPS_LP_STATUS_FMIN1_FRAME_STORED
#define VIDEO_SYNA_SR100_ENC_HANDLED_STATUS_MASK \
	(VIDEO_SYNA_SR100_ENC_FRAME_DONE_MASK | VIDEO_SYNA_SR100_ENC_FMIN1_STORED_MASK)
#define VIDEO_SYNA_SR100_ENC_MEMORY_COMPLETE_MASK                                           \
	(VIDEO_SYNA_SR100_ENC_FRAME_DONE_MASK |                                             \
	 LPS_LP_STATUS_FMIN0_FRAME_STORED |                             \
	 LPS_LP_STATUS_FMOUT_STREAMING_DONE)

/* Driver state */

struct video_syna_sr100_enc_layout {
	size_t input_offset;
	size_t output_offset;
	size_t jpeg_size_limit;
	size_t clear_offset;
	size_t clear_size;
};

struct video_syna_sr100_enc_config {
	uintptr_t lp_mem_base;
	size_t lp_mem_size;
	size_t max_jpeg_size;
	size_t frame_raw_offset;
	uint32_t mode;
	enum video_buf_type type;
	const struct device *sensor_dev;
	uint8_t csi_lanes;
	uint32_t csi_id;
	uint32_t csi_timing;
	uint32_t csi_interface;
	uint32_t csi_virt_ch;
	uint32_t csi_auto_flush;
	void (*irq_config)(void);
};

struct video_syna_sr100_enc_data {
	struct k_fifo fifo_in;
	struct k_fifo fifo_out;
	struct k_mutex lock;
	struct k_work capture_work;
#ifdef CONFIG_POLL
	struct k_poll_signal *signal_out;
#endif
	const struct device *dev;
	struct video_format fmt;
	struct video_frmival frmival;
	atomic_t streaming;
	bool starting;
	bool configured;
	bool stopping;
	atomic_t capture_inflight;
	atomic_t pending_status;
	size_t input_offset;
	size_t output_offset;
	size_t jpeg_size_limit;
};

static const struct video_format_cap video_syna_sr100_enc_format_caps[] = {
	{
		.pixelformat = VIDEO_PIX_FMT_JPEG,
		.width_min = 320,
		.width_max = 320,
		.height_min = 180,
		.height_max = 180,
		.width_step = 0,
		.height_step = 0,
	},
	{
		.pixelformat = VIDEO_PIX_FMT_JPEG,
		.width_min = 480,
		.width_max = 480,
		.height_min = 270,
		.height_max = 270,
		.width_step = 0,
		.height_step = 0,
	},
	{
		.pixelformat = VIDEO_PIX_FMT_JPEG,
		.width_min = 500,
		.width_max = 500,
		.height_min = 500,
		.height_max = 500,
		.width_step = 0,
		.height_step = 0,
	},
	{
		.pixelformat = VIDEO_PIX_FMT_JPEG,
		.width_min = 640,
		.width_max = 640,
		.height_min = 360,
		.height_max = 360,
		.width_step = 0,
		.height_step = 0,
	},
	{
		.pixelformat = VIDEO_PIX_FMT_JPEG,
		.width_min = 640,
		.width_max = 640,
		.height_min = 480,
		.height_max = 480,
		.width_step = 0,
		.height_step = 0,
	},
	{
		.pixelformat = VIDEO_PIX_FMT_JPEG,
		.width_min = 960,
		.width_max = 960,
		.height_min = 540,
		.height_max = 540,
		.width_step = 0,
		.height_step = 0,
	},
	{ 0 }
};

static const struct device *video_syna_sr100_enc_cb_dev;
static struct k_spinlock video_syna_sr100_enc_cb_lock;

static int video_syna_sr100_enc_kick(const struct device *dev);
static void video_syna_sr100_enc_complete_buffer(const struct device *dev, uint32_t status);
static void video_syna_sr100_enc_callback(uint32_t lps_status,
					uint32_t lps_dma_status,
					uint32_t dma_status,
					uint32_t fvf_status1);

static void video_syna_sr100_enc_irq(const struct device *dev)
{
	uint32_t status;
	uint32_t ack_mask;

	status = sys_read32(LPS_LP_BASE_ADDRESS + LPS_LP_STATUS_OFFSET);
	ack_mask = status & VIDEO_SYNA_SR100_ENC_HANDLED_STATUS_MASK;
	if (ack_mask != 0U) {
		sys_write32(ack_mask, LPS_LP_BASE_ADDRESS + LPS_LP_STATUS_OFFSET);
	}

	if (status != 0U) {
		video_syna_sr100_enc_callback(status, 0U, 0U, 0U);
	}
}

static void video_syna_sr100_enc_irq_frame_done(const void *arg)
{
	const struct device *dev = arg;

	video_syna_sr100_enc_irq(dev);
}

static void video_syna_sr100_enc_irq_fmin1_stored(const void *arg)
{
	const struct device *dev = arg;

	video_syna_sr100_enc_irq(dev);
}

static void video_syna_sr100_enc_irq_status(const void *arg)
{
	const struct device *dev = arg;
	uint32_t status;
	uint32_t ack_mask;
	uint32_t fvf_status;

	ARG_UNUSED(dev);

	status = sys_read32(LPS_LP_BASE_ADDRESS + LPS_LP_STATUS_OFFSET);
	ack_mask = status & (VIDEO_SYNA_SR100_ENC_HANDLED_STATUS_MASK |
			     LPS_LP_STATUS_FMIN0_FRAME_STORED |
			     LPS_LP_STATUS_FMOUT_STREAMING_DONE |
			     LPS_LP_STATUS_STAT_RANGE_EXCEEDED |
			     LPS_LP_STATUS_PROG_COPY_DONE |
			     LPS_LP_STATUS_CSI_AHB_ERROR);

	if (ack_mask != 0U) {
		sys_write32(ack_mask, LPS_LP_BASE_ADDRESS + LPS_LP_STATUS_OFFSET);
	}

	fvf_status = sys_read32(LPS_LP_BASE_ADDRESS + LPS_LP_FVF_STATUS1_OFFSET);
	if (fvf_status != 0U) {
		sys_write32(0xFFFFFFFFu, LPS_LP_BASE_ADDRESS + LPS_LP_FVF_STATUS1_OFFSET);
	}

	if (status != 0U) {
		video_syna_sr100_enc_callback(status, 0U, 0U, 0U);
	}
}

static const struct device *video_syna_sr100_enc_callback_owner_get(void)
{
	k_spinlock_key_t key;
	const struct device *owner;

	key = k_spin_lock(&video_syna_sr100_enc_cb_lock);
	owner = video_syna_sr100_enc_cb_dev;
	k_spin_unlock(&video_syna_sr100_enc_cb_lock, key);

	return owner;
}

static int video_syna_sr100_enc_callback_owner_claim(const struct device *dev)
{
	const struct device *owner;
	k_spinlock_key_t key;
	int ret = 0;

	key = k_spin_lock(&video_syna_sr100_enc_cb_lock);
	owner = video_syna_sr100_enc_cb_dev;
	if ((owner != NULL) && (owner != dev)) {
		ret = -EBUSY;
	} else {
		video_syna_sr100_enc_cb_dev = dev;
	}
	k_spin_unlock(&video_syna_sr100_enc_cb_lock, key);

	return ret;
}

static void video_syna_sr100_enc_callback_owner_release(const struct device *dev)
{
	k_spinlock_key_t key;

	key = k_spin_lock(&video_syna_sr100_enc_cb_lock);
	if (video_syna_sr100_enc_cb_dev == dev) {
		video_syna_sr100_enc_cb_dev = NULL;
	}
	k_spin_unlock(&video_syna_sr100_enc_cb_lock, key);
}

static bool video_syna_sr100_enc_format_supported(const struct video_format *fmt)
{
	if (fmt == NULL) {
		return false;
	}

	if ((fmt->type != VIDEO_BUF_TYPE_OUTPUT) ||
	    (fmt->pixelformat != VIDEO_PIX_FMT_JPEG)) {
		return false;
	}

	return lp_enc_is_valid_size(fmt->width, fmt->height);
}

static int video_syna_sr100_enc_scan_jpeg_size(const uint8_t *data, size_t limit,
				    size_t *jpeg_size)
{
	size_t i;

	if ((data == NULL) || (jpeg_size == NULL) || (limit < 4U)) {
		return -EINVAL;
	}

	if ((data[0] != 0xFFU) || (data[1] != 0xD8U)) {
		return -EIO;
	}

	for (i = 2U; i < limit; ++i) {
		if ((data[i - 1U] == 0xFFU) && (data[i] == 0xD9U)) {
			*jpeg_size = i + 1U;
			return 0;
		}
	}

	return -EIO;
}

static int video_syna_sr100_enc_calculate_sensor_layout(const struct video_syna_sr100_enc_config *cfg,
						     const struct video_format *fmt,
						     struct video_syna_sr100_enc_layout *layout)
{
	size_t raw_size;
	size_t jpeg_limit;
	size_t raw_end;
	size_t raw_offset;
	size_t output_offset;
	size_t clear_span;

	if ((cfg == NULL) || (fmt == NULL) || (layout == NULL)) {
		return -EINVAL;
	}

	if ((cfg->lp_mem_base == 0U) || (cfg->lp_mem_size == 0U)) {
		return -EINVAL;
	}

	raw_offset = (size_t)cfg->frame_raw_offset;
	if (raw_offset >= cfg->lp_mem_size) {
		return -ENOMEM;
	}

	if (((size_t)fmt->width != 0U) && ((size_t)fmt->height > (SIZE_MAX / (size_t)fmt->width))) {
		return -EOVERFLOW;
	}
	raw_size = (size_t)fmt->width * (size_t)fmt->height;

	if ((raw_offset > (SIZE_MAX - raw_size)) || ((raw_end = raw_offset + raw_size) > cfg->lp_mem_size)) {
		return -ENOMEM;
	}

	output_offset = ROUND_UP(raw_end, VIDEO_SYNA_SR100_ENC_DEFAULT_ALIGNMENT);
	if (output_offset >= cfg->lp_mem_size) {
		return -ENOMEM;
	}

	jpeg_limit = cfg->max_jpeg_size;
	if (jpeg_limit == 0U) {
		jpeg_limit = VIDEO_SYNA_SR100_ENC_DEFAULT_MAX_JPEG_SIZE;
	}

	jpeg_limit = MIN(jpeg_limit, cfg->lp_mem_size - output_offset);
	if (jpeg_limit == 0U) {
		return -ENOMEM;
	}

	if ((output_offset - raw_offset) > (SIZE_MAX - jpeg_limit)) {
		return -EOVERFLOW;
	}
	clear_span = (output_offset - raw_offset) + jpeg_limit;

	layout->input_offset = raw_offset;
	layout->output_offset = output_offset;
	layout->jpeg_size_limit = jpeg_limit;
	layout->clear_offset = raw_offset;
	layout->clear_size = clear_span;

	return 0;
}

static int video_syna_sr100_enc_calculate_memory_layout(const struct video_syna_sr100_enc_config *cfg,
							 const struct video_format *fmt,
							 struct video_syna_sr100_enc_layout *layout)
{
	size_t raw_size;
	size_t jpeg_limit;
	size_t raw_end;
	size_t raw_offset;
	size_t output_offset;

	if ((cfg == NULL) || (fmt == NULL) || (layout == NULL)) {
		return -EINVAL;
	}

	if ((cfg->lp_mem_base == 0U) || (cfg->lp_mem_size == 0U)) {
		return -EINVAL;
	}

	if (((size_t)fmt->width != 0U) && ((size_t)fmt->height > (SIZE_MAX / (size_t)fmt->width))) {
		return -EOVERFLOW;
	}
	raw_size = (size_t)fmt->width * (size_t)fmt->height;

	raw_offset = (size_t)cfg->frame_raw_offset;
	if (raw_offset >= cfg->lp_mem_size) {
		return -ENOMEM;
	}

	if ((raw_offset > (SIZE_MAX - raw_size)) || ((raw_end = raw_offset + raw_size) > cfg->lp_mem_size)) {
		return -ENOMEM;
	}

	output_offset = ROUND_UP(raw_end, VIDEO_SYNA_SR100_ENC_DEFAULT_ALIGNMENT);
	if (output_offset >= cfg->lp_mem_size) {
		return -ENOMEM;
	}

	jpeg_limit = cfg->max_jpeg_size;
	if (jpeg_limit == 0U) {
		jpeg_limit = VIDEO_SYNA_SR100_ENC_DEFAULT_MAX_JPEG_SIZE;
	}

	jpeg_limit = MIN(jpeg_limit, (size_t)cfg->lp_mem_size - output_offset);
	if (jpeg_limit == 0U) {
		return -ENOMEM;
	}

	layout->input_offset = raw_offset;
	layout->output_offset = output_offset;
	layout->jpeg_size_limit = jpeg_limit;
	/*
	 * In mode=1, the application populates the raw input region. Do not clear it
	 * here, otherwise the encoder would see an empty/black frame.
	 */
	layout->clear_offset = output_offset;
	layout->clear_size = jpeg_limit;

	return 0;
}

static int video_syna_sr100_enc_prepare_memory_layout(const struct device *dev)
{
	const struct video_syna_sr100_enc_config *cfg = dev->config;
	struct video_syna_sr100_enc_data *data = dev->data;
	struct video_syna_sr100_enc_layout layout;
	int ret;

	if (cfg->mode == VIDEO_SYNA_SR100_ENC_MODE_SENSOR_TO_MEMORY) {
		ret = video_syna_sr100_enc_calculate_sensor_layout(cfg, &data->fmt, &layout);
		if (ret != 0) {
			return ret;
		}
	} else if (cfg->mode == VIDEO_SYNA_SR100_ENC_MODE_MEMORY_INPUT) {
		ret = video_syna_sr100_enc_calculate_memory_layout(cfg, &data->fmt, &layout);
		if (ret != 0) {
			return ret;
		}
	} else {
		return -EINVAL;
	}

	data->input_offset = layout.input_offset;
	data->output_offset = layout.output_offset;
	data->jpeg_size_limit = layout.jpeg_size_limit;

	memset((void *)(cfg->lp_mem_base + layout.clear_offset), 0, layout.clear_size);
#if IS_ENABLED(CONFIG_CACHE_MANAGEMENT) && IS_ENABLED(CONFIG_DCACHE)
	if (layout.clear_size != 0U) {
		(void)sys_cache_data_flush_range((void *)(cfg->lp_mem_base + layout.clear_offset),
						 layout.clear_size);
		barrier_dsync_fence_full();
	}
#endif

	return 0;
}

static void video_syna_sr100_enc_callback(uint32_t lps_status,
					    uint32_t lps_dma_status,
					    uint32_t dma_status,
					    uint32_t fvf_status1)
{
	const struct device *dev = video_syna_sr100_enc_callback_owner_get();
	struct video_syna_sr100_enc_data *data;

	ARG_UNUSED(lps_dma_status);
	ARG_UNUSED(dma_status);
	ARG_UNUSED(fvf_status1);

	if (dev == NULL) {
		return;
	}

	data = dev->data;
	(void)atomic_or(&data->pending_status, (atomic_val_t)lps_status);
	(void)k_work_submit_to_queue(&k_sys_work_q, &data->capture_work);
}

static int video_syna_sr100_enc_start(const struct device *dev)
{
	const struct video_syna_sr100_enc_config *cfg = dev->config;
	struct video_syna_sr100_enc_data *data = dev->data;
	int ret;
	const uint8_t bpp = 8U;
	bool sensor_started = false;

	soc_sr100_lp_jpeg_clocks_enable();

	ret = video_syna_sr100_enc_prepare_memory_layout(dev);
	if (ret != 0) {
		LOG_ERR("Invalid JPEG encoder reserved memory layout: %d", ret);
		soc_sr100_lp_jpeg_clocks_disable();
		return ret;
	}

	ret = video_syna_sr100_enc_callback_owner_claim(dev);
	if (ret != 0) {
		LOG_ERR("JPEG encoder driver supports only one active instance");
		soc_sr100_lp_jpeg_clocks_disable();
		return ret;
	}

	if (cfg->mode == VIDEO_SYNA_SR100_ENC_MODE_SENSOR_TO_MEMORY) {
		struct video_format sensor_fmt = {
			.type = VIDEO_BUF_TYPE_OUTPUT,
			.pixelformat = VIDEO_PIX_FMT_SRGGB8,
			.width = data->fmt.width,
			.height = data->fmt.height,
		};
		struct video_control ctrl = { .id = VIDEO_CID_PIXEL_RATE };
		struct lp_enc_mipi_cfg enc_cfg = { 0 };
		uint64_t pixel_rate = 0U;
		uint64_t bit_rate;
		uint32_t lane_rate_kbps;

		if ((cfg->sensor_dev == NULL) || !device_is_ready(cfg->sensor_dev)) {
			ret = -ENODEV;
			goto fail;
		}

		ret = video_set_format(cfg->sensor_dev, &sensor_fmt);
		if (ret != 0) {
			LOG_ERR("sensor video_set_format failed ret=%d (%ux%u pixfmt=0x%x)",
				ret, (unsigned int)sensor_fmt.width, (unsigned int)sensor_fmt.height,
				(unsigned int)sensor_fmt.pixelformat);
			goto fail;
		}

		sensor_fmt = (struct video_format){ .type = VIDEO_BUF_TYPE_OUTPUT };
		ret = video_get_format(cfg->sensor_dev, &sensor_fmt);
		if (ret != 0) {
			goto fail;
		}
		if ((sensor_fmt.width != data->fmt.width) || (sensor_fmt.height != data->fmt.height)) {
			LOG_ERR("Sensor format mismatch: sensor=%ux%u enc=%ux%u",
				(unsigned int)sensor_fmt.width, (unsigned int)sensor_fmt.height,
				(unsigned int)data->fmt.width, (unsigned int)data->fmt.height);
			ret = -EINVAL;
			goto fail;
		}

		if ((sensor_fmt.pixelformat != VIDEO_PIX_FMT_SBGGR8) &&
		    (sensor_fmt.pixelformat != VIDEO_PIX_FMT_SGBRG8) &&
		    (sensor_fmt.pixelformat != VIDEO_PIX_FMT_SGRBG8) &&
		    (sensor_fmt.pixelformat != VIDEO_PIX_FMT_SRGGB8)) {
			LOG_ERR("Unsupported sensor pixelformat: 0x%x",
				(unsigned int)sensor_fmt.pixelformat);
			ret = -ENOTSUP;
			goto fail;
		}

		ret = video_get_ctrl(cfg->sensor_dev, &ctrl);
		if (ret == 0) {
			pixel_rate = (uint64_t)ctrl.val64;
		}
		if ((pixel_rate == 0U) || (cfg->csi_lanes == 0U)) {
			ret = -ENOTSUP;
			goto fail;
		}
		bit_rate = pixel_rate * (uint64_t)bpp;
		bit_rate /= (uint64_t)cfg->csi_lanes;
		lane_rate_kbps = (uint32_t)(bit_rate / 1000ULL);
		if (lane_rate_kbps == 0U) {
			ret = -EINVAL;
			goto fail;
		}

		enc_cfg.frame.width = (uint16_t)data->fmt.width;
		enc_cfg.frame.height = (uint16_t)data->fmt.height;
		enc_cfg.frame.raw_offset = (uint32_t)data->input_offset;
		enc_cfg.frame.jpeg_size_limit = (uint32_t)data->jpeg_size_limit;
		enc_cfg.input.csi_id = cfg->csi_id;
		enc_cfg.input.code = 0U;
		enc_cfg.input.lanes = cfg->csi_lanes;
		enc_cfg.input.lane_rate_kbps = lane_rate_kbps;
		enc_cfg.input.timing = cfg->csi_timing;
		enc_cfg.input.interface = cfg->csi_interface;
		enc_cfg.input.virt_ch = cfg->csi_virt_ch;
		enc_cfg.input.auto_flush = cfg->csi_auto_flush;
		enc_cfg.callback = video_syna_sr100_enc_callback;

		if (!lp_enc_init()) {
			ret = -EIO;
			goto fail;
		}

		if (!lp_enc_config_input(&enc_cfg)) {
			ret = -EIO;
			goto fail_uninit;
		}

		ret = video_stream_start(cfg->sensor_dev, VIDEO_BUF_TYPE_OUTPUT);
		if (ret != 0) {
			LOG_ERR("sensor stream start failed ret=%d", ret);
			goto fail_uninit;
		}
		sensor_started = true;
	} else if (cfg->mode == VIDEO_SYNA_SR100_ENC_MODE_MEMORY_INPUT) {
		struct lp_enc_mem_cfg enc_cfg = { 0 };

		enc_cfg.frame.width = (uint16_t)data->fmt.width;
		enc_cfg.frame.height = (uint16_t)data->fmt.height;
		enc_cfg.frame.jpeg_size_limit = (uint32_t)data->jpeg_size_limit;
		enc_cfg.memory.src_offset = (uint32_t)data->input_offset;
		enc_cfg.memory.dst_offset = (uint32_t)data->output_offset;
		enc_cfg.callback = video_syna_sr100_enc_callback;

		if (!lp_enc_init()) {
			ret = -EIO;
			goto fail;
		}

		if (!lp_enc_config_memory(&enc_cfg)) {
			ret = -EIO;
			goto fail_uninit;
		}
	} else {
		ret = -EINVAL;
		goto fail;
	}

	barrier_dsync_fence_full();
	if (!lp_enc_start()) {
		ret = -EIO;
		(void)lp_enc_deinit();
		goto fail_stop_sensor;
	}

	data->configured = true;
	atomic_set(&data->streaming, 1);
	atomic_clear(&data->capture_inflight);
	if (atomic_get(&data->pending_status) != 0) {
		(void)k_work_submit(&data->capture_work);
	}
	(void)video_syna_sr100_enc_kick(dev);
	return 0;

fail_stop_sensor:
	if (sensor_started && (cfg->sensor_dev != NULL) && device_is_ready(cfg->sensor_dev)) {
		(void)video_stream_stop(cfg->sensor_dev, VIDEO_BUF_TYPE_OUTPUT);
	}
	goto fail;

fail_uninit:
	(void)lp_enc_deinit();
fail:
	video_syna_sr100_enc_callback_owner_release(dev);
	soc_sr100_lp_jpeg_clocks_disable();
	return ret;
}

static void video_syna_sr100_enc_abort_queued(struct video_syna_sr100_enc_data *data)
{
	struct video_buffer *vbuf;

	while ((vbuf = k_fifo_get(&data->fifo_in, K_NO_WAIT)) != NULL) {
		vbuf->bytesused = 0U;
		k_fifo_put(&data->fifo_out, vbuf);
#ifdef CONFIG_POLL
		if (data->signal_out != NULL) {
			k_poll_signal_raise(data->signal_out, VIDEO_BUF_ABORTED);
		}
#endif
	}
}

static void video_syna_sr100_enc_stop(const struct device *dev)
{
	const struct video_syna_sr100_enc_config *cfg = dev->config;
	struct video_syna_sr100_enc_data *data = dev->data;
	struct k_work_sync capture_sync;
	uint32_t status;
	uint32_t dma_status;
	uint32_t fvf_status;
	uint32_t ack_mask;

	k_mutex_lock(&data->lock, K_FOREVER);

	if (data->stopping) {
		k_mutex_unlock(&data->lock);
		(void)k_work_cancel_sync(&data->capture_work, &capture_sync);
		return;
	}

	data->stopping = true;
	atomic_clear(&data->streaming);
	atomic_clear(&data->capture_inflight);
	video_syna_sr100_enc_callback_owner_release(dev);
	k_mutex_unlock(&data->lock);

	(void)k_work_cancel_sync(&data->capture_work, &capture_sync);

	k_mutex_lock(&data->lock, K_FOREVER);
	atomic_set(&data->pending_status, 0);

	if (data->configured) {
		if ((cfg->mode == VIDEO_SYNA_SR100_ENC_MODE_SENSOR_TO_MEMORY) &&
		    (cfg->sensor_dev != NULL) && device_is_ready(cfg->sensor_dev)) {
			(void)video_stream_stop(cfg->sensor_dev, VIDEO_BUF_TYPE_OUTPUT);
		}

		(void)lp_enc_stop();
		(void)lp_enc_deinit();

		status = sys_read32(LPS_LP_BASE_ADDRESS + LPS_LP_STATUS_OFFSET);
		dma_status = sys_read32(LPS_LP_BASE_ADDRESS + LPS_LP_DMA_STATUS2_OFFSET);
		fvf_status = sys_read32(LPS_LP_BASE_ADDRESS + LPS_LP_FVF_STATUS1_OFFSET);
		ack_mask = status & (VIDEO_SYNA_SR100_ENC_HANDLED_STATUS_MASK |
				     LPS_LP_STATUS_FMIN0_FRAME_STORED |
				     LPS_LP_STATUS_FMOUT_STREAMING_DONE |
				     LPS_LP_STATUS_STAT_RANGE_EXCEEDED |
				     LPS_LP_STATUS_PROG_COPY_DONE |
				     LPS_LP_STATUS_CSI_AHB_ERROR);

		if (ack_mask != 0U) {
			sys_write32(ack_mask, LPS_LP_BASE_ADDRESS + LPS_LP_STATUS_OFFSET);
		}

		if (dma_status != 0U) {
			sys_write32(LPS_LP_DMA_STATUS2_ERR_SET_ALL,
				    LPS_LP_BASE_ADDRESS + LPS_LP_DMA_STATUS2_OFFSET);
		}

		if (fvf_status != 0U) {
			sys_write32(0xFFFFFFFFu, LPS_LP_BASE_ADDRESS + LPS_LP_FVF_STATUS1_OFFSET);
		}
	}

	data->configured = false;
	video_syna_sr100_enc_abort_queued(data);
	data->stopping = false;
	k_mutex_unlock(&data->lock);
}

static int video_syna_sr100_enc_kick(const struct device *dev)
{
	const struct video_syna_sr100_enc_config *cfg = dev->config;
	struct video_syna_sr100_enc_data *data = dev->data;

	if ((atomic_get(&data->streaming) == 0) || !data->configured ||
	    (atomic_get(&data->capture_inflight) != 0)) {
		return 0;
	}

	if (k_fifo_peek_head(&data->fifo_in) == NULL) {
		return 0;
	}

	if (cfg->mode != VIDEO_SYNA_SR100_ENC_MODE_MEMORY_INPUT) {
		atomic_set(&data->capture_inflight, 1);
		return 0;
	}

	barrier_dsync_fence_full();
	/* Retrigger is only valid for memory-input mode. */
	(void)lp_enc_retrigger();
	atomic_set(&data->capture_inflight, 1);
	return 0;
}

static void video_syna_sr100_enc_complete_buffer(const struct device *dev, uint32_t status)
{
	const struct video_syna_sr100_enc_config *cfg = dev->config;
	struct video_syna_sr100_enc_data *data = dev->data;
	const uint8_t *frame_data = NULL;
	size_t frame_size = 0U;
	const uint8_t *jpeg_ptr;
	struct video_buffer *vbuf;
	int ret;

	if (data->stopping || (atomic_get(&data->streaming) == 0)) {
		return;
	}

	if (cfg->mode == VIDEO_SYNA_SR100_ENC_MODE_MEMORY_INPUT) {
		atomic_clear(&data->capture_inflight);
	}

	if (cfg->mode == VIDEO_SYNA_SR100_ENC_MODE_MEMORY_INPUT) {
		if ((status & VIDEO_SYNA_SR100_ENC_MEMORY_COMPLETE_MASK) == 0U) {
			LOG_DBG("Ignoring JPEG encoder completion status 0x%x", status);
			return;
		}
	} else if ((status & VIDEO_SYNA_SR100_ENC_FRAME_DONE_MASK) == 0U) {
		LOG_DBG("Ignoring JPEG encoder completion status 0x%x", status);
		return;
	}

	if (cfg->mode != VIDEO_SYNA_SR100_ENC_MODE_MEMORY_INPUT) {
		if (atomic_get(&data->capture_inflight) == 0) {
			LOG_DBG("Ignoring JPEG encoder completion while not armed (status=0x%x)",
				status);
			return;
		}
		atomic_clear(&data->capture_inflight);
	}

	vbuf = k_fifo_get(&data->fifo_in, K_NO_WAIT);
	if (vbuf == NULL) {
		LOG_DBG("JPEG encoder completed with no queued video buffer");
		return;
	}

	if ((cfg->lp_mem_base == 0U) || (data->jpeg_size_limit == 0U)) {
		ret = -EINVAL;
	} else {
		jpeg_ptr = (const uint8_t *)(cfg->lp_mem_base + data->output_offset);
#if IS_ENABLED(CONFIG_CACHE_MANAGEMENT) && IS_ENABLED(CONFIG_DCACHE)
		(void)sys_cache_data_invd_range((void *)jpeg_ptr, data->jpeg_size_limit);
		barrier_dsync_fence_full();
#endif
		ret = video_syna_sr100_enc_scan_jpeg_size(jpeg_ptr, data->jpeg_size_limit,
						   &frame_size);
		if (ret == 0) {
			frame_data = jpeg_ptr;
		}
	}
	if (ret != 0) {
		LOG_ERR("Failed to fetch JPEG frame: %d", ret);
		vbuf->bytesused = 0U;
		k_fifo_put(&data->fifo_out, vbuf);
#ifdef CONFIG_POLL
		if (data->signal_out != NULL) {
			k_poll_signal_raise(data->signal_out, VIDEO_BUF_ABORTED);
		}
#endif
		(void)video_syna_sr100_enc_kick(dev);
		return;
	}

	if ((frame_data == NULL) || (frame_size == 0U)) {
		LOG_ERR("JPEG encoder returned an empty JPEG frame");
		vbuf->bytesused = 0U;
		k_fifo_put(&data->fifo_out, vbuf);
#ifdef CONFIG_POLL
		if (data->signal_out != NULL) {
			k_poll_signal_raise(data->signal_out, VIDEO_BUF_ABORTED);
		}
#endif
		(void)video_syna_sr100_enc_kick(dev);
		return;
	}

	if (frame_size > vbuf->size) {
		LOG_ERR("Queued buffer too small for JPEG frame (%zu > %zu)",
			frame_size, vbuf->size);
		vbuf->bytesused = 0U;
		k_fifo_put(&data->fifo_out, vbuf);
#ifdef CONFIG_POLL
		if (data->signal_out != NULL) {
			k_poll_signal_raise(data->signal_out, VIDEO_BUF_ABORTED);
		}
#endif
		(void)video_syna_sr100_enc_kick(dev);
		return;
	}

	memcpy(vbuf->buffer, frame_data, frame_size);
	vbuf->bytesused = frame_size;
	k_fifo_put(&data->fifo_out, vbuf);
#ifdef CONFIG_POLL
	if (data->signal_out != NULL) {
		k_poll_signal_raise(data->signal_out, VIDEO_BUF_DONE);
	}
#endif
	(void)video_syna_sr100_enc_kick(dev);
}

static void video_syna_sr100_enc_capture_work(struct k_work *work)
{
	struct video_syna_sr100_enc_data *data = CONTAINER_OF(work, struct video_syna_sr100_enc_data,
						   capture_work);
	const struct device *dev = data->dev;
	atomic_val_t status;

	if (dev == NULL) {
		return;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	if (data->stopping) {
		k_mutex_unlock(&data->lock);
		return;
	}

	if (atomic_get(&data->streaming) == 0) {
		k_mutex_unlock(&data->lock);
		return;
	}

	status = atomic_set(&data->pending_status, 0);
	if (status == 0U) {
		k_mutex_unlock(&data->lock);
		return;
	}

	video_syna_sr100_enc_complete_buffer(dev, (uint32_t)status);
	k_mutex_unlock(&data->lock);
}

static int video_syna_sr100_enc_enqueue(const struct device *dev,
				     struct video_buffer *vbuf)
{
	struct video_syna_sr100_enc_data *data = dev->data;
	int ret;

	if ((vbuf == NULL) || (vbuf->buffer == NULL)) {
		return -EINVAL;
	}

	if (vbuf->type != VIDEO_BUF_TYPE_OUTPUT) {
		return -EINVAL;
	}

	if (vbuf->size == 0U) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	if ((data->fmt.size != 0U) && (vbuf->size < data->fmt.size)) {
		k_mutex_unlock(&data->lock);
		return -EINVAL;
	}
	k_fifo_put(&data->fifo_in, vbuf);
	ret = video_syna_sr100_enc_kick(dev);
	k_mutex_unlock(&data->lock);

	return ret;
}

static int video_syna_sr100_enc_dequeue(const struct device *dev, struct video_buffer **vbuf,
				     k_timeout_t timeout)
{
	struct video_syna_sr100_enc_data *data = dev->data;

	if (vbuf == NULL) {
		return -EINVAL;
	}

	*vbuf = k_fifo_get(&data->fifo_out, timeout);
	if (*vbuf == NULL) {
		return -EAGAIN;
	}

	return 0;
}

static int video_syna_sr100_enc_set_format(const struct device *dev,
					struct video_format *fmt)
{
	const struct video_syna_sr100_enc_config *cfg = dev->config;
	struct video_syna_sr100_enc_data *data = dev->data;
	struct video_syna_sr100_enc_layout layout;
	int ret;

	if (fmt == NULL) {
		return -EINVAL;
	}

	if (!video_syna_sr100_enc_format_supported(fmt)) {
		return -ENOTSUP;
	}

	ret = video_estimate_fmt_size(fmt);
	if (ret < 0) {
		return ret;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	if (atomic_get(&data->streaming) != 0) {
		k_mutex_unlock(&data->lock);
		return -EBUSY;
	}

	if (data->starting) {
		k_mutex_unlock(&data->lock);
		return -EBUSY;
	}

	if (cfg->mode == VIDEO_SYNA_SR100_ENC_MODE_SENSOR_TO_MEMORY) {
		ret = video_syna_sr100_enc_calculate_sensor_layout(cfg, fmt, &layout);
		if (ret != 0) {
			k_mutex_unlock(&data->lock);
			return ret;
		}
	} else if (cfg->mode == VIDEO_SYNA_SR100_ENC_MODE_MEMORY_INPUT) {
		ret = video_syna_sr100_enc_calculate_memory_layout(cfg, fmt, &layout);
		if (ret != 0) {
			k_mutex_unlock(&data->lock);
			return ret;
		}
	} else {
		k_mutex_unlock(&data->lock);
		return -EINVAL;
	}

	fmt->type = VIDEO_BUF_TYPE_OUTPUT;
	/*
	 * Report the required JPEG output-buffer capacity, not the exact encoded
	 * frame size. The actual JPEG byte count is returned in
	 * `video_buffer.bytesused` on dequeue.
	 */
	fmt->size = layout.jpeg_size_limit;
	data->fmt = *fmt;
	data->configured = false;
	atomic_clear(&data->capture_inflight);
	data->input_offset = 0U;
	data->output_offset = 0U;
	data->jpeg_size_limit = 0U;

	k_mutex_unlock(&data->lock);

	return 0;
}

static int video_syna_sr100_enc_get_format(const struct device *dev, struct video_format *fmt)
{
	struct video_syna_sr100_enc_data *data = dev->data;

	if (fmt == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	*fmt = data->fmt;
	k_mutex_unlock(&data->lock);

	return 0;
}

static int video_syna_sr100_enc_set_frmival(const struct device *dev, struct video_frmival *frmival)
{
	struct video_syna_sr100_enc_data *data = dev->data;

	if ((frmival == NULL) || (frmival->numerator == 0U) ||
	    (frmival->denominator == 0U)) {
		return -EINVAL;
	}

	if ((frmival->numerator != VIDEO_SYNA_SR100_ENC_DEFAULT_FRMIVAL_NUM) ||
	    (frmival->denominator != VIDEO_SYNA_SR100_ENC_DEFAULT_FRMIVAL_DEN)) {
		return -ENOTSUP;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	data->frmival = *frmival;
	k_mutex_unlock(&data->lock);

	return 0;
}

static int video_syna_sr100_enc_get_frmival(const struct device *dev, struct video_frmival *frmival)
{
	struct video_syna_sr100_enc_data *data = dev->data;

	if (frmival == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	*frmival = data->frmival;
	k_mutex_unlock(&data->lock);

	return 0;
}

static int video_syna_sr100_enc_enum_frmival(const struct device *dev,
					  struct video_frmival_enum *fie)
{
	struct video_syna_sr100_enc_data *data = dev->data;

	ARG_UNUSED(dev);

	if ((fie == NULL) || (fie->format == NULL)) {
		return -EINVAL;
	}

	if (!video_syna_sr100_enc_format_supported(fie->format) || (fie->index != 0U)) {
		return -EINVAL;
	}

	fie->type = VIDEO_FRMIVAL_TYPE_DISCRETE;
	k_mutex_lock(&data->lock, K_FOREVER);
	fie->discrete = data->frmival;
	k_mutex_unlock(&data->lock);

	return 0;
}

static int video_syna_sr100_enc_get_caps(const struct device *dev, struct video_caps *caps)
{
	const struct video_syna_sr100_enc_config *cfg = dev->config;

	if (caps == NULL) {
		return -EINVAL;
	}

	caps->type = cfg->type;
	caps->format_caps = video_syna_sr100_enc_format_caps;
	caps->min_vbuf_count = VIDEO_SYNA_SR100_ENC_DEFAULT_MIN_VBUFS;
	caps->buf_align = VIDEO_SYNA_SR100_ENC_DEFAULT_ALIGNMENT;

	return 0;
}

static int video_syna_sr100_enc_flush(const struct device *dev, bool cancel)
{
	struct video_syna_sr100_enc_data *data = dev->data;
	int ret = 0;

	k_mutex_lock(&data->lock, K_FOREVER);

	if (data->starting) {
		k_mutex_unlock(&data->lock);
		return -EBUSY;
	}

	if (!cancel) {
		/* Non-cancel flush is used as a kick to start capture. */
		ret = video_syna_sr100_enc_kick(dev);
		k_mutex_unlock(&data->lock);
		return ret;
	}

	/* flush(cancel=true) stops streaming and aborts queued buffers. */
	k_mutex_unlock(&data->lock);
	video_syna_sr100_enc_stop(dev);

	return 0;
}

#ifdef CONFIG_POLL
static int video_syna_sr100_enc_set_signal(const struct device *dev,
					struct k_poll_signal *signal)
{
	struct video_syna_sr100_enc_data *data = dev->data;

	k_mutex_lock(&data->lock, K_FOREVER);
	data->signal_out = signal;
	k_mutex_unlock(&data->lock);

	return 0;
}
#endif

static int video_syna_sr100_enc_set_stream(const struct device *dev, bool enable,
						enum video_buf_type type)
{
	struct video_syna_sr100_enc_data *data = dev->data;
	bool do_stop = false;
	int ret = 0;

	if (type != VIDEO_BUF_TYPE_OUTPUT) {
		return -ENOTSUP;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	if (enable) {
		if (atomic_get(&data->streaming) != 0) {
			k_mutex_unlock(&data->lock);
			return 0;
		}

		if (data->starting) {
			k_mutex_unlock(&data->lock);
			return -EBUSY;
		}

		/*
		 * Drop the data lock before start() so long-running pipeline setup
		 * does not hold the state mutex. `starting` blocks concurrent
		 * set_format/stop/start requests while setup is in progress.
		 */
		data->starting = true;
		k_mutex_unlock(&data->lock);
		ret = video_syna_sr100_enc_start(dev);

		k_mutex_lock(&data->lock, K_FOREVER);
		data->starting = false;
		k_mutex_unlock(&data->lock);
		return ret;
	} else {
		if (data->starting) {
			k_mutex_unlock(&data->lock);
			return -EBUSY;
		}

		if (atomic_get(&data->streaming) == 0) {
			k_mutex_unlock(&data->lock);
			return 0;
		}
		do_stop = true;
	}

	k_mutex_unlock(&data->lock);

	if (do_stop) {
		video_syna_sr100_enc_stop(dev);
		LOG_INF("JPEG encoder stream stopped");
	}

	return ret;
}

static int video_syna_sr100_enc_init(const struct device *dev)
{
	const struct video_syna_sr100_enc_config *cfg = dev->config;
	struct video_syna_sr100_enc_data *data = dev->data;

	k_fifo_init(&data->fifo_in);
	k_fifo_init(&data->fifo_out);
	k_mutex_init(&data->lock);
	k_work_init(&data->capture_work, video_syna_sr100_enc_capture_work);

	data->dev = dev;
	data->fmt.type = VIDEO_BUF_TYPE_OUTPUT;
	data->fmt.pixelformat = VIDEO_PIX_FMT_JPEG;
	data->fmt.width = VIDEO_SYNA_SR100_ENC_DEFAULT_WIDTH;
	data->fmt.height = VIDEO_SYNA_SR100_ENC_DEFAULT_HEIGHT;
	data->frmival.numerator = VIDEO_SYNA_SR100_ENC_DEFAULT_FRMIVAL_NUM;
	data->frmival.denominator = VIDEO_SYNA_SR100_ENC_DEFAULT_FRMIVAL_DEN;
	atomic_clear(&data->streaming);
	data->starting = false;
	data->configured = false;
	data->stopping = false;
#ifdef CONFIG_POLL
	data->signal_out = NULL;
#endif
	atomic_clear(&data->capture_inflight);
	data->input_offset = 0U;
	data->output_offset = 0U;
	data->jpeg_size_limit = 0U;
	atomic_set(&data->pending_status, 0);

	if ((cfg->lp_mem_base == 0U) || (cfg->lp_mem_size == 0U)) {
		LOG_WRN("JPEG encoder driver initialized without encoder memory DT data");
	} else {
		LOG_INF("JPEG encoder driver initialized: enc_mem=%p size=%zu raw_off=%zu max_jpeg=%zu (memory-region)",
			(void *)cfg->lp_mem_base, cfg->lp_mem_size, cfg->frame_raw_offset,
			cfg->max_jpeg_size);
	}

	if (cfg->irq_config != NULL) {
		cfg->irq_config();
	}

	return 0;
}

static DEVICE_API(video, video_syna_sr100_enc_driver_api) = {
	.set_format = video_syna_sr100_enc_set_format,
	.get_format = video_syna_sr100_enc_get_format,
	.set_stream = video_syna_sr100_enc_set_stream,
	.get_caps = video_syna_sr100_enc_get_caps,
	.enqueue = video_syna_sr100_enc_enqueue,
	.dequeue = video_syna_sr100_enc_dequeue,
	.flush = video_syna_sr100_enc_flush,
	.set_frmival = video_syna_sr100_enc_set_frmival,
	.get_frmival = video_syna_sr100_enc_get_frmival,
	.enum_frmival = video_syna_sr100_enc_enum_frmival,
#ifdef CONFIG_POLL
	.set_signal = video_syna_sr100_enc_set_signal,
#endif
};

#define VIDEO_SYNA_SR100_ENC_IRQ_CONNECT_0(n)                                                  \
	do {                                                                        \
		IRQ_CONNECT(DT_INST_IRQ_BY_IDX(n, 0, irq),                          \
			    DT_INST_IRQ_BY_IDX(n, 0, priority),                     \
			    video_syna_sr100_enc_irq_frame_done, DEVICE_DT_INST_GET(n), 0);    \
		irq_enable(DT_INST_IRQ_BY_IDX(n, 0, irq));                        \
	} while (0)

#define VIDEO_SYNA_SR100_ENC_IRQ_CONNECT_1(n)                                                  \
	do {                                                                        \
		IRQ_CONNECT(DT_INST_IRQ_BY_IDX(n, 1, irq),                          \
			    DT_INST_IRQ_BY_IDX(n, 1, priority),                     \
			    video_syna_sr100_enc_irq_fmin1_stored, DEVICE_DT_INST_GET(n), 0);  \
		irq_enable(DT_INST_IRQ_BY_IDX(n, 1, irq));                        \
	} while (0)

#define VIDEO_SYNA_SR100_ENC_IRQ_CONNECT_2(n)                                                  \
	do {                                                                        \
		IRQ_CONNECT(DT_INST_IRQ_BY_IDX(n, 2, irq),                          \
			    DT_INST_IRQ_BY_IDX(n, 2, priority),                     \
			    video_syna_sr100_enc_irq_status, DEVICE_DT_INST_GET(n), 0);        \
		irq_enable(DT_INST_IRQ_BY_IDX(n, 2, irq));                        \
	} while (0)

#define VIDEO_SYNA_SR100_ENC_IRQ_CONNECT(idx, n) UTIL_CAT(VIDEO_SYNA_SR100_ENC_IRQ_CONNECT_, idx)(n)

#define VIDEO_SYNA_SR100_ENC_DT_LP_MEM_NODE(n) DT_INST_PHANDLE(n, memory_region)
#define VIDEO_SYNA_SR100_ENC_DT_LP_MEM_BASE(n) DT_REG_ADDR(VIDEO_SYNA_SR100_ENC_DT_LP_MEM_NODE(n))
#define VIDEO_SYNA_SR100_ENC_DT_LP_MEM_SIZE(n) DT_REG_SIZE(VIDEO_SYNA_SR100_ENC_DT_LP_MEM_NODE(n))
#define VIDEO_SYNA_SR100_ENC_DT_LP_MEM_ATTR(n) DT_PROP(VIDEO_SYNA_SR100_ENC_DT_LP_MEM_NODE(n), zephyr_memory_attr)

#define VIDEO_SYNA_SR100_ENC_EP(n) DT_INST_ENDPOINT_BY_ID(n, 0, 0)
#define VIDEO_SYNA_SR100_ENC_REMOTE_NODE(n) DT_NODE_REMOTE_DEVICE(VIDEO_SYNA_SR100_ENC_EP(n))
#define VIDEO_SYNA_SR100_ENC_CSI_LANES(n) DT_PROP_LEN_OR(VIDEO_SYNA_SR100_ENC_EP(n), data_lanes, VIDEO_SYNA_SR100_ENC_DEFAULT_CSI_LANES)

#define VIDEO_SYNA_SR100_ENC_NEEDS_SENSOR(n) IS_EQ(DT_INST_PROP_OR(n, mode, VIDEO_SYNA_SR100_ENC_MODE_SENSOR_TO_MEMORY), \
						VIDEO_SYNA_SR100_ENC_MODE_SENSOR_TO_MEMORY)

#define VIDEO_SYNA_SR100_ENC_SENSOR_DEV_OR_NULL(n)            \
	COND_CODE_1(VIDEO_SYNA_SR100_ENC_NEEDS_SENSOR(n),     \
		    (DEVICE_DT_GET_OR_NULL(VIDEO_SYNA_SR100_ENC_REMOTE_NODE(n))), \
		    (NULL))

#define VIDEO_SYNA_SR100_ENC_INIT(n) \
	BUILD_ASSERT(DT_INST_NODE_HAS_PROP(n, memory_region), \
		     "syna,enc-video [" DT_NODE_FULL_NAME(DT_DRV_INST(n)) \
		     "] requires a memory-region phandle"); \
	BUILD_ASSERT(DT_NODE_HAS_PROP(VIDEO_SYNA_SR100_ENC_DT_LP_MEM_NODE(n), zephyr_memory_attr), \
		     "syna,enc-video [" DT_NODE_FULL_NAME(DT_DRV_INST(n)) \
		     "] memory-region must define " \
		     "zephyr,memory-attr"); \
	BUILD_ASSERT((VIDEO_SYNA_SR100_ENC_DT_LP_MEM_ATTR(n) & DT_MEM_ARM_MPU_RAM_NOCACHE) != 0, \
		     "syna,enc-video [" DT_NODE_FULL_NAME(DT_DRV_INST(n)) \
		     "] memory-region must be non-cacheable"); \
	BUILD_ASSERT(DT_INST_NUM_IRQS(n) <= 3, \
		     "syna,enc-video [" DT_NODE_FULL_NAME(DT_DRV_INST(n)) \
		     "] supports up to three IRQ lines"); \
	static void video_syna_sr100_enc_irq_config_##n(void) \
	{ \
		LISTIFY(DT_INST_NUM_IRQS(n), VIDEO_SYNA_SR100_ENC_IRQ_CONNECT, (;), n); \
	} \
	static const struct video_syna_sr100_enc_config video_syna_sr100_enc_config_##n = { \
		.lp_mem_base = VIDEO_SYNA_SR100_ENC_DT_LP_MEM_BASE(n), \
		.lp_mem_size = VIDEO_SYNA_SR100_ENC_DT_LP_MEM_SIZE(n), \
		.max_jpeg_size = DT_INST_PROP_OR(n, max_jpeg_size, \
						 VIDEO_SYNA_SR100_ENC_DEFAULT_MAX_JPEG_SIZE), \
		.frame_raw_offset = DT_INST_PROP_OR(n, frame_raw_offset, 0), \
		.mode = DT_INST_PROP_OR(n, mode, VIDEO_SYNA_SR100_ENC_MODE_SENSOR_TO_MEMORY), \
		.type = VIDEO_BUF_TYPE_OUTPUT, \
		.sensor_dev = VIDEO_SYNA_SR100_ENC_SENSOR_DEV_OR_NULL(n), \
		.csi_lanes = (uint8_t)VIDEO_SYNA_SR100_ENC_CSI_LANES(n), \
		.csi_id = DT_INST_PROP_OR(n, csi_id, VIDEO_SYNA_SR100_ENC_DEFAULT_CSI_ID), \
		.csi_timing = DT_INST_PROP_OR(n, csi_timing, VIDEO_SYNA_SR100_ENC_DEFAULT_CSI_TIMING), \
		.csi_interface = DT_INST_PROP_OR(n, csi_interface, VIDEO_SYNA_SR100_ENC_DEFAULT_CSI_INTERFACE), \
		.csi_virt_ch = DT_INST_PROP_OR(n, csi_virt_ch, VIDEO_SYNA_SR100_ENC_DEFAULT_CSI_VIRT_CH), \
		.csi_auto_flush = DT_INST_PROP_OR(n, csi_auto_flush, \
						  VIDEO_SYNA_SR100_ENC_DEFAULT_CSI_AUTO_FLUSH), \
		.irq_config = video_syna_sr100_enc_irq_config_##n, \
	}; \
	static struct video_syna_sr100_enc_data video_syna_sr100_enc_data_##n; \
	DEVICE_DT_INST_DEFINE(n, video_syna_sr100_enc_init, NULL, \
			      &video_syna_sr100_enc_data_##n, &video_syna_sr100_enc_config_##n, \
			      POST_KERNEL, CONFIG_VIDEO_INIT_PRIORITY, \
			      &video_syna_sr100_enc_driver_api);

DT_INST_FOREACH_STATUS_OKAY(VIDEO_SYNA_SR100_ENC_INIT)
