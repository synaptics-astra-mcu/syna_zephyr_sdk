/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT syna_mipi_video

#include <errno.h>
#include <string.h>
#include <zephyr/cache.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/devicetree/port-endpoint.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/video.h>
#include <zephyr/drivers/video-controls.h>
#include <zephyr/kernel.h>
#include <zephyr/linker/section_tags.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/barrier.h>
#include <zephyr/sys/util.h>

#include <imgproc_mipi.h>

LOG_MODULE_REGISTER(video_syna_sr100_mipi, CONFIG_VIDEO_LOG_LEVEL);

#define VIDEO_SYNA_DEFAULT_WIDTH      480U
#define VIDEO_SYNA_DEFAULT_HEIGHT     270U
#define VIDEO_SYNA_DEFAULT_MIN_VBUFS  2U
#define VIDEO_SYNA_DEFAULT_ALIGNMENT  64U
#define VIDEO_SYNA_MAX_DATA_LANES     4U
/* Typical Bayer sensor output for this pipeline (sensor -> MIPI input). */
#define VIDEO_SYNA_PIXEL_FORMAT_RAW10 VIDEO_PIX_FMT_SBGGR10P
/* Application-visible capture output (MIPI writes RAW8 into SHM). */
#define VIDEO_SYNA_PIXEL_FORMAT_RAW8  VIDEO_PIX_FMT_SBGGR8

#if IS_ENABLED(CONFIG_VIDEO_SAMPLE_RES_FHD)
/* FHD sample uses a DT-reserved DMA-only pool; avoid reserving extra SRAM. */
#define VIDEO_SYNA_INTERNAL_SHM_POOL_SIZE 0U
#else
/* Fixed-size internal SHM pool for small-frame CPU-accessible captures. */
#define VIDEO_SYNA_INTERNAL_SHM_POOL_SIZE 524288U
#endif

#define VIDEO_SYNA_INTERNAL_SHM_POOL_SIZE_BYTES ((size_t)VIDEO_SYNA_INTERNAL_SHM_POOL_SIZE)

#define VIDEO_SYNA_INIT_PRIORITY CONFIG_VIDEO_SYNA_MIPI_INIT_PRIORITY

#define VIDEO_SYNA_CLK_DEV(n) DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(n))
#define VIDEO_SYNA_CLK_SUBSYS(n) ((clock_control_subsys_t)(uintptr_t)DT_INST_CLOCKS_CELL(n, clkid))

#define VIDEO_SYNA_DT_SHM_ADDR(n) DT_REG_ADDR(DT_INST_PHANDLE(n, memory_region))
#define VIDEO_SYNA_DT_SHM_SIZE(n) DT_REG_SIZE(DT_INST_PHANDLE(n, memory_region))

#define VIDEO_SYNA_SHM_POOL_ADDR(n)                                                  \
	COND_CODE_1(DT_INST_NODE_HAS_PROP(n, memory_region),                          \
		    (VIDEO_SYNA_DT_SHM_ADDR(n)),                                      \
		    (DT_INST_PROP_OR(n, shm_pool_addr, 0)))

#define VIDEO_SYNA_SHM_POOL_SIZE(n)                                                  \
	COND_CODE_1(DT_INST_NODE_HAS_PROP(n, memory_region),                          \
		    (VIDEO_SYNA_DT_SHM_SIZE(n)),                                      \
		    (DT_INST_PROP_OR(n, shm_pool_size, 0)))

#define VIDEO_SYNA_NUM_LANES(n) DT_INST_PROP_OR(n, num_lanes, 1)
#define VIDEO_SYNA_INTERFACE_WIDTH(n) DT_INST_PROP_OR(n, interface_width, 0)
#define VIDEO_SYNA_ENDPOINT(n) DT_INST_ENDPOINT_BY_ID(n, 0, 0)
#define VIDEO_SYNA_SENSOR_DEV(n) DEVICE_DT_GET(DT_NODE_REMOTE_DEVICE(VIDEO_SYNA_ENDPOINT(n)))
#define VIDEO_SYNA_IRQ_FLAGS(n)                                                   \
	COND_CODE_1(DT_INST_IRQ_HAS_CELL(n, flags),                             \
		    (DT_INST_IRQ(n, flags)),                                    \
		    (0))

#if IS_ENABLED(CONFIG_POLL)
#define VIDEO_SYNA_RAISE_SIGNAL(_data, _result)                                     \
	do {                                                                        \
		if ((_data)->signal_out != NULL) {                                   \
			k_poll_signal_raise((_data)->signal_out, (_result));          \
		}                                                                   \
	} while (0)
#else
#define VIDEO_SYNA_RAISE_SIGNAL(_data, _result) do { } while (0)
#endif

struct video_syna_sr100_mipi_config {
	uintptr_t shm_pool_addr;
	size_t shm_pool_size;
	const struct device *clk_dev;
	clock_control_subsys_t clk_subsys;
	uint8_t csi_id;
	uint8_t data_lanes;
	uint8_t interface_width;
	const struct device *sensor_dev;
	enum video_buf_type type;
	bool has_irq;
	int irqn;
	void (*mipi_cb)(uint32_t status);
};

struct video_syna_sr100_mipi_data {
	const struct device *dev;
	const struct device *sensor_dev;
	struct video_format fmt;
	struct video_frmival frmival;
	struct k_fifo fifo_in;
	struct k_fifo fifo_out;
	struct k_fifo fifo_complete;
	struct k_work kick_work;
	struct k_work complete_work;
	struct k_mutex lock;
#if IS_ENABLED(CONFIG_POLL)
	struct k_poll_signal *signal_out;
#endif
	uint32_t lane_rate_kbps;
	uint8_t input_bit_depth;
	uintptr_t shm_pool_addr;
	size_t shm_pool_size;
	size_t frame_size;
	bool shm_cpu_accessible;
	atomic_t streaming;
	atomic_t capture_inflight;
	bool lib_initialized;
};

K_MUTEX_DEFINE(video_syna_sr100_mipi_init_lock);
static atomic_t video_syna_sr100_mipi_inited;
static atomic_t video_syna_sr100_mipi_clk_users;

#if VIDEO_SYNA_INTERNAL_SHM_POOL_SIZE > 0
static __noinit uint8_t video_syna_sr100_mipi_internal_shm_pool[VIDEO_SYNA_INTERNAL_SHM_POOL_SIZE_BYTES]
	__aligned(VIDEO_SYNA_DEFAULT_ALIGNMENT);
#endif

static const struct video_format_cap video_syna_sr100_mipi_format_caps[] = {
	{
		.pixelformat = VIDEO_SYNA_PIXEL_FORMAT_RAW8,
		.width_min = 64,
		.width_max = 1920,
		.height_min = 64,
		.height_max = 1080,
		.width_step = 2,
		.height_step = 2,
	},
	{ 0 }
};

static int video_syna_sr100_mipi_kick(const struct device *dev);
static void video_syna_sr100_mipi_kick_work(struct k_work *work);
static void video_syna_sr100_mipi_complete_work(struct k_work *work);
static void video_syna_sr100_mipi_callback_handle(const struct device *dev, uint32_t status);

extern void mipi_dma_interrupt_handler(void);
extern char _image_ram_start[];
extern char __kernel_ram_end[];

static uint8_t video_syna_sr100_mipi_pixfmt_to_bpp(uint32_t pixelformat)
{
	switch (pixelformat) {
	case VIDEO_PIX_FMT_SBGGR8:
	case VIDEO_PIX_FMT_SGBRG8:
	case VIDEO_PIX_FMT_SGRBG8:
	case VIDEO_PIX_FMT_SRGGB8:
		return 8U;
	case VIDEO_PIX_FMT_SBGGR10P:
	case VIDEO_PIX_FMT_SGBRG10P:
	case VIDEO_PIX_FMT_SGRBG10P:
	case VIDEO_PIX_FMT_SRGGB10P:
		return 10U;
	default:
		return 0U;
	}
}

static int video_syna_sr100_mipi_clocks_apply(const struct device *clk_dev,
					      clock_control_subsys_t clk_subsys,
					      bool enable)
{
	int ret;

	if ((clk_dev == NULL) || !device_is_ready(clk_dev)) {
		return enable ? -ENODEV : 0;
	}

	if (!enable) {
		/* Ignore extra "off" calls when no active users remain. */
		if (atomic_get(&video_syna_sr100_mipi_clk_users) <= 0) {
			return 0;
		}

		if (atomic_dec(&video_syna_sr100_mipi_clk_users) != 1) {
			return 0;
		}

		(void)clock_control_off(clk_dev, clk_subsys);

		return 0;
	}

	/*
	 * Fast path: clocks are already enabled by another user.
	 * Keep an extra refcount and return.
	 */
	if (atomic_get(&video_syna_sr100_mipi_clk_users) > 0) {
		(void)atomic_inc(&video_syna_sr100_mipi_clk_users);
		return 0;
	}

	/* Refcount to tolerate multiple init/start paths calling enable. */
	if (atomic_inc(&video_syna_sr100_mipi_clk_users) > 0) {
		return 0;
	}

	ret = clock_control_on(clk_dev, clk_subsys);
	if (ret != 0) {
		(void)atomic_dec(&video_syna_sr100_mipi_clk_users);
		return ret;
	}

	return ret;
}

static int video_syna_sr100_mipi_lib_init(const struct device *dev)
{
	const struct video_syna_sr100_mipi_config *cfg = dev->config;
	struct video_syna_sr100_mipi_data *data = dev->data;
	int ret;

	/* Initialize the underlying library at most one time per boot. */
	if (data->lib_initialized) {
		return 0;
	}

	/* Library init is global; ensure exactly one successful init. */
	if (atomic_get(&video_syna_sr100_mipi_inited) == 0) {
		k_mutex_lock(&video_syna_sr100_mipi_init_lock, K_FOREVER);
		if (atomic_get(&video_syna_sr100_mipi_inited) == 0) {
			ret = imgproc_mipi_init(cfg->csi_id) ? 0 : -EIO;
			if (ret != 0) {
				k_mutex_unlock(&video_syna_sr100_mipi_init_lock);
				LOG_ERR("imgproc_mipi_init(csi=%u) failed", (uint32_t)cfg->csi_id);
				return ret;
			}

			atomic_set(&video_syna_sr100_mipi_inited, 1);
		}
		k_mutex_unlock(&video_syna_sr100_mipi_init_lock);
	}

	data->lib_initialized = true;
	return 0;
}

static int video_syna_sr100_mipi_shm_prepare(const struct device *dev)
{
	const struct video_syna_sr100_mipi_config *cfg = dev->config;
	struct video_syna_sr100_mipi_data *data = dev->data;
	uintptr_t ram_start = (uintptr_t)_image_ram_start;
	uintptr_t ram_end = (uintptr_t)__kernel_ram_end;
	bool dt_pool_present;
	bool dt_pool_cpu_accessible;
	uintptr_t resolved_addr = cfg->shm_pool_addr;
	size_t resolved_size = cfg->shm_pool_size;

	/* Resolve a CPU-accessible SHM pool for the DHUB capture output. */
	data->frame_size = (data->fmt.size != 0U) ? (size_t)data->fmt.size :
			  (data->fmt.pitch != 0U) ? ((size_t)data->fmt.pitch * (size_t)data->fmt.height) :
						    ((size_t)data->fmt.width * (size_t)data->fmt.height);
	if (data->frame_size == 0U) {
		return -EINVAL;
	}

	dt_pool_present = (resolved_addr != 0U) && (resolved_size != 0U);
	/*
	 * A devicetree memory-region reserves address space for the DMA target,
	 * but it does not guarantee the CPU can safely dereference that region.
	 * Only treat the DT pool as CPU-accessible when it actually falls inside
	 * the active Zephyr RAM window.
	 */
	dt_pool_cpu_accessible = (resolved_size != 0U) &&
				 (resolved_addr >= ram_start) &&
				 (resolved_addr < ram_end) &&
				 (resolved_size <= (ram_end - resolved_addr));

	/*
	 * Prefer a CPU-accessible pool for small frames (so we can memcpy into
	 * application buffers). For large frames, allow a DMA-only DT pool when
	 * it is big enough (FHD use-case where the app consumes the SHM buffer
	 * directly).
	 */
	if (dt_pool_present && !dt_pool_cpu_accessible) {
#if VIDEO_SYNA_INTERNAL_SHM_POOL_SIZE > 0
		if (data->frame_size <= VIDEO_SYNA_INTERNAL_SHM_POOL_SIZE_BYTES) {
			resolved_addr = (uintptr_t)video_syna_sr100_mipi_internal_shm_pool;
			resolved_size = sizeof(video_syna_sr100_mipi_internal_shm_pool);
			dt_pool_cpu_accessible = true;
		}
#endif
	}

	/* If still not resolved, require a DT pool (DMA-only is acceptable). */
	if (!dt_pool_present) {
	#if VIDEO_SYNA_INTERNAL_SHM_POOL_SIZE > 0
		resolved_addr = (uintptr_t)video_syna_sr100_mipi_internal_shm_pool;
		resolved_size = sizeof(video_syna_sr100_mipi_internal_shm_pool);
		dt_pool_cpu_accessible = true;
	#else
		LOG_ERR("No SHM pool configured (DT pool missing and internal SHM pool disabled)");
		return -ENOTSUP;
	#endif
	}

	if (data->frame_size > resolved_size) {
		return -ENOMEM;
	}

	if (!IS_ALIGNED(resolved_addr, VIDEO_SYNA_DEFAULT_ALIGNMENT)) {
		LOG_ERR("SHM pool addr %p is not %u-byte aligned",
			(void *)resolved_addr, VIDEO_SYNA_DEFAULT_ALIGNMENT);
		return -EINVAL;
	}

	data->shm_pool_addr = resolved_addr;
	data->shm_pool_size = resolved_size;
	data->shm_cpu_accessible = dt_pool_cpu_accessible;

	if (data->shm_cpu_accessible) {
		memset((void *)data->shm_pool_addr, 0, data->frame_size);
		/*
		 * The SHM pool is written by the CPU (memset) and then by DHUB DMA.
		 * Flush+invalidate after memset to avoid writing back cached zeros over
		 * the DMA result.
		 */
	#if IS_ENABLED(CONFIG_CACHE_MANAGEMENT) && IS_ENABLED(CONFIG_DCACHE)
		sys_cache_data_flush_range((void *)data->shm_pool_addr, data->frame_size);
		barrier_dsync_fence_full();
		sys_cache_data_invd_range((void *)data->shm_pool_addr, data->frame_size);
		barrier_dsync_fence_full();
	#endif
	}

	if ((uintptr_t)cfg->shm_pool_addr != data->shm_pool_addr) {
		LOG_INF("Using internal SHM pool: addr=%p size=%zu",
			(void *)data->shm_pool_addr, (uint32_t)data->shm_pool_size);
	} else {
		LOG_INF("Using DT SHM pool: addr=%p size=%zu",
			(void *)data->shm_pool_addr, data->shm_pool_size);
	}

	return 0;
}

static int video_syna_sr100_mipi_kick(const struct device *dev)
{
	const struct video_syna_sr100_mipi_config *cfg = dev->config;
	struct video_syna_sr100_mipi_data *data = dev->data;

	/* Trigger a one-shot capture if streaming and a buffer is queued. */
	if (atomic_get(&data->streaming) == 0) {
		return 0;
	}

	if (k_fifo_peek_head(&data->fifo_in) == NULL) {
		return 0;
	}

	if (!atomic_cas(&data->capture_inflight, 0, 1)) {
		return 0;
	}

	/*
	 * When capturing into cached SRAM (e.g. the internal SHM pool in .noinit),
	 * ensure there are no dirty cache lines that could be written back over the
	 * DMA result, and avoid the CPU observing stale lines after DMA completes.
	 *
	 * The completion path invalidates again before handing buffers back, but we
	 * also invalidate here to discard any dirty lines from previous users of the
	 * buffer before kicking the next DMA capture.
	 */
	if (data->shm_cpu_accessible) {
	#if IS_ENABLED(CONFIG_CACHE_MANAGEMENT) && IS_ENABLED(CONFIG_DCACHE)
		sys_cache_data_invd_range((void *)data->shm_pool_addr, data->frame_size);
		barrier_dsync_fence_full();
	#endif
	}

	barrier_dsync_fence_full();

	if (!imgproc_mipi_trigger(cfg->csi_id)) {
		int mipi_status = imgproc_mipi_status(cfg->csi_id);

		LOG_WRN("imgproc_mipi_trigger(csi=%u) failed", (uint32_t)cfg->csi_id);
		LOG_WRN("imgproc_mipi_status(csi=%u) -> %d", (uint32_t)cfg->csi_id, mipi_status);

		atomic_clear(&data->capture_inflight);

		return -EIO;
	}

	return 0;
}

static int video_syna_sr100_mipi_start(const struct device *dev)
{
	const struct video_syna_sr100_mipi_config *cfg = dev->config;
	struct video_syna_sr100_mipi_data *data = dev->data;
	uint32_t shm_dst_addr32;
	struct video_format sensor_fmt;
	struct video_format active_sensor_fmt;
	struct video_control ctrl;
	imgproc_mipi_cfg_t imgproc_cfg;
	uint32_t pixel_rate = 0U;
	uint8_t bpp;
	int ret;

	if ((data->sensor_dev == NULL) || !device_is_ready(data->sensor_dev)) {
		LOG_ERR("Sensor device is not ready");
		return -ENODEV;
	}

	ret = video_syna_sr100_mipi_clocks_apply(cfg->clk_dev, cfg->clk_subsys, true);
	if (ret != 0) {
		LOG_ERR("Failed to enable ImgProc clocks: %d", ret);
		return ret;
	}

	/* Start the capture pipeline for the currently configured format. */
	ret = video_syna_sr100_mipi_lib_init(dev);
	if (ret != 0) {
		(void)video_syna_sr100_mipi_clocks_apply(cfg->clk_dev, cfg->clk_subsys, false);
		return ret;
	}

	ret = video_syna_sr100_mipi_shm_prepare(dev);
	if (ret != 0) {
		LOG_ERR("Invalid capture memory layout: %d", ret);
		(void)video_syna_sr100_mipi_clocks_apply(cfg->clk_dev, cfg->clk_subsys, false);
		return ret;
	}

	/*
	 * Ensure the sensor output mode matches the configured capture size so
	 * subsequent video_get_format() and VIDEO_CID_PIXEL_RATE queries reflect
	 * the active sensor mode.
	 */
	sensor_fmt = (struct video_format){
		.type = VIDEO_BUF_TYPE_OUTPUT,
		.pixelformat = VIDEO_SYNA_PIXEL_FORMAT_RAW10,
		.width = data->fmt.width,
		.height = data->fmt.height,
	};

	active_sensor_fmt = (struct video_format){
		.type = VIDEO_BUF_TYPE_OUTPUT,
	};
	ret = video_get_format(data->sensor_dev, &active_sensor_fmt);
	if (ret != 0) {
		/*
		 * If the sensor cannot report its current mode, fall back to
		 * programming it explicitly.
		 */
		active_sensor_fmt = (struct video_format){
			.type = VIDEO_BUF_TYPE_OUTPUT,
		};
	}

	bpp = (ret == 0) ? video_syna_sr100_mipi_pixfmt_to_bpp(active_sensor_fmt.pixelformat) : 0U;

	if ((ret != 0) || (active_sensor_fmt.width != sensor_fmt.width) ||
	    (active_sensor_fmt.height != sensor_fmt.height) || (bpp == 0U)) {
		ret = video_set_format(data->sensor_dev, &sensor_fmt);
		if (ret != 0) {
			LOG_ERR("Sensor video_set_format(%ux%u pix=0x%x) failed: %d",
				sensor_fmt.width, sensor_fmt.height,
				sensor_fmt.pixelformat, ret);
			(void)video_syna_sr100_mipi_clocks_apply(cfg->clk_dev,
								cfg->clk_subsys, false);
			return ret;
		}

		active_sensor_fmt = (struct video_format){
			.type = VIDEO_BUF_TYPE_OUTPUT,
		};
		ret = video_get_format(data->sensor_dev, &active_sensor_fmt);
		if (ret != 0) {
			LOG_ERR("Sensor video_get_format() failed: %d", ret);
			(void)video_syna_sr100_mipi_clocks_apply(cfg->clk_dev,
								cfg->clk_subsys, false);
			return ret;
		}

		bpp = video_syna_sr100_mipi_pixfmt_to_bpp(active_sensor_fmt.pixelformat);
		if (bpp == 0U) {
			LOG_ERR("Sensor output pixelformat 0x%x is unsupported",
				active_sensor_fmt.pixelformat);
			(void)video_syna_sr100_mipi_clocks_apply(cfg->clk_dev,
								cfg->clk_subsys, false);
			return -ENOTSUP;
		}
	}

	ctrl = (struct video_control){
		.id = VIDEO_CID_PIXEL_RATE,
	};
	ret = video_get_ctrl(data->sensor_dev, &ctrl);
	if (ret == 0) {
		pixel_rate = (uint32_t)ctrl.val64;
	}
	if ((ret != 0) || (pixel_rate == 0U) || (cfg->data_lanes == 0U)) {
		LOG_ERR("Missing sensor pixel-rate for active mode (ret=%d rate=%u)",
			ret, pixel_rate);
		(void)video_syna_sr100_mipi_clocks_apply(cfg->clk_dev, cfg->clk_subsys, false);
		return -ENOTSUP;
	}

	data->lane_rate_kbps = (uint32_t)(((uint64_t)pixel_rate * (uint64_t)bpp) /
					  (uint64_t)cfg->data_lanes / 1000ULL);
	data->input_bit_depth = bpp;

	memset(&imgproc_cfg, 0, sizeof(imgproc_cfg));
	imgproc_cfg.width = data->fmt.width;
	imgproc_cfg.height = data->fmt.height;
	imgproc_cfg.lane_rate_kbps = data->lane_rate_kbps;
	imgproc_cfg.interface_width = cfg->interface_width;
	imgproc_cfg.format = (data->input_bit_depth <= 8U) ?
		IMGPROC_MIPI_VIDEO_FORMAT_RAW8 : IMGPROC_MIPI_VIDEO_FORMAT_RAW10;

	LOG_INF("Sensor input bpp=%u, driver output pixelformat=0x%x",
		(uint32_t)data->input_bit_depth, data->fmt.pixelformat);

	shm_dst_addr32 = (uint32_t)data->shm_pool_addr;

	LOG_INF("Configuring csi=%u shm=%p size=%zu",
		(uint32_t)cfg->csi_id, (void *)data->shm_pool_addr, data->frame_size);

	if (!imgproc_mipi_configure(cfg->csi_id, &imgproc_cfg, shm_dst_addr32)) {
		if (cfg->has_irq) {
			irq_disable(cfg->irqn);
		}
		/* Ensure any partially-created datapath state is released. */
		(void)imgproc_mipi_release(cfg->csi_id);
		LOG_ERR("imgproc_mipi_configure(csi=%u) failed", (uint32_t)cfg->csi_id);
		(void)video_syna_sr100_mipi_clocks_apply(cfg->clk_dev, cfg->clk_subsys, false);
		return -EIO;
	}

	if (!imgproc_mipi_start(cfg->csi_id, cfg->mipi_cb)) {
		if (cfg->has_irq) {
			irq_disable(cfg->irqn);
		}
		LOG_ERR("imgproc_mipi_start(csi=%u) failed", (uint32_t)cfg->csi_id);
		(void)imgproc_mipi_stop(cfg->csi_id);
		(void)imgproc_mipi_release(cfg->csi_id);
		(void)video_syna_sr100_mipi_clocks_apply(cfg->clk_dev, cfg->clk_subsys, false);
		return -EIO;
	}

	if (cfg->has_irq) {
		irq_enable(cfg->irqn);
	}

	ret = video_stream_start(data->sensor_dev, VIDEO_BUF_TYPE_OUTPUT);
	if (ret != 0) {
		if (cfg->has_irq) {
			irq_disable(cfg->irqn);
		}
		(void)imgproc_mipi_stop(cfg->csi_id);
		(void)imgproc_mipi_release(cfg->csi_id);
		(void)video_syna_sr100_mipi_clocks_apply(cfg->clk_dev, cfg->clk_subsys, false);
		return ret;
	}

	atomic_set(&data->streaming, 1);
	atomic_clear(&data->capture_inflight);

	LOG_INF("Stream started (%ux%u size=%u)",
		data->fmt.width, data->fmt.height, (uint32_t)data->frame_size);
	return 0;
}

static void video_syna_sr100_mipi_stop(const struct device *dev)
{
	const struct video_syna_sr100_mipi_config *cfg = dev->config;
	struct video_syna_sr100_mipi_data *data = dev->data;
	struct video_buffer *vbuf;
	bool was_streaming = (atomic_get(&data->streaming) != 0);
	struct k_work_sync complete_sync;
	struct k_work_sync kick_sync;

	/* Stop capture and release the active datapath. */
	if (cfg->has_irq) {
		irq_disable(cfg->irqn);
	}

	/* Prevent callbacks/work from completing buffers after stop. */
	atomic_clear(&data->streaming);
	atomic_clear(&data->capture_inflight);

	if (data->sensor_dev != NULL) {
		(void)video_stream_stop(data->sensor_dev, VIDEO_BUF_TYPE_OUTPUT);
	}

	if (was_streaming) {
		(void)imgproc_mipi_stop(cfg->csi_id);
	}

	/*
	 * Always release the datapath configuration so the next start/configure
	 * begins from a clean state (stop() is halt-only).
	 */
	(void)imgproc_mipi_release(cfg->csi_id);

	/* Cancel any pending deferred work and drain completion/queued buffers. */
	(void)k_work_cancel_sync(&data->complete_work, &complete_sync);
	(void)k_work_cancel_sync(&data->kick_work, &kick_sync);

	while ((vbuf = k_fifo_get(&data->fifo_complete, K_NO_WAIT)) != NULL) {
		vbuf->bytesused = 0U;
		k_fifo_put(&data->fifo_out, vbuf);
		VIDEO_SYNA_RAISE_SIGNAL(data, VIDEO_BUF_ABORTED);
	}

	while ((vbuf = k_fifo_get(&data->fifo_in, K_NO_WAIT)) != NULL) {
		vbuf->bytesused = 0U;
		k_fifo_put(&data->fifo_out, vbuf);
		VIDEO_SYNA_RAISE_SIGNAL(data, VIDEO_BUF_ABORTED);
	}

	if (was_streaming) {
		(void)video_syna_sr100_mipi_clocks_apply(cfg->clk_dev, cfg->clk_subsys, false);
	}
}

static void video_syna_sr100_mipi_callback_handle(const struct device *dev, uint32_t status)
{
	struct video_syna_sr100_mipi_data *data;
	struct video_buffer *vbuf;
	size_t frame_size;
	bool in_isr = k_is_in_isr();
	uintptr_t ram_start = (uintptr_t)_image_ram_start;
	uintptr_t ram_end = (uintptr_t)__kernel_ram_end;

	if (dev == NULL) {
		return;
	}

	data = dev->data;
	atomic_clear(&data->capture_inflight);

	/*
	 * For CSI->DHUB, the library uses the callback as the frame-complete signal.
	 * The status value is not meaningful for this path, so treat any callback
	 * invocation as "frame ready".
	 */
	ARG_UNUSED(status);

	if (atomic_get(&data->streaming) == 0) {
		return;
	}

	vbuf = k_fifo_get(&data->fifo_in, K_NO_WAIT);
	if (vbuf == NULL) {
		LOG_WRN("Capture completed with no queued video buffer");
		return;
	}

	frame_size = MIN((size_t)vbuf->size, data->frame_size);

	if (in_isr) {
		/*
		 * Avoid copying multi-megabyte frames in interrupt context.
		 * Stash the expected frame size and defer the cache+memcpy work
		 * to the system workqueue.
		 */
		if (((uintptr_t)vbuf->buffer == data->shm_pool_addr) && !data->shm_cpu_accessible) {
			vbuf->bytesused = frame_size;
			vbuf->timestamp = k_uptime_get_32();
			vbuf->line_offset = 0U;
			k_fifo_put(&data->fifo_out, vbuf);
			VIDEO_SYNA_RAISE_SIGNAL(data, VIDEO_BUF_DONE);
			(void)k_work_submit(&data->kick_work);
			return;
		}

		vbuf->bytesused = frame_size;
		k_fifo_put(&data->fifo_complete, vbuf);
		(void)k_work_submit(&data->complete_work);
		return;
	}

	/*
	 * For DMA-only destination addresses (not CPU accessible), only support
	 * buffers that match the active SHM pool address. Never rewrite the
	 * application's vbuf->buffer pointer.
	 */
	if ((frame_size == 0U) ||
	    ((uintptr_t)vbuf->buffer < ram_start) ||
	    ((uintptr_t)vbuf->buffer >= ram_end) ||
	    (frame_size > (ram_end - (uintptr_t)vbuf->buffer))) {
		if ((uintptr_t)vbuf->buffer != data->shm_pool_addr) {
			vbuf->bytesused = 0U;
			k_fifo_put(&data->fifo_out, vbuf);
			VIDEO_SYNA_RAISE_SIGNAL(data, VIDEO_BUF_ABORTED);
			(void)k_work_submit(&data->kick_work);
			return;
		}

		vbuf->bytesused = frame_size;
		vbuf->timestamp = k_uptime_get_32();
		vbuf->line_offset = 0U;
		k_fifo_put(&data->fifo_out, vbuf);
		VIDEO_SYNA_RAISE_SIGNAL(data, VIDEO_BUF_DONE);
		(void)k_work_submit(&data->kick_work);
		return;
	}

	if (!data->shm_cpu_accessible && ((uintptr_t)vbuf->buffer != data->shm_pool_addr)) {
		vbuf->bytesused = 0U;
		k_fifo_put(&data->fifo_out, vbuf);
		VIDEO_SYNA_RAISE_SIGNAL(data, VIDEO_BUF_ABORTED);
		(void)k_work_submit(&data->kick_work);
		return;
	}

	if (data->shm_cpu_accessible) {
#if IS_ENABLED(CONFIG_CACHE_MANAGEMENT) && IS_ENABLED(CONFIG_DCACHE)
		sys_cache_data_invd_range((void *)data->shm_pool_addr, frame_size);
		barrier_dsync_fence_full();
#endif
	}
	if ((uintptr_t)vbuf->buffer != data->shm_pool_addr) {
		memcpy(vbuf->buffer, (const void *)data->shm_pool_addr, frame_size);
#if IS_ENABLED(CONFIG_CACHE_MANAGEMENT) && IS_ENABLED(CONFIG_DCACHE)
		sys_cache_data_flush_range((void *)vbuf->buffer, frame_size);
		barrier_dsync_fence_full();
#endif
	}

	vbuf->bytesused = frame_size;
	vbuf->timestamp = k_uptime_get_32();
	vbuf->line_offset = 0U;
	k_fifo_put(&data->fifo_out, vbuf);
	VIDEO_SYNA_RAISE_SIGNAL(data, VIDEO_BUF_DONE);
	(void)k_work_submit(&data->kick_work);
}

static void video_syna_sr100_mipi_complete_work(struct k_work *work)
{
	struct video_syna_sr100_mipi_data *data =
		CONTAINER_OF(work, struct video_syna_sr100_mipi_data, complete_work);
	const struct device *dev = data->dev;
	struct video_buffer *vbuf;
	uintptr_t ram_start = (uintptr_t)_image_ram_start;
	uintptr_t ram_end = (uintptr_t)__kernel_ram_end;

	while ((vbuf = k_fifo_get(&data->fifo_complete, K_NO_WAIT)) != NULL) {
		size_t frame_size = vbuf->bytesused;

		if ((dev == NULL) || (atomic_get(&data->streaming) == 0)) {
			vbuf->bytesused = 0U;
			k_fifo_put(&data->fifo_out, vbuf);
			VIDEO_SYNA_RAISE_SIGNAL(data, VIDEO_BUF_ABORTED);
			continue;
		}

		frame_size = MIN((size_t)vbuf->size, frame_size);

		if ((frame_size == 0U) ||
		    ((uintptr_t)vbuf->buffer < ram_start) ||
		    ((uintptr_t)vbuf->buffer >= ram_end) ||
		    (frame_size > (ram_end - (uintptr_t)vbuf->buffer))) {
			if ((uintptr_t)vbuf->buffer != data->shm_pool_addr) {
				vbuf->bytesused = 0U;
				k_fifo_put(&data->fifo_out, vbuf);
				VIDEO_SYNA_RAISE_SIGNAL(data, VIDEO_BUF_ABORTED);
				(void)k_work_submit(&data->kick_work);
				continue;
			}

			vbuf->bytesused = frame_size;
			vbuf->timestamp = k_uptime_get_32();
			vbuf->line_offset = 0U;
			k_fifo_put(&data->fifo_out, vbuf);
			VIDEO_SYNA_RAISE_SIGNAL(data, VIDEO_BUF_DONE);
			(void)k_work_submit(&data->kick_work);
			continue;
		}

		if (!data->shm_cpu_accessible) {
			vbuf->bytesused = 0U;
			k_fifo_put(&data->fifo_out, vbuf);
			VIDEO_SYNA_RAISE_SIGNAL(data, VIDEO_BUF_ABORTED);
			(void)k_work_submit(&data->kick_work);
			continue;
		}

#if IS_ENABLED(CONFIG_CACHE_MANAGEMENT) && IS_ENABLED(CONFIG_DCACHE)
		sys_cache_data_invd_range((void *)data->shm_pool_addr, frame_size);
		barrier_dsync_fence_full();
#endif
		if ((uintptr_t)vbuf->buffer != data->shm_pool_addr) {
			memcpy(vbuf->buffer, (const void *)data->shm_pool_addr, frame_size);
#if IS_ENABLED(CONFIG_CACHE_MANAGEMENT) && IS_ENABLED(CONFIG_DCACHE)
			sys_cache_data_flush_range((void *)vbuf->buffer, frame_size);
			barrier_dsync_fence_full();
#endif
		}

		vbuf->bytesused = frame_size;
		vbuf->timestamp = k_uptime_get_32();
		vbuf->line_offset = 0U;
		k_fifo_put(&data->fifo_out, vbuf);
		VIDEO_SYNA_RAISE_SIGNAL(data, VIDEO_BUF_DONE);
		(void)k_work_submit(&data->kick_work);
	}
}

static void video_syna_sr100_mipi_kick_work(struct k_work *work)
{
	struct video_syna_sr100_mipi_data *data =
		CONTAINER_OF(work, struct video_syna_sr100_mipi_data, kick_work);
	const struct device *dev = data->dev;

	if ((dev == NULL) || (atomic_get(&data->streaming) == 0)) {
		return;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	(void)video_syna_sr100_mipi_kick(dev);
	k_mutex_unlock(&data->lock);
}

static int video_syna_sr100_mipi_enqueue(const struct device *dev, struct video_buffer *vbuf)
{
	struct video_syna_sr100_mipi_data *data = dev->data;
	const struct video_syna_sr100_mipi_config *cfg = dev->config;
	int ret;
	uintptr_t ram_start = (uintptr_t)_image_ram_start;
	uintptr_t ram_end = (uintptr_t)__kernel_ram_end;
	size_t frame_size;
	bool in_ram;

	if ((vbuf == NULL) || (vbuf->buffer == NULL)) {
		return -EINVAL;
	}

	if (vbuf->type != data->fmt.type) {
		return -EINVAL;
	}

	if (vbuf->size == 0U) {
		vbuf->size = data->fmt.size;
	}

	{
		size_t fmt_frame_size = (data->frame_size != 0U) ? data->frame_size :
				      (data->fmt.size != 0U) ? (size_t)data->fmt.size :
				      (data->fmt.pitch != 0U) ?
					      ((size_t)data->fmt.pitch * (size_t)data->fmt.height) :
					      ((size_t)data->fmt.width * (size_t)data->fmt.height);

		frame_size = MIN((size_t)vbuf->size, fmt_frame_size);
	}

	in_ram = (frame_size != 0U) &&
		 ((uintptr_t)vbuf->buffer >= ram_start) &&
		 ((uintptr_t)vbuf->buffer < ram_end) &&
		 (frame_size <= (ram_end - (uintptr_t)vbuf->buffer));
	if (!in_ram) {
		/*
		 * DMA-only buffers are supported only when they match the SHM pool
		 * address that the selected datapath is configured to use.
		 *
		 * - While streaming: require the resolved pool address.
		 * - Before streaming: allow the DT pool address only when we can
		 *   predict the DT pool will be selected for the current frame size.
		 */
		if (atomic_get(&data->streaming) != 0) {
			if ((uintptr_t)vbuf->buffer != data->shm_pool_addr) {
				return -EINVAL;
			}
			} else {
				bool dt_pool_present = (cfg->shm_pool_addr != 0U) && (cfg->shm_pool_size != 0U);

				if ((uintptr_t)vbuf->buffer != cfg->shm_pool_addr) {
					return -EINVAL;
				}

				if (!dt_pool_present || (frame_size > cfg->shm_pool_size)) {
					return -EINVAL;
				}

	#if VIDEO_SYNA_INTERNAL_SHM_POOL_SIZE > 0
				bool dt_pool_cpu_accessible =
					dt_pool_present && (cfg->shm_pool_addr >= ram_start) &&
					(cfg->shm_pool_addr < ram_end) &&
					(cfg->shm_pool_size <= (ram_end - cfg->shm_pool_addr));

				if (!dt_pool_cpu_accessible &&
				    (frame_size <= VIDEO_SYNA_INTERNAL_SHM_POOL_SIZE_BYTES)) {
					return -EINVAL;
				}
	#endif
			}
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	k_fifo_put(&data->fifo_in, vbuf);
	ret = video_syna_sr100_mipi_kick(dev);
	k_mutex_unlock(&data->lock);

	return ret;
}

static int video_syna_sr100_mipi_dequeue(const struct device *dev, struct video_buffer **vbuf,
				      k_timeout_t timeout)
{
	struct video_syna_sr100_mipi_data *data = dev->data;

	if (vbuf == NULL) {
		return -EINVAL;
	}

	*vbuf = k_fifo_get(&data->fifo_out, timeout);
	if (*vbuf == NULL) {
		return -EAGAIN;
	}

	return 0;
}

static int video_syna_sr100_mipi_set_format(const struct device *dev, struct video_format *fmt)
{
	struct video_syna_sr100_mipi_data *data = dev->data;
	const struct video_syna_sr100_mipi_config *cfg = dev->config;
	const struct device *sensor_dev;
	struct video_format sensor_fmt;
	int ret;

	if (fmt == NULL) {
		return -EINVAL;
	}

	if ((fmt->type != VIDEO_BUF_TYPE_OUTPUT) ||
	    (fmt->pixelformat != VIDEO_SYNA_PIXEL_FORMAT_RAW8)) {
		return -ENOTSUP;
	}

	ret = video_estimate_fmt_size(fmt);
	if (ret < 0) {
		return ret;
	}

	{
		bool dt_pool_present = (cfg->shm_pool_addr != 0U) && (cfg->shm_pool_size != 0U);
		bool supported = (dt_pool_present && ((size_t)fmt->size <= cfg->shm_pool_size));

#if VIDEO_SYNA_INTERNAL_SHM_POOL_SIZE > 0
		supported = supported ||
			    ((size_t)fmt->size <= VIDEO_SYNA_INTERNAL_SHM_POOL_SIZE_BYTES);
#endif
		if (!supported) {
			return -ENOTSUP;
		}
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	if (atomic_get(&data->streaming) != 0) {
		k_mutex_unlock(&data->lock);
		return -EBUSY;
	}

	sensor_dev = data->sensor_dev;
	k_mutex_unlock(&data->lock);

	/*
	 * Validate the requested resolution against the sensor capabilities.
	 *
	 * The application-visible capture format is RAW8 (ImgProc output), but the
	 * sensor link format is RAW10P. Program the sensor with the requested width
	 * and height so unsupported sizes fail early in video_set_format().
	 */
	if ((sensor_dev == NULL) || !device_is_ready(sensor_dev)) {
		return -ENODEV;
	}

	sensor_fmt = (struct video_format) {
		.type = VIDEO_BUF_TYPE_OUTPUT,
		.pixelformat = VIDEO_SYNA_PIXEL_FORMAT_RAW10,
		.width = fmt->width,
		.height = fmt->height,
	};

	ret = video_set_format(sensor_dev, &sensor_fmt);
	if (ret != 0) {
		LOG_ERR("Sensor video_set_format(%ux%u pix=0x%x) failed: %d",
			sensor_fmt.width, sensor_fmt.height, sensor_fmt.pixelformat, ret);
		return ret;
	}

	if ((sensor_fmt.width != fmt->width) || (sensor_fmt.height != fmt->height)) {
		LOG_ERR("Sensor adjusted requested format (%ux%u -> %ux%u)",
			fmt->width, fmt->height, sensor_fmt.width, sensor_fmt.height);
		return -ENOTSUP;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	data->fmt = *fmt;
	atomic_clear(&data->capture_inflight);
	data->frame_size = 0U;
	/* HAL config is finalized at stream-start after the sensor mode is set. */

	k_mutex_unlock(&data->lock);

	return 0;
}

static int video_syna_sr100_mipi_get_format(const struct device *dev, struct video_format *fmt)
{
	struct video_syna_sr100_mipi_data *data = dev->data;

	if (fmt == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	*fmt = data->fmt;
	k_mutex_unlock(&data->lock);

	return 0;
}

static int video_syna_sr100_mipi_set_frmival(const struct device *dev, struct video_frmival *frmival)
{
	struct video_syna_sr100_mipi_data *data = dev->data;
	const struct device *sensor_dev;
	int ret;

	if ((frmival == NULL) || (frmival->numerator == 0U) ||
	    (frmival->denominator == 0U)) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	if (atomic_get(&data->streaming) != 0) {
		k_mutex_unlock(&data->lock);
		return -EBUSY;
	}

	sensor_dev = data->sensor_dev;
	k_mutex_unlock(&data->lock);

	if ((sensor_dev == NULL) || !device_is_ready(sensor_dev)) {
		return -ENODEV;
	}

	ret = video_set_frmival(sensor_dev, frmival);
	if (ret != 0) {
		return ret;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	data->frmival = *frmival;
	k_mutex_unlock(&data->lock);

	return 0;
}

static int video_syna_sr100_mipi_get_frmival(const struct device *dev, struct video_frmival *frmival)
{
	struct video_syna_sr100_mipi_data *data = dev->data;
	const struct device *sensor_dev;
	int ret;

	if (frmival == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	sensor_dev = data->sensor_dev;
	k_mutex_unlock(&data->lock);

	if ((sensor_dev != NULL) && device_is_ready(sensor_dev)) {
		ret = video_get_frmival(sensor_dev, frmival);
		if (ret == 0) {
			k_mutex_lock(&data->lock, K_FOREVER);
			data->frmival = *frmival;
			k_mutex_unlock(&data->lock);
			return 0;
		}

		if (ret != -ENOTSUP) {
			return ret;
		}
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	*frmival = data->frmival;
	k_mutex_unlock(&data->lock);

	return 0;
}

static int video_syna_sr100_mipi_enum_frmival(const struct device *dev,
						   struct video_frmival_enum *fie)
{
	struct video_syna_sr100_mipi_data *data = dev->data;
	const struct device *sensor_dev;
	struct video_format sensor_fmt;
	struct video_frmival_enum sensor_fie;
	int ret;

	if ((fie == NULL) || (fie->format == NULL) || (fie->format->width == 0U) ||
	    (fie->format->height == 0U)) {
		return -EINVAL;
	}

	if ((fie->format->type != VIDEO_BUF_TYPE_OUTPUT) ||
	    (fie->format->pixelformat != VIDEO_SYNA_PIXEL_FORMAT_RAW8)) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	sensor_dev = data->sensor_dev;
	k_mutex_unlock(&data->lock);

	if ((sensor_dev == NULL) || !device_is_ready(sensor_dev)) {
		return -ENODEV;
	}

	/*
	 * Frame interval capability is determined by the sensor link.
	 * Map the application's RAW8 capture format to the sensor's RAW10P output.
	 */
	sensor_fmt = (struct video_format) {
		.type = VIDEO_BUF_TYPE_OUTPUT,
		.pixelformat = VIDEO_SYNA_PIXEL_FORMAT_RAW10,
		.width = fie->format->width,
		.height = fie->format->height,
	};

	sensor_fie = *fie;
	sensor_fie.format = &sensor_fmt;

	ret = video_enum_frmival(sensor_dev, &sensor_fie);
	if (ret != 0) {
		return ret;
	}

	fie->type = sensor_fie.type;
	if (sensor_fie.type == VIDEO_FRMIVAL_TYPE_DISCRETE) {
		fie->discrete = sensor_fie.discrete;
	} else {
		fie->stepwise = sensor_fie.stepwise;
	}

	return 0;
}

static int video_syna_sr100_mipi_get_caps(const struct device *dev, struct video_caps *caps)
{
	const struct video_syna_sr100_mipi_config *cfg = dev->config;

	if (caps == NULL) {
		return -EINVAL;
	}

	caps->type = cfg->type;
	caps->format_caps = video_syna_sr100_mipi_format_caps;
	caps->min_vbuf_count = VIDEO_SYNA_DEFAULT_MIN_VBUFS;
	caps->buf_align = VIDEO_SYNA_DEFAULT_ALIGNMENT;

	return 0;
}

static int video_syna_sr100_mipi_flush(const struct device *dev, bool abort)
{
	struct video_syna_sr100_mipi_data *data = dev->data;
	int ret = 0;

	k_mutex_lock(&data->lock, K_FOREVER);

	if (!abort) {
		/* Non-cancel flush is used as a "kick" to start capture. */
		ret = video_syna_sr100_mipi_kick(dev);
		k_mutex_unlock(&data->lock);
		return ret;
	}

	/* flush(abort=true) stops streaming and aborts queued buffers. */
	k_mutex_unlock(&data->lock);
	video_syna_sr100_mipi_stop(dev);

	return 0;
}

#if IS_ENABLED(CONFIG_POLL)
static int video_syna_sr100_mipi_set_signal(const struct device *dev, struct k_poll_signal *signal)
{
	struct video_syna_sr100_mipi_data *data = dev->data;

	k_mutex_lock(&data->lock, K_FOREVER);
	data->signal_out = signal;
	k_mutex_unlock(&data->lock);

	return 0;
}
#endif

static int video_syna_sr100_mipi_set_stream(const struct device *dev, bool enable,
				 enum video_buf_type type)
{
	struct video_syna_sr100_mipi_data *data = dev->data;
	bool do_stop = false;
	int ret = 0;

	if (type != VIDEO_BUF_TYPE_OUTPUT) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	if (enable) {
		if (atomic_get(&data->streaming) != 0) {
			k_mutex_unlock(&data->lock);
			return 0;
		}

		/*
		 * Keep the lock held during start so format/buffer state cannot change
		 * while the pipeline is being configured.
		 */
		ret = video_syna_sr100_mipi_start(dev);
		k_mutex_unlock(&data->lock);
		return ret;
	} else {
		if (atomic_get(&data->streaming) == 0) {
			k_mutex_unlock(&data->lock);
			return 0;
		}

		do_stop = true;
	}

	k_mutex_unlock(&data->lock);

	if (do_stop) {
		video_syna_sr100_mipi_stop(dev);
		LOG_INF("Stream stopped");
	}

	return ret;
}

static void video_syna_sr100_mipi_isr(const void *arg)
{
	const struct device *dev = arg;
	struct video_syna_sr100_mipi_data *data;

	if (dev == NULL) {
		return;
	}

	data = dev->data;
	if ((data == NULL) || !data->lib_initialized || (atomic_get(&data->streaming) == 0)) {
		return;
	}

	mipi_dma_interrupt_handler();
}

static int video_syna_sr100_mipi_init_base(const struct device *dev)
{
	const struct video_syna_sr100_mipi_config *cfg = dev->config;
	struct video_syna_sr100_mipi_data *data = dev->data;
	int ret;

	if ((cfg->data_lanes == 0U) || (cfg->data_lanes > VIDEO_SYNA_MAX_DATA_LANES)) {
		LOG_ERR("Invalid num-lanes: %u (supported range: 1..%u)",
			cfg->data_lanes, VIDEO_SYNA_MAX_DATA_LANES);
		return -EINVAL;
	}

	if ((cfg->sensor_dev == NULL) || !device_is_ready(cfg->sensor_dev)) {
		LOG_ERR("Sensor device/link not configured");
		return -ENODEV;
	}

	if ((cfg->clk_dev == NULL) || !device_is_ready(cfg->clk_dev)) {
		LOG_ERR("Clock device/link not configured");
		return -ENODEV;
	}

	data->dev = dev;
	data->sensor_dev = cfg->sensor_dev;
	k_fifo_init(&data->fifo_in);
	k_fifo_init(&data->fifo_out);
	k_fifo_init(&data->fifo_complete);
	k_work_init(&data->kick_work, video_syna_sr100_mipi_kick_work);
	k_work_init(&data->complete_work, video_syna_sr100_mipi_complete_work);
	k_mutex_init(&data->lock);

	data->fmt.type = VIDEO_BUF_TYPE_OUTPUT;
	/* Application-visible OUTPUT format (MIPI writes RAW8 into SHM). */
	data->fmt.pixelformat = VIDEO_SYNA_PIXEL_FORMAT_RAW8;
	data->fmt.width = VIDEO_SYNA_DEFAULT_WIDTH;
	data->fmt.height = VIDEO_SYNA_DEFAULT_HEIGHT;
	data->frmival.numerator = 1U;
	data->frmival.denominator = 30U;
	{
		struct video_frmival sensor_frmival;

		ret = video_get_frmival(cfg->sensor_dev, &sensor_frmival);
		if (ret == 0) {
			data->frmival = sensor_frmival;
		}
	}
	atomic_clear(&data->streaming);
	atomic_clear(&data->capture_inflight);
	data->lib_initialized = false;
	data->frame_size = 0U;
	data->lane_rate_kbps = 0U;
	data->input_bit_depth = 0U;
#if IS_ENABLED(CONFIG_POLL)
	data->signal_out = NULL;
#endif

	ret = video_estimate_fmt_size(&data->fmt);
	if (ret < 0) {
		return ret;
	}

	/* HAL config is finalized at stream-start after the sensor mode is set. */

	if ((cfg->shm_pool_addr == 0U) || (cfg->shm_pool_size == 0U)) {
		LOG_WRN("Driver initialized without SHM pool DT data");
	} else {
		LOG_INF("Driver initialized: csi=%u pool=%p size=%zu",
			(uint32_t)cfg->csi_id, (void *)cfg->shm_pool_addr, cfg->shm_pool_size);
	}

	return 0;
}

static DEVICE_API(video, video_syna_sr100_mipi_ops) = {
	.set_format = video_syna_sr100_mipi_set_format,
	.get_format = video_syna_sr100_mipi_get_format,
	.set_stream = video_syna_sr100_mipi_set_stream,
	.get_caps = video_syna_sr100_mipi_get_caps,
	.enqueue = video_syna_sr100_mipi_enqueue,
	.dequeue = video_syna_sr100_mipi_dequeue,
	.flush = video_syna_sr100_mipi_flush,
	.set_frmival = video_syna_sr100_mipi_set_frmival,
	.get_frmival = video_syna_sr100_mipi_get_frmival,
	.enum_frmival = video_syna_sr100_mipi_enum_frmival,
#if IS_ENABLED(CONFIG_POLL)
	.set_signal = video_syna_sr100_mipi_set_signal,
#endif
};

#define VIDEO_SYNA_SR100_MIPI_INIT(n)                                               \
	static void video_syna_sr100_mipi_callback_##n(uint32_t status)              \
	{                                                                            \
		video_syna_sr100_mipi_callback_handle(DEVICE_DT_INST_GET(n), status); \
	}                                                                            \
	static int video_syna_sr100_mipi_init_##n(const struct device *dev)          \
	{                                                                            \
		int ret = video_syna_sr100_mipi_init_base(dev);                       \
		if (ret != 0) {                                                      \
			return ret;                                                  \
		}                                                                    \
		COND_CODE_1(DT_INST_NODE_HAS_PROP(n, interrupts),                     \
			    (IRQ_CONNECT(DT_INST_IRQN(n), DT_INST_IRQ(n, priority),  \
					 video_syna_sr100_mipi_isr,                 \
					 DEVICE_DT_INST_GET(n),                     \
					 VIDEO_SYNA_IRQ_FLAGS(n));                  \
			     irq_disable(DT_INST_IRQN(n));), ())                         \
		return 0;                                                            \
	}                                                                            \
	static const struct video_syna_sr100_mipi_config video_syna_sr100_mipi_config_##n = { \
		.shm_pool_addr = VIDEO_SYNA_SHM_POOL_ADDR(n),                       \
		.shm_pool_size = VIDEO_SYNA_SHM_POOL_SIZE(n),                       \
		.clk_dev = VIDEO_SYNA_CLK_DEV(n),                                   \
		.clk_subsys = VIDEO_SYNA_CLK_SUBSYS(n),                             \
		.csi_id = (uint8_t)(n),                                             \
		.data_lanes = VIDEO_SYNA_NUM_LANES(n),                              \
		.interface_width = VIDEO_SYNA_INTERFACE_WIDTH(n),                   \
		.sensor_dev = VIDEO_SYNA_SENSOR_DEV(n),                             \
		.type = VIDEO_BUF_TYPE_OUTPUT,                                     \
		.has_irq = DT_INST_NODE_HAS_PROP(n, interrupts),                    \
		.irqn = COND_CODE_1(DT_INST_NODE_HAS_PROP(n, interrupts),           \
				    (DT_INST_IRQN(n)), (0)),                        \
		.mipi_cb = video_syna_sr100_mipi_callback_##n,                      \
	};                                                                       \
	static struct video_syna_sr100_mipi_data video_syna_sr100_mipi_data_##n;  \
	DEVICE_DT_INST_DEFINE(n, video_syna_sr100_mipi_init_##n, NULL,            \
			      &video_syna_sr100_mipi_data_##n,                  \
			      &video_syna_sr100_mipi_config_##n,                \
			      POST_KERNEL, VIDEO_SYNA_INIT_PRIORITY,            \
			      &video_syna_sr100_mipi_ops);

DT_INST_FOREACH_STATUS_OKAY(VIDEO_SYNA_SR100_MIPI_INIT)
