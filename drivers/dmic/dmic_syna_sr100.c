/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT syna_sr100_dmic

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/audio/dmic.h>
#include <zephyr/cache.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/barrier.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define SYNA_DMIC_RX_QUEUE_LEN 16U
#define SYNA_DMIC_RX_RETRY_MS  10U

#define LP_SENSE_CLOCK_MD_G2_3_MODE2 2
#define LP_SENSE_SAMPLE_SIZE_24BIT   0

#define LPS_MEM_AUDIO_ENTRY_SIZE_IN_BYTES 8U
#define UC_AUDIO_LPS_MAX_BUF_BYTES \
	(LPS_MEM_AUDIO_ENTRY_SIZE_IN_BYTES * 0x10000U)

#define LPS_REG(data, offset)    ((data)->config->regs.lps + (offset))
#define GLOBAL_REG(data, offset) ((data)->config->regs.global + (offset))
#define GEAR1_REG(data, offset)  ((data)->config->regs.gear1 + (offset))

#define LPS_INT_EN_OFFSET        0x010U
#define LPS_STATUS_OFFSET        0x01CU
#define MIF_AMIN_OFFSET          0x184U
#define MIF_AMIN_BSTART_OFFSET   0x188U
#define MIF_AMIN_BSIZE_OFFSET    0x18CU
#define MIF_AMIN_BTHRESH_OFFSET  0x190U
#define MIF_AMIN_RPTR_OFFSET     0x194U
#define MIF_AMIN_UPDATE_OFFSET   0x1A0U
#define MIF_AMIN_WPTR_OFFSET     0x1C0U
#define LP_SENSE_AS_OFFSET       0x1C8U
#define LP_SENSE_AS_FCNT_OFFSET  0x1CCU
#define LP_SENSE_AS_STAT_OFFSET  0x1D0U

#define DM_PORT_CTRL_OFFSET      0xA0CU
#define DM_PORT_CTRL_ENABLE      0x3AU

#define CLK_ENABLE_OFFSET        0x000U
#define AUDIO_CLK_OFFSET         0x028U
#define DM_DIV_OFFSET            0x02CU
#define SOF_CTL_OFFSET           0x03CU
#define STICKY_RSTN_OFFSET       0x048U
#define PORT_MUX_OFFSET          0x04CU
#define PINMUX_CNTL_BUS_OFFSET   0x0C0U

#define DDF_CFG1_OFFSET          0x000U
#define DDF_CFG2_OFFSET          0x004U
#define DDF_FIR_DC_OFFSET        0x00CU

#define LPS_INT_AMIN_BLOCK_RDY   BIT(5)
#define LPS_STATUS_CLEAR_ALL     0xFFFFFFFFU

#define MIF_AMIN_APS_EN          BIT(0)
#define MIF_AMIN_CHANNEL_ON(ch)  BIT((ch) + 1U)
#define MIF_AMIN_BITS_PER_SAMPLE_24 BIT(5)
#define MIF_AMIN_PTR_MASK        GENMASK(16, 0)

#define MIF_AMIN_UPDATE_ACTIVE_CHANNELS BIT(0)

#define MIF_AMIN_BTHRESH_MASK        GENMASK(15, 0)
#define MIF_AMIN_BTHRESH_BLOCK_SHIFT 16U

#define LP_SENSE_AS_DM_IF0_EN BIT(0)
#define LP_SENSE_AS_DM_IF1_EN BIT(1)
#define LP_SENSE_AS_TVALID(ch) BIT((ch) + 2U)

#define LP_SENSE_AS_DDF_INPUT_MUX_MASK       GENMASK(1, 0)
#define LP_SENSE_AS_DDF_INPUT_MUX_BASE_SHIFT 6U
#define LP_SENSE_AS_DDF_INPUT_MUX_STRIDE     2U

#define LP_SENSE_AS_MIN_DM_SAMP_GAP_SHIFT    14U
#define LP_SENSE_AS_MIN_DM_SAMP_GAP_VALUE    3U

#define LP_SENSE_AS_STATUS_FIFO0_EMPTY BIT(0)
#define LP_SENSE_AS_STATUS_FIFO0_FULL  BIT(1)
#define LP_SENSE_AS_STATUS_FIFO1_EMPTY BIT(2)
#define LP_SENSE_AS_STATUS_FIFO1_FULL  BIT(3)

#define AUDIO_CLK_SRC_MASK       GENMASK(2, 0)
#define AUDIO_CLK_SRC_BASE       0U
#define AUDIO_CLK_SRC_MODE2      1U
#define AUDIO_CLK_ENABLE         BIT(3)
#define AUDIO_CLK_DM0_CLK_ENABLE BIT(4)
#define AUDIO_CLK_DM1_CLK_ENABLE BIT(5)
#define AUDIO_CLK_DM_CLK_EN      BIT(6)
#define AUDIO_CLK_DM_CLK_DIV_EN  BIT(8)

#define DM_DIV_FRAC_SHIFT        8U
#define DM_DIV_INT_MASK          GENMASK(7, 0)
#define DM_DIV_FRAC_MASK         GENMASK(31, 8)

#define SOF_CTL_DM_DIV_CFG_UPD   BIT(1)

#define PINMUX_I2C0_MS_SDA_SHIFT 0U
#define PINMUX_I2C0_MS_SCL_SHIFT 3U
#define PINMUX_3BIT_MASK         0x7U
#define PINMUX_I2C0_MS_DMIC_VAL  0x3U
#define PORT_MUX_LOW_BYTE_MASK   0xFFU
#define PORT_MUX_DM_PADS         0xFAU

#define CLK_ENABLE_ALL           0x007FFFFFU
#define STICKY_RSTN_ALL          0x0001FFFFU
#define STICKY_RSTN_RUN_BASE     0x0001C0FFU
#define STICKY_RSTN_AD_SENSE     BIT(3)
#define STICKY_RSTN_DDF0         BIT(7)
#define STICKY_RSTN_DDF1         BIT(8)
#define STICKY_RSTN_DDF2         BIT(9)
#define STICKY_RSTN_DDF3         BIT(10)

#define CLK_ENABLE_LP_BASE       BIT(0)
#define CLK_ENABLE_LPS_MEM       BIT(2)
#define CLK_ENABLE_AUDIO         BIT(3)
#define CLK_ENABLE_LPS_CORE      BIT(4)
#define CLK_ENABLE_LPS_REG       BIT(7)
#define CLK_ENABLE_LPS_AUDSENSE  BIT(9)
#define CLK_ENABLE_LPS_DDF0      BIT(13)
#define CLK_ENABLE_LPS_DDF1      BIT(14)
#define CLK_ENABLE_LPS_DDF2      BIT(15)
#define CLK_ENABLE_LPS_DDF3      BIT(16)
#define CLK_ENABLE_LPS_IPI       BIT(18)
#define CLK_ENABLE_CFG           BIT(19)
#define CLK_ENABLE_LPS_RES       BIT(22)

#define DDF_CFG1_16BIT_SAMPLE_EN BIT(5)
#define DDF_CFG2_CHANNEL_EN      BIT(31)

#define DDF_INPUT_MUX(ch) \
	((ch) & LP_SENSE_AS_DDF_INPUT_MUX_MASK)


#define SYNA_DMIC_AMIN_BLOCK_ENTRIES 0x400U


struct dmic_syna_sr100_regs {
	uint32_t global;
	uint32_t lps;
	uint32_t gear1;
	uint32_t lp_mem;
	uint32_t ddf0;
	uint32_t ddf_stride;
};

struct syna_dmic_ddf_reg_write {
	uint32_t off;
	uint32_t val;
};

struct st_lp_sense_df_audio {
	uint32_t bit_depth;
	uint32_t sample_rate;
	uint32_t audio_buf_offset_in_bytes;
	uint32_t audio_buf_size_in_bytes;
	uint32_t audio_buf_block_size_in_bytes;
};

struct dmic_syna_sr100_amin_state {
	uint32_t wptr_reg;
	uint32_t wptr_entries;
	uint32_t bstart_entries;
	uint32_t bsize_entries;
};

struct dmic_syna_sr100_rx_buf {
	void *buffer;
	size_t size;
	uint32_t next_rptr_entries;
};

struct dmic_syna_sr100_dma_ctx {
	const struct device *dev;
	uint32_t channel;
	struct dmic_syna_sr100_data *data;
};

struct dmic_syna_sr100_config {
	const struct device *dma_dev;
	uint32_t dma_chan;
	const struct device *clock_dev;
	clock_control_subsys_t clock_subsys;
	struct reset_dt_spec reset;
	const struct pinctrl_dev_config *pcfg;
	void (*irq_config)(void);
	struct dmic_syna_sr100_regs regs;
	const struct syna_dmic_ddf_reg_write *ddf_profile;
	size_t ddf_profile_len;
};

struct dmic_syna_sr100_data {
	enum dmic_state state;
	const struct dmic_syna_sr100_config *config;
	struct k_mutex lock;
	struct k_spinlock spinlock;
	struct dmic_cfg cfg;
	struct pcm_stream_cfg stream_cfg;
	struct st_lp_sense_df_audio df_audio;
	struct dmic_syna_sr100_dma_ctx dma;
	struct dma_config dma_cfg;
	struct dma_block_config dma_blocks[2];
	uint32_t amin_sw_rptr_entries;
	struct k_msgq rx_pending_q;
	struct k_msgq rx_out_q;
	struct k_work amin_block_ready_event;
	struct k_work_delayable amin_rx_retry_work;
	char rx_pending_buf[SYNA_DMIC_RX_QUEUE_LEN *
			    sizeof(struct dmic_syna_sr100_rx_buf)];
	char rx_out_buf[SYNA_DMIC_RX_QUEUE_LEN *
			sizeof(struct dmic_syna_sr100_rx_buf)];
	bool dma_active;
	bool stop_pending;
	atomic_t rx_drops;
	atomic_t rx_overruns;
	atomic_t dma_errors;
};

static void dmic_syna_sr100_dma_cb(const struct device *dev, void *user_data,
				   uint32_t channel, int status);
static void dmic_syna_sr100_get_amin_state(uint32_t lps_base,
					   struct dmic_syna_sr100_amin_state *state);
static void dmic_syna_sr100_set_amin_rptr(uint32_t lps_base, uint32_t rptr_entries);
static int dmic_syna_sr100_df_start_hw(const struct dmic_syna_sr100_data *data,
				       const struct st_lp_sense_df_audio *cfg);
static int dmic_syna_sr100_df_stop_hw(const struct dmic_syna_sr100_data *data);
static int dmic_syna_sr100_check_capture_started(const struct dmic_syna_sr100_data *data);
static void dmic_syna_sr100_ddf_channel_config(const struct dmic_syna_sr100_data *data,
						       uint32_t ddf_base);
static void dmic_syna_sr100_mask_amin_irq(struct dmic_syna_sr100_data *data);
static void dmic_syna_sr100_unmask_amin_irq(struct dmic_syna_sr100_data *data);

static uint32_t dmic_syna_sr100_amin_available_entries(
	const struct dmic_syna_sr100_data *data,
	const struct dmic_syna_sr100_amin_state *amin)
{
	if (amin->wptr_entries >= data->amin_sw_rptr_entries)
		return amin->wptr_entries - data->amin_sw_rptr_entries;


	return amin->bsize_entries - data->amin_sw_rptr_entries +
	       amin->wptr_entries;
}

static int dmic_syna_sr100_get_amin_dma_segments(
	struct dmic_syna_sr100_data *data,
	uintptr_t *src0, size_t *len0,
	uintptr_t *src1, size_t *len1,
	size_t *total_len,
	uint32_t *next_rptr_entries)
{
	struct dmic_syna_sr100_amin_state amin;
	uint32_t available_entries;
	uint32_t target_entries;
	uint32_t ring_off_entries;
	uint32_t first_entries;

	target_entries = data->cfg.streams[0].block_size /
			 LPS_MEM_AUDIO_ENTRY_SIZE_IN_BYTES;

	dmic_syna_sr100_get_amin_state(data->config->regs.lps, &amin);
	available_entries = dmic_syna_sr100_amin_available_entries(data, &amin);

	if (available_entries < target_entries)
		return -EAGAIN;


	if (available_entries >= amin.bsize_entries - amin.bsize_entries / 16U)
		return -EOVERFLOW;


	ring_off_entries = data->amin_sw_rptr_entries % amin.bsize_entries;
	first_entries = MIN(target_entries, amin.bsize_entries - ring_off_entries);

	*src0 = data->config->regs.lp_mem +
		amin.bstart_entries * LPS_MEM_AUDIO_ENTRY_SIZE_IN_BYTES +
		ring_off_entries * LPS_MEM_AUDIO_ENTRY_SIZE_IN_BYTES;
	*len0 = first_entries * LPS_MEM_AUDIO_ENTRY_SIZE_IN_BYTES;

	if (first_entries < target_entries) {
		*src1 = data->config->regs.lp_mem +
			amin.bstart_entries * LPS_MEM_AUDIO_ENTRY_SIZE_IN_BYTES;
		*len1 = (target_entries - first_entries) *
			LPS_MEM_AUDIO_ENTRY_SIZE_IN_BYTES;
	} else {
		*src1 = 0U;
		*len1 = 0U;
	}

	*total_len = data->cfg.streams[0].block_size;
	*next_rptr_entries =
		(data->amin_sw_rptr_entries + target_entries) % amin.bsize_entries;

	return 0;
}

static int dmic_syna_sr100_dma_submit_segments(struct dmic_syna_sr100_data *data,
					       void *dst,
					       uintptr_t src0,
					       size_t len0,
					       uintptr_t src1,
					       size_t len1)
{
	struct dma_config *cfg = &data->dma_cfg;
	struct dma_block_config *blk0 = &data->dma_blocks[0];
	struct dma_block_config *blk1 = &data->dma_blocks[1];
	uintptr_t dst_addr = (uintptr_t)dst;
	k_spinlock_key_t key;
	int ret;

	if ((len0 == 0U) || (data->dma.dev == NULL))
		return -EINVAL;


	if (!IS_ALIGNED(src0, sizeof(uint32_t)) ||
	    !IS_ALIGNED(dst_addr, sizeof(uint32_t)) ||
	    !IS_ALIGNED(len0, sizeof(uint32_t))) {
		return -EINVAL;
	}

	if ((len1 != 0U) &&
	    (!IS_ALIGNED(src1, sizeof(uint32_t)) ||
	     !IS_ALIGNED(dst_addr + len0, sizeof(uint32_t)) ||
	     !IS_ALIGNED(len1, sizeof(uint32_t)))) {
		return -EINVAL;
	}

	memset(cfg, 0, sizeof(*cfg));
	memset(blk0, 0, sizeof(*blk0));
	memset(blk1, 0, sizeof(*blk1));

	blk0->source_address = (uint32_t)src0;
	blk0->dest_address = (uint32_t)dst_addr;
	blk0->block_size = len0;

	if (len1 != 0U) {
		blk0->next_block = blk1;

		blk1->source_address = (uint32_t)src1;
		blk1->dest_address = (uint32_t)(dst_addr + len0);
		blk1->block_size = len1;

		cfg->block_count = 2U;
	} else {
		cfg->block_count = 1U;
	}

	cfg->channel_direction = MEMORY_TO_MEMORY;
	cfg->source_data_size = sizeof(uint32_t);
	cfg->dest_data_size = sizeof(uint32_t);
	cfg->source_burst_length = 1U;
	cfg->dest_burst_length = 1U;
	cfg->head_block = blk0;
	cfg->user_data = &data->dma;
	cfg->dma_callback = dmic_syna_sr100_dma_cb;

#if IS_ENABLED(CONFIG_CACHE_MANAGEMENT) && IS_ENABLED(CONFIG_DCACHE)
	sys_cache_data_invd_range((void *)src0, len0);
	if (len1 != 0U)
		sys_cache_data_invd_range((void *)src1, len1);

	sys_cache_data_invd_range(dst, len0 + len1);
#endif

	ret = dma_config(data->dma.dev, data->dma.channel, cfg);
	if (ret < 0)
		return ret;


	key = k_spin_lock(&data->spinlock);
	data->dma_active = true;
	k_spin_unlock(&data->spinlock, key);

	ret = dma_start(data->dma.dev, data->dma.channel);
	if (ret < 0) {
		key = k_spin_lock(&data->spinlock);
		data->dma_active = false;
		k_spin_unlock(&data->spinlock, key);
	}


	return ret;
}

static int dmic_syna_sr100_rx_submit_next(struct dmic_syna_sr100_data *data)
{
	struct dmic_syna_sr100_rx_buf rx;
	uintptr_t src0;
	uintptr_t src1;
	size_t len0;
	size_t len1;
	size_t total_len;
	uint32_t next_rptr_entries;
	k_spinlock_key_t key;
	int ret;

	key = k_spin_lock(&data->spinlock);
	if ((data->state != DMIC_STATE_ACTIVE) ||
	    data->dma_active ||
	    data->stop_pending) {
		k_spin_unlock(&data->spinlock, key);
		return 0;
	}
	k_spin_unlock(&data->spinlock, key);

	ret = dmic_syna_sr100_get_amin_dma_segments(data, &src0, &len0,
						    &src1, &len1,
						    &total_len,
						    &next_rptr_entries);
	if (ret == -EAGAIN)
		return 0;

	if (ret == -EOVERFLOW) {
		atomic_inc(&data->rx_overruns);
		return ret;
	}

	if (ret < 0)
		return ret;

	ret = k_mem_slab_alloc(data->cfg.streams[0].mem_slab,
			       &rx.buffer, K_NO_WAIT);
	if (ret < 0) {
		atomic_inc(&data->rx_drops);
		return 0;
	}

	rx.size = total_len;
	rx.next_rptr_entries = next_rptr_entries;

	ret = k_msgq_put(&data->rx_pending_q, &rx, K_NO_WAIT);
	if (ret < 0) {
		k_mem_slab_free(data->cfg.streams[0].mem_slab, rx.buffer);
		atomic_inc(&data->rx_drops);
		return 0;
	}

	ret = dmic_syna_sr100_dma_submit_segments(data, rx.buffer,
						  src0, len0,
						  src1, len1);
	if (ret < 0) {
		(void)k_msgq_get(&data->rx_pending_q, &rx, K_NO_WAIT);
		k_mem_slab_free(data->cfg.streams[0].mem_slab, rx.buffer);
		atomic_inc(&data->dma_errors);
		return ret;
	}

	return 0;
}

static void dmic_syna_sr100_handle_amin_block_ready(struct k_work *item)
{
	struct dmic_syna_sr100_data *data =
		CONTAINER_OF(item, struct dmic_syna_sr100_data, amin_block_ready_event);

	(void)dmic_syna_sr100_rx_submit_next(data);
}

static void dmic_syna_sr100_handle_rx_retry(struct k_work *item)
{
	struct k_work_delayable *work = k_work_delayable_from_work(item);
	struct dmic_syna_sr100_data *data =
		CONTAINER_OF(work, struct dmic_syna_sr100_data, amin_rx_retry_work);
	k_spinlock_key_t key;
	bool keep_running;

	(void)dmic_syna_sr100_rx_submit_next(data);

	key = k_spin_lock(&data->spinlock);
	keep_running = (data->state == DMIC_STATE_ACTIVE) &&
		       !data->stop_pending;
	k_spin_unlock(&data->spinlock, key);

	if (keep_running) {
		(void)k_work_schedule(&data->amin_rx_retry_work,
				      K_MSEC(SYNA_DMIC_RX_RETRY_MS));
	}
}

static void dmic_syna_sr100_amin_isr(const void *arg)
{
	const struct device *dev = arg;
	struct dmic_syna_sr100_data *data = dev->data;
	uint32_t status = sys_read32(LPS_REG(data, LPS_STATUS_OFFSET));

	if ((status & LPS_INT_AMIN_BLOCK_RDY) == 0U)
		return;


	sys_write32(LPS_INT_AMIN_BLOCK_RDY, LPS_REG(data, LPS_STATUS_OFFSET));
	(void)k_work_submit(&data->amin_block_ready_event);
}

static void dmic_syna_sr100_dma_cb(const struct device *dev,
				   void *user_data,
				   uint32_t channel,
				   int status)
{
	struct dmic_syna_sr100_dma_ctx *dma = user_data;
	struct dmic_syna_sr100_data *data = dma->data;
	struct dmic_syna_sr100_rx_buf rx;
	k_spinlock_key_t key;
	bool keep_running;
	int ret;

	ARG_UNUSED(dev);
	ARG_UNUSED(channel);

	key = k_spin_lock(&data->spinlock);
	data->dma_active = false;
	k_spin_unlock(&data->spinlock, key);

	ret = k_msgq_get(&data->rx_pending_q, &rx, K_NO_WAIT);
	if (ret < 0) {
		atomic_inc(&data->dma_errors);
		return;
	}

	if (status < 0) {
		k_mem_slab_free(data->cfg.streams[0].mem_slab,
				rx.buffer);

		atomic_inc(&data->dma_errors);
		return;
	}

	key = k_spin_lock(&data->spinlock);
	data->amin_sw_rptr_entries = rx.next_rptr_entries;

	dmic_syna_sr100_set_amin_rptr(
		data->config->regs.lps,
		data->amin_sw_rptr_entries);
	k_spin_unlock(&data->spinlock, key);

#if IS_ENABLED(CONFIG_CACHE_MANAGEMENT) && IS_ENABLED(CONFIG_DCACHE)
	sys_cache_data_invd_range(rx.buffer, rx.size);
#endif

	ret = k_msgq_put(&data->rx_out_q, &rx, K_NO_WAIT);
	if (ret < 0) {
		k_mem_slab_free(data->cfg.streams[0].mem_slab,
				rx.buffer);

		atomic_inc(&data->rx_drops);
		return;
	}

	key = k_spin_lock(&data->spinlock);
	keep_running = (data->state == DMIC_STATE_ACTIVE) &&
		       !data->stop_pending;
	k_spin_unlock(&data->spinlock, key);

	if (keep_running)
		(void)dmic_syna_sr100_rx_submit_next(data);
}

static void dmic_syna_sr100_rx_cleanup(struct dmic_syna_sr100_data *data)
{
	struct dmic_syna_sr100_rx_buf rx;

	(void)k_work_cancel(&data->amin_block_ready_event);
	(void)k_work_cancel_delayable(&data->amin_rx_retry_work);

	while (k_msgq_get(&data->rx_pending_q, &rx, K_NO_WAIT) == 0) {
		if (rx.buffer != NULL) {
			k_mem_slab_free(data->cfg.streams[0].mem_slab,
					rx.buffer);
		}
	}

	while (k_msgq_get(&data->rx_out_q, &rx, K_NO_WAIT) == 0) {
		if (rx.buffer != NULL) {
			k_mem_slab_free(data->cfg.streams[0].mem_slab,
					rx.buffer);
		}
	}

	data->dma_active = false;
	data->stop_pending = false;
}

static void dmic_syna_sr100_runtime_reset(struct dmic_syna_sr100_data *data)
{
	(void)dma_stop(data->dma.dev, data->dma.channel);
	(void)dmic_syna_sr100_df_stop_hw(data);

	dmic_syna_sr100_rx_cleanup(data);

	memset(&data->cfg, 0, sizeof(data->cfg));
	memset(&data->stream_cfg, 0, sizeof(data->stream_cfg));
	memset(&data->df_audio, 0, sizeof(data->df_audio));

	data->amin_sw_rptr_entries = 0U;
	data->state = DMIC_STATE_INITIALIZED;
}

static void dmic_syna_sr100_reset_amin_rptr_to_block_boundary(
	struct dmic_syna_sr100_data *data)
{
	struct dmic_syna_sr100_amin_state amin;
	uint32_t block_entries = SYNA_DMIC_AMIN_BLOCK_ENTRIES;

	dmic_syna_sr100_get_amin_state(
		data->config->regs.lps,
		&amin);

	if ((block_entries == 0U) ||
	    (block_entries > amin.bsize_entries)) {
		data->amin_sw_rptr_entries =
			amin.wptr_entries;
	} else {
		data->amin_sw_rptr_entries =
			ROUND_DOWN(
				amin.wptr_entries,
				block_entries);
	}

	dmic_syna_sr100_set_amin_rptr(
		data->config->regs.lps,
		data->amin_sw_rptr_entries);
}

static void dmic_syna_sr100_get_amin_state(
	uint32_t lps_base,
	struct dmic_syna_sr100_amin_state *state)
{
	uint32_t bstart_reg;
	uint32_t bsize_reg;

	if (state == NULL)
		return;


	state->wptr_reg =
		sys_read32(lps_base + MIF_AMIN_WPTR_OFFSET);

	bstart_reg =
		sys_read32(lps_base + MIF_AMIN_BSTART_OFFSET);

	bsize_reg =
		sys_read32(lps_base + MIF_AMIN_BSIZE_OFFSET);

	state->bsize_entries =
		bsize_reg & MIF_AMIN_PTR_MASK;

	if (state->bsize_entries == 0U) {
		state->bsize_entries =
			UC_AUDIO_LPS_MAX_BUF_BYTES /
			LPS_MEM_AUDIO_ENTRY_SIZE_IN_BYTES;
	}

	if (state->bsize_entries == 0U) {
		state->wptr_entries = 0U;
		state->bstart_entries = 0U;
		return;
	}

	state->wptr_entries =
		(state->wptr_reg & MIF_AMIN_PTR_MASK) %
		state->bsize_entries;

	state->bstart_entries =
		(bstart_reg & MIF_AMIN_PTR_MASK) %
		state->bsize_entries;
}

static void dmic_syna_sr100_set_amin_rptr(uint32_t lps_base,
					  uint32_t rptr_entries)
{
	sys_write32(rptr_entries & MIF_AMIN_PTR_MASK,
		    lps_base + MIF_AMIN_RPTR_OFFSET);
}

static int dmic_syna_sr100_start_rx(struct dmic_syna_sr100_data *data)
{
	k_spinlock_key_t key;
	int ret;

	key = k_spin_lock(&data->spinlock);
	data->stop_pending = false;
	k_spin_unlock(&data->spinlock, key);

	ret = dmic_syna_sr100_df_start_hw(data, &data->df_audio);
	if (ret < 0) {
		data->state = DMIC_STATE_ERROR;
		return ret;
	}

	ret = dmic_syna_sr100_check_capture_started(data);
	if (ret < 0) {
		(void)dmic_syna_sr100_df_stop_hw(data);
		data->state = DMIC_STATE_ERROR;
		return ret;
	}

	dmic_syna_sr100_reset_amin_rptr_to_block_boundary(data);

	data->state = DMIC_STATE_ACTIVE;
	dmic_syna_sr100_unmask_amin_irq(data);

	ret = dmic_syna_sr100_rx_submit_next(data);
	if (ret < 0) {
		data->state = DMIC_STATE_ERROR;
		return ret;
	}

	(void)k_work_schedule(&data->amin_rx_retry_work,
			      K_MSEC(SYNA_DMIC_RX_RETRY_MS));

	return 0;
}

static void dmic_syna_sr100_stop_dma(struct dmic_syna_sr100_data *data)
{
	k_spinlock_key_t key;
	bool dma_active;

	key = k_spin_lock(&data->spinlock);
	dma_active = data->dma_active;
	k_spin_unlock(&data->spinlock, key);

	if (dma_active) {
		(void)dma_stop(data->dma.dev, data->dma.channel);
		key = k_spin_lock(&data->spinlock);
		data->dma_active = false;
		k_spin_unlock(&data->spinlock, key);
	}
}

static int dmic_syna_sr100_stop_rx(struct dmic_syna_sr100_data *data, bool cleanup)
{
	k_spinlock_key_t key;
	int ret;

	key = k_spin_lock(&data->spinlock);
	data->stop_pending = true;
	k_spin_unlock(&data->spinlock, key);

	dmic_syna_sr100_mask_amin_irq(data);
	(void)k_work_cancel(&data->amin_block_ready_event);
	(void)k_work_cancel_delayable(&data->amin_rx_retry_work);
	dmic_syna_sr100_stop_dma(data);

	ret = dmic_syna_sr100_df_stop_hw(data);
	if (ret < 0) {
		data->state = DMIC_STATE_ERROR;
		return ret;
	}

	if (cleanup)
		dmic_syna_sr100_rx_cleanup(data);


	data->state = DMIC_STATE_CONFIGURED;
	return 0;
}

static int dmic_syna_sr100_trigger(const struct device *dev, enum dmic_trigger cmd)
{
	struct dmic_syna_sr100_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);

	switch (cmd) {
	case DMIC_TRIGGER_START:
		if ((data->state != DMIC_STATE_CONFIGURED) &&
		    (data->state != DMIC_STATE_PAUSED)) {
			ret = -EACCES;
			break;
		}

		ret = dmic_syna_sr100_start_rx(data);
		break;

	case DMIC_TRIGGER_STOP:
		if ((data->state != DMIC_STATE_ACTIVE) &&
		    (data->state != DMIC_STATE_PAUSED)) {
			ret = -EACCES;
			break;
		}

		ret = dmic_syna_sr100_stop_rx(data, true);
		break;

	case DMIC_TRIGGER_PAUSE:
		if (data->state != DMIC_STATE_ACTIVE) {
			ret = -EACCES;
			break;
		}

		ret = dmic_syna_sr100_stop_rx(data, false);
		if (ret == 0)
			data->state = DMIC_STATE_PAUSED;

		break;

	case DMIC_TRIGGER_RELEASE:
		if (data->state != DMIC_STATE_PAUSED) {
			ret = -EACCES;
			break;
		}

		ret = dmic_syna_sr100_start_rx(data);
		break;

	case DMIC_TRIGGER_RESET:
		dmic_syna_sr100_runtime_reset(data);
		ret = 0;
		break;

	default:
		ret = -EINVAL;
		break;
	}

	k_mutex_unlock(&data->lock);

	return ret;
}

static int dmic_syna_sr100_build_df_audio_cfg(
	struct dmic_syna_sr100_data *data,
	struct st_lp_sense_df_audio *df_audio)
{
	uint32_t block_size_bytes;

	if (df_audio == NULL)
		return -EINVAL;


	memset(df_audio, 0, sizeof(*df_audio));

	df_audio->bit_depth = LP_SENSE_SAMPLE_SIZE_24BIT;
	df_audio->sample_rate = data->cfg.streams[0].pcm_rate;
	df_audio->audio_buf_offset_in_bytes = 0U;

	block_size_bytes =
		LPS_MEM_AUDIO_ENTRY_SIZE_IN_BYTES *
		SYNA_DMIC_AMIN_BLOCK_ENTRIES;

	df_audio->audio_buf_block_size_in_bytes = block_size_bytes;
	df_audio->audio_buf_size_in_bytes = UC_AUDIO_LPS_MAX_BUF_BYTES;

	return 0;
}

static void dmic_syna_sr100_lps_apply_reset_defaults(
	const struct dmic_syna_sr100_data *data)
{
	sys_write32(0U, LPS_REG(data, LPS_INT_EN_OFFSET));
	sys_write32(LPS_STATUS_CLEAR_ALL, LPS_REG(data, LPS_STATUS_OFFSET));
	sys_write32(0U, LPS_REG(data, LP_SENSE_AS_OFFSET));
	sys_write32(0U, LPS_REG(data, LP_SENSE_AS_FCNT_OFFSET));
	sys_write32(0U, LPS_REG(data, MIF_AMIN_OFFSET));
	sys_write32(0U, LPS_REG(data, MIF_AMIN_RPTR_OFFSET));
	sys_write32(0U, LPS_REG(data, MIF_AMIN_WPTR_OFFSET));
}

static void dmic_syna_sr100_lps_clear_status(
	const struct dmic_syna_sr100_data *data)
{
	sys_write32(LPS_STATUS_CLEAR_ALL, LPS_REG(data, LPS_STATUS_OFFSET));
	barrier_dsync_fence_full();
	(void)sys_read32(LPS_REG(data, LPS_STATUS_OFFSET));
}

static void dmic_syna_sr100_lp_sense_reset_mask(
	const struct dmic_syna_sr100_data *data,
	uint32_t sw_reset_mask)
{
	uint32_t sticky;

	if (sw_reset_mask == 0U)
		return;


	sticky = sys_read32(GEAR1_REG(data, STICKY_RSTN_OFFSET));

	sys_write32(sticky & ~sw_reset_mask,
		    GEAR1_REG(data, STICKY_RSTN_OFFSET));
	(void)sys_read32(GEAR1_REG(data, STICKY_RSTN_OFFSET));

	sys_write32((sticky & ~sw_reset_mask) | sw_reset_mask,
		    GEAR1_REG(data, STICKY_RSTN_OFFSET));
	(void)sys_read32(GEAR1_REG(data, STICKY_RSTN_OFFSET));
}

static void dmic_syna_sr100_lp_sense_reset_blocks(
	const struct dmic_syna_sr100_data *data,
	uint32_t sw_reset_mask)
{
	dmic_syna_sr100_lp_sense_reset_mask(data, sw_reset_mask);
}

static int dmic_syna_sr100_df_init_clock(
	const struct dmic_syna_sr100_data *data,
	int mode)
{
	uint32_t g1_clk_cur;
	uint32_t g1_clk_en;
	uint32_t audio_clk;
	uint32_t sticky;

	g1_clk_cur = sys_read32(GEAR1_REG(data, CLK_ENABLE_OFFSET));
	if ((g1_clk_cur & CLK_ENABLE_ALL) != CLK_ENABLE_ALL) {
		g1_clk_en = g1_clk_cur | CLK_ENABLE_ALL;

		k_busy_wait(2);

		sys_write32(g1_clk_en, GEAR1_REG(data, CLK_ENABLE_OFFSET));
		(void)sys_read32(GEAR1_REG(data, CLK_ENABLE_OFFSET));
	}

	sticky = sys_read32(GEAR1_REG(data, STICKY_RSTN_OFFSET));

	sys_write32(STICKY_RSTN_ALL,
		    GEAR1_REG(data, STICKY_RSTN_OFFSET));
	(void)sys_read32(GEAR1_REG(data, STICKY_RSTN_OFFSET));

	sys_write32(sticky,
		    GEAR1_REG(data, STICKY_RSTN_OFFSET));
	(void)sys_read32(GEAR1_REG(data, STICKY_RSTN_OFFSET));

	audio_clk = sys_read32(GEAR1_REG(data, AUDIO_CLK_OFFSET));
	audio_clk &= ~AUDIO_CLK_SRC_MASK;
	audio_clk |= AUDIO_CLK_SRC_MODE2;

	sys_write32(audio_clk, GEAR1_REG(data, AUDIO_CLK_OFFSET));
	(void)sys_read32(GEAR1_REG(data, AUDIO_CLK_OFFSET));

	return 0;
}

static uint32_t dmic_syna_sr100_df_audio_channel_mask(uint8_t num_mics)
{
	uint32_t mask = 0U;

	for (uint8_t i = 0U; i < num_mics; i++)
		mask |= MIF_AMIN_CHANNEL_ON(i);


	return mask;
}

static uint32_t dmic_syna_sr100_dm_div_for_sample_rate(
	uint32_t sample_rate,
	uint32_t lp_bit_depth)
{
	const uint64_t divider_48k_q24 = (32ULL << 24) | 0x827F2AULL;
	uint64_t divider_q24;
	uint32_t divider_int;
	uint32_t divider_frac;

	ARG_UNUSED(lp_bit_depth);

	divider_q24 =
		((divider_48k_q24 * 48000ULL) +
		 (sample_rate / 2U)) /
		sample_rate;

	divider_int = (uint32_t)(divider_q24 >> 24);
	divider_frac = (uint32_t)(divider_q24 & 0xFFFFFFULL);

	return (divider_frac << DM_DIV_FRAC_SHIFT) | divider_int;
}

static void dmic_syna_sr100_program_dm_div(
	const struct dmic_syna_sr100_data *data,
	uint32_t sample_rate,
	uint32_t lp_bit_depth)
{
	uint32_t g1_port_mux;
	uint32_t g1_pinmux_bus;
	uint32_t dm_div;

	dm_div = dmic_syna_sr100_dm_div_for_sample_rate(
			sample_rate,
			lp_bit_depth);

	sys_write32(dm_div, GEAR1_REG(data, DM_DIV_OFFSET));

	sys_write32(SOF_CTL_DM_DIV_CFG_UPD,
		    GEAR1_REG(data, SOF_CTL_OFFSET));
	(void)sys_read32(GEAR1_REG(data, SOF_CTL_OFFSET));

	g1_port_mux = sys_read32(GEAR1_REG(data, PORT_MUX_OFFSET));
	g1_port_mux &= ~PORT_MUX_LOW_BYTE_MASK;
	g1_port_mux |= PORT_MUX_DM_PADS;
	sys_write32(g1_port_mux, GEAR1_REG(data, PORT_MUX_OFFSET));

	g1_pinmux_bus =
		sys_read32(GEAR1_REG(data, PINMUX_CNTL_BUS_OFFSET));

	g1_pinmux_bus &=
		~((PINMUX_3BIT_MASK << PINMUX_I2C0_MS_SDA_SHIFT) |
		  (PINMUX_3BIT_MASK << PINMUX_I2C0_MS_SCL_SHIFT));

	g1_pinmux_bus |=
		(PINMUX_I2C0_MS_DMIC_VAL << PINMUX_I2C0_MS_SDA_SHIFT) |
		(PINMUX_I2C0_MS_DMIC_VAL << PINMUX_I2C0_MS_SCL_SHIFT);

	sys_write32(g1_pinmux_bus,
		    GEAR1_REG(data, PINMUX_CNTL_BUS_OFFSET));

	sys_write32(DM_PORT_CTRL_ENABLE,
		    GLOBAL_REG(data, DM_PORT_CTRL_OFFSET));
	(void)sys_read32(GLOBAL_REG(data, DM_PORT_CTRL_OFFSET));
}

static void dmic_syna_sr100_audio_ddf_config(
	const struct dmic_syna_sr100_data *data,
	uint8_t num_mics)
{
	for (uint8_t ch = 0U; ch < num_mics; ch++) {
		dmic_syna_sr100_ddf_channel_config(
			data,
			data->config->regs.ddf0 +
			(data->config->regs.ddf_stride * ch));
	}
}

static int dmic_syna_sr100_lp_sense_init_registers(
	const struct dmic_syna_sr100_data *data)
{
	uint32_t sticky;

	dmic_syna_sr100_lps_apply_reset_defaults(data);

	sys_write32(CLK_ENABLE_ALL,
		    GEAR1_REG(data, CLK_ENABLE_OFFSET));
	(void)sys_read32(GEAR1_REG(data, CLK_ENABLE_OFFSET));

	sticky = sys_read32(
			GEAR1_REG(data, STICKY_RSTN_OFFSET));

	sys_write32(STICKY_RSTN_ALL,
		    GEAR1_REG(data, STICKY_RSTN_OFFSET));
	(void)sys_read32(
			GEAR1_REG(data, STICKY_RSTN_OFFSET));

	sys_write32(sticky,
		    GEAR1_REG(data, STICKY_RSTN_OFFSET));
	(void)sys_read32(
			GEAR1_REG(data, STICKY_RSTN_OFFSET));

	return 0;
}

static int dmic_syna_sr100_df_init_hw(
	const struct dmic_syna_sr100_data *data,
	int mode)
{
	if (dmic_syna_sr100_df_init_clock(data,
					  mode) != 0) {
		return -EIO;
	}

	dmic_syna_sr100_lps_clear_status(data);

	return 0;
}

static int dmic_syna_sr100_apply_pinctrl(
	struct dmic_syna_sr100_data *data,
	const char *stage)
{
	int ret;

	if ((data == NULL) ||
	    (data->config == NULL) ||
	    (data->config->pcfg == NULL)) {
		return -EINVAL;
	}
	ARG_UNUSED(stage);

	ret = pinctrl_apply_state(
			data->config->pcfg,
			PINCTRL_STATE_DEFAULT);

	if (ret < 0)
		return ret;


	return 0;
}

static int dmic_syna_sr100_df_audio_config_hw(
	const struct dmic_syna_sr100_data *data,
	const struct st_lp_sense_df_audio *cfg)
{
	uint32_t block_bytes;
	uint32_t start_bytes;
	uint32_t size_bytes;
	uint32_t start_entries;
	uint32_t size_entries;
	uint32_t block_entries;
	uint32_t threshold_entries;
	uint32_t amin_cfg;
	uint32_t as_cfg;
	uint8_t num_mics =
		(uint8_t)data->cfg.channel.act_num_chan;
	uint32_t sw_reset;
	uint32_t clk_mask;

	if (cfg == NULL)
		return -EINVAL;


	if ((num_mics == 0U) ||
	    (num_mics > 4U) ||
	    (cfg->audio_buf_block_size_in_bytes <
	     LPS_MEM_AUDIO_ENTRY_SIZE_IN_BYTES)) {
		return -EINVAL;
	}

	sw_reset = STICKY_RSTN_AD_SENSE;

	if (num_mics >= 1U)
		sw_reset |= STICKY_RSTN_DDF0;

	if (num_mics >= 2U)
		sw_reset |= STICKY_RSTN_DDF1;

	if (num_mics >= 3U)
		sw_reset |= STICKY_RSTN_DDF2;

	if (num_mics >= 4U)
		sw_reset |= STICKY_RSTN_DDF3;


	dmic_syna_sr100_lp_sense_reset_blocks(
		data,
		sw_reset);

	clk_mask =
		CLK_ENABLE_LP_BASE |
		CLK_ENABLE_AUDIO |
		CLK_ENABLE_LPS_MEM |
		CLK_ENABLE_LPS_CORE |
		CLK_ENABLE_LPS_REG |
		CLK_ENABLE_LPS_AUDSENSE |
		CLK_ENABLE_LPS_IPI |
		CLK_ENABLE_CFG |
		CLK_ENABLE_LPS_RES;

	if (num_mics >= 1U)
		clk_mask |= CLK_ENABLE_LPS_DDF0;

	if (num_mics >= 2U)
		clk_mask |= CLK_ENABLE_LPS_DDF1;

	if (num_mics >= 3U)
		clk_mask |= CLK_ENABLE_LPS_DDF2;

	if (num_mics >= 4U)
		clk_mask |= CLK_ENABLE_LPS_DDF3;


	sys_write32(
		sys_read32(
			GEAR1_REG(data, CLK_ENABLE_OFFSET)) |
		clk_mask,
		GEAR1_REG(data, CLK_ENABLE_OFFSET));

	as_cfg =
		LP_SENSE_AS_MIN_DM_SAMP_GAP_VALUE <<
		LP_SENSE_AS_MIN_DM_SAMP_GAP_SHIFT;

	as_cfg &= ~(LP_SENSE_AS_DM_IF0_EN |
		    LP_SENSE_AS_DM_IF1_EN);

	as_cfg &= ~(LP_SENSE_AS_TVALID(0) |
		    LP_SENSE_AS_TVALID(1) |
		    LP_SENSE_AS_TVALID(2) |
		    LP_SENSE_AS_TVALID(3));

	sys_write32(
		as_cfg,
		LPS_REG(data, LP_SENSE_AS_OFFSET));

	sys_write32(
		0U,
		LPS_REG(data, LP_SENSE_AS_FCNT_OFFSET));

	sys_write32(
		sys_read32(
			GEAR1_REG(data, AUDIO_CLK_OFFSET)) &
		~AUDIO_CLK_DM_CLK_EN,
		GEAR1_REG(data, AUDIO_CLK_OFFSET));

	block_bytes =
		ROUND_DOWN(
			cfg->audio_buf_block_size_in_bytes,
			LPS_MEM_AUDIO_ENTRY_SIZE_IN_BYTES);

	start_bytes =
		ROUND_UP(
			cfg->audio_buf_offset_in_bytes,
			LPS_MEM_AUDIO_ENTRY_SIZE_IN_BYTES);

	size_bytes =
		ROUND_DOWN(
			cfg->audio_buf_size_in_bytes,
			block_bytes);

	if (size_bytes == 0U)
		return -EINVAL;


	start_entries =
		start_bytes /
		LPS_MEM_AUDIO_ENTRY_SIZE_IN_BYTES;

	size_entries =
		size_bytes /
		LPS_MEM_AUDIO_ENTRY_SIZE_IN_BYTES;

	block_entries =
		block_bytes /
		LPS_MEM_AUDIO_ENTRY_SIZE_IN_BYTES;

	threshold_entries =
		size_entries / 2U;

	sys_write32(
		start_entries & MIF_AMIN_PTR_MASK,
		LPS_REG(data, MIF_AMIN_BSTART_OFFSET));

	sys_write32(
		size_entries & MIF_AMIN_PTR_MASK,
		LPS_REG(data, MIF_AMIN_BSIZE_OFFSET));

	sys_write32(
		((block_entries &
		  MIF_AMIN_BTHRESH_MASK)
		 << MIF_AMIN_BTHRESH_BLOCK_SHIFT) |
		(threshold_entries &
		 MIF_AMIN_BTHRESH_MASK),
		LPS_REG(data, MIF_AMIN_BTHRESH_OFFSET));

	sys_write32(
		start_entries & MIF_AMIN_PTR_MASK,
		LPS_REG(data, MIF_AMIN_RPTR_OFFSET));

	amin_cfg =
		dmic_syna_sr100_df_audio_channel_mask(
			num_mics);

	if (cfg->bit_depth ==
	    LP_SENSE_SAMPLE_SIZE_24BIT) {
		amin_cfg |=
			MIF_AMIN_BITS_PER_SAMPLE_24;
	}

	sys_write32(amin_cfg, LPS_REG(data, MIF_AMIN_OFFSET));

	sys_write32(MIF_AMIN_UPDATE_ACTIVE_CHANNELS,
		LPS_REG(data, MIF_AMIN_UPDATE_OFFSET));

	(void)sys_read32(LPS_REG(data, MIF_AMIN_UPDATE_OFFSET));

	sys_write32(0U,	LPS_REG(data, MIF_AMIN_UPDATE_OFFSET));

	(void)sys_read32(LPS_REG(data, MIF_AMIN_UPDATE_OFFSET));

	dmic_syna_sr100_program_dm_div(data, cfg->sample_rate,
		cfg->bit_depth);

	dmic_syna_sr100_audio_ddf_config(data, num_mics);

	return 0;
}

static int dmic_syna_sr100_hw_configure(struct dmic_syna_sr100_data *data)
{
	int ret;

	ret = dmic_syna_sr100_lp_sense_init_registers(data);
	if (ret < 0)
		return ret;


	ret = dmic_syna_sr100_df_init_hw(data, LP_SENSE_CLOCK_MD_G2_3_MODE2);
	if (ret < 0)
		return ret;


	ret = dmic_syna_sr100_apply_pinctrl(data, "after df init");
	if (ret < 0)
		return ret;


	ret = dmic_syna_sr100_df_audio_config_hw(data, &data->df_audio);
	if (ret < 0)
		return ret;


	ret = dmic_syna_sr100_apply_pinctrl(data, "after rx config");
	if (ret < 0)
		return ret;


	return 0;
}

static int dmic_syna_sr100_read(const struct device *dev, uint8_t stream,
				void **buffer, size_t *size, int32_t timeout)
{
	struct dmic_syna_sr100_data *data = dev->data;
	struct dmic_syna_sr100_rx_buf rx;
	k_spinlock_key_t key;
	bool active;
	int ret;

	if ((buffer == NULL) || (size == NULL))
		return -EINVAL;


	*buffer = NULL;
	*size = 0U;

	if ((stream != 0U) || (timeout < 0))
		return -EINVAL;


	key = k_spin_lock(&data->spinlock);
	active = (data->state == DMIC_STATE_ACTIVE);
	k_spin_unlock(&data->spinlock, key);

	if (!active)
		return -EACCES;


	ret = k_msgq_get(&data->rx_out_q, &rx, K_MSEC(timeout));
	if (ret < 0)
		return ret;


	*buffer = rx.buffer;
	*size = rx.size;

	return 0;
}

static void dmic_syna_sr100_ddf_channel_config(
	const struct dmic_syna_sr100_data *data,
	uint32_t ddf_base)
{
	const struct syna_dmic_ddf_reg_write *profile;
	size_t profile_len;
	uint32_t cfg1;
	uint32_t cfg2;
	uint32_t as_cfg;
	uint32_t ch;
	uint32_t pos;
	uint32_t mux;

	profile = data->config->ddf_profile;
	profile_len = data->config->ddf_profile_len;

	for (size_t i = 0; i < profile_len; i++)
		sys_write32(profile[i].val, ddf_base + profile[i].off);


	cfg1 = sys_read32(ddf_base + DDF_CFG1_OFFSET);
	cfg1 &= ~DDF_CFG1_16BIT_SAMPLE_EN;
	sys_write32(cfg1, ddf_base + DDF_CFG1_OFFSET);

	as_cfg = sys_read32(LPS_REG(data, LP_SENSE_AS_OFFSET));

	ch = (ddf_base - data->config->regs.ddf0) /
	     data->config->regs.ddf_stride;

	pos = LP_SENSE_AS_DDF_INPUT_MUX_BASE_SHIFT +
	      (ch * LP_SENSE_AS_DDF_INPUT_MUX_STRIDE);

	mux = ch & LP_SENSE_AS_DDF_INPUT_MUX_MASK;

	as_cfg &= ~(LP_SENSE_AS_DDF_INPUT_MUX_MASK << pos);
	as_cfg |= mux << pos;

	sys_write32(as_cfg, LPS_REG(data, LP_SENSE_AS_OFFSET));

	cfg2 = sys_read32(ddf_base + DDF_CFG2_OFFSET);
	cfg2 |= DDF_CFG2_CHANNEL_EN;
	sys_write32(cfg2, ddf_base + DDF_CFG2_OFFSET);
}

static void dmic_syna_sr100_mask_amin_irq(struct dmic_syna_sr100_data *data)
{
	sys_write32(sys_read32(LPS_REG(data, LPS_INT_EN_OFFSET)) &
		    ~LPS_INT_AMIN_BLOCK_RDY,
		    LPS_REG(data, LPS_INT_EN_OFFSET));

	sys_write32(LPS_STATUS_CLEAR_ALL,
		    LPS_REG(data, LPS_STATUS_OFFSET));
}

static void dmic_syna_sr100_unmask_amin_irq(struct dmic_syna_sr100_data *data)
{
	sys_write32(LPS_INT_AMIN_BLOCK_RDY, LPS_REG(data, LPS_STATUS_OFFSET));

	sys_write32(sys_read32(LPS_REG(data, LPS_INT_EN_OFFSET)) |
		    LPS_INT_AMIN_BLOCK_RDY,
		    LPS_REG(data, LPS_INT_EN_OFFSET));
}


static int dmic_syna_sr100_enable_resources(
	const struct dmic_syna_sr100_config *cfg)
{
	int ret;

	if ((cfg->clock_dev == NULL) ||
	    !device_is_ready(cfg->clock_dev)) {
		return -ENODEV;
	}

	ret = clock_control_on(cfg->clock_dev, cfg->clock_subsys);
	if ((ret < 0) &&
	    (ret != -EALREADY) &&
	    (ret != -ENOSYS))
		return ret;


	if (cfg->reset.dev != NULL) {
		if (!device_is_ready(cfg->reset.dev))
			return -ENODEV;


		ret = reset_line_deassert_dt(&cfg->reset);
		if (ret < 0)
			return ret;

	}

	return 0;
}

static int dmic_syna_sr100_df_start_hw(
	const struct dmic_syna_sr100_data *data,
	const struct st_lp_sense_df_audio *cfg)
{
	uint32_t amin_cfg;
	uint32_t as_cfg;
	uint32_t audio_clk;
	uint8_t num_mics = (uint8_t)data->cfg.channel.act_num_chan;
	uint32_t sticky_rstn;

	if ((cfg == NULL) || (num_mics == 0U) || (num_mics > 4U))
		return -EINVAL;


	sticky_rstn = STICKY_RSTN_RUN_BASE | STICKY_RSTN_AD_SENSE;

	if (num_mics >= 1U)
		sticky_rstn |= STICKY_RSTN_DDF0;

	if (num_mics >= 2U)
		sticky_rstn |= STICKY_RSTN_DDF1;

	if (num_mics >= 3U)
		sticky_rstn |= STICKY_RSTN_DDF2;

	if (num_mics >= 4U)
		sticky_rstn |= STICKY_RSTN_DDF3;


	sys_write32(sticky_rstn, GEAR1_REG(data, STICKY_RSTN_OFFSET));
	(void)sys_read32(GEAR1_REG(data, STICKY_RSTN_OFFSET));

	sys_write32(LPS_STATUS_CLEAR_ALL, LPS_REG(data, LPS_STATUS_OFFSET));

	amin_cfg = sys_read32(LPS_REG(data, MIF_AMIN_OFFSET));
	amin_cfg |= MIF_AMIN_APS_EN;
	sys_write32(amin_cfg, LPS_REG(data, MIF_AMIN_OFFSET));

	as_cfg = sys_read32(LPS_REG(data, LP_SENSE_AS_OFFSET));
	as_cfg &= ~(LP_SENSE_AS_DM_IF0_EN | LP_SENSE_AS_DM_IF1_EN |
		    LP_SENSE_AS_TVALID(0) | LP_SENSE_AS_TVALID(1) |
		    LP_SENSE_AS_TVALID(2) | LP_SENSE_AS_TVALID(3));

	if (num_mics >= 1U)
		as_cfg |= LP_SENSE_AS_DM_IF0_EN | LP_SENSE_AS_TVALID(0);

	if (num_mics >= 2U)
		as_cfg |= LP_SENSE_AS_TVALID(1);

	if (num_mics >= 3U)
		as_cfg |= LP_SENSE_AS_DM_IF1_EN | LP_SENSE_AS_TVALID(2);

	if (num_mics >= 4U)
		as_cfg |= LP_SENSE_AS_TVALID(3);


	sys_write32(as_cfg, LPS_REG(data, LP_SENSE_AS_OFFSET));

	audio_clk = AUDIO_CLK_SRC_BASE |
		AUDIO_CLK_ENABLE |
		AUDIO_CLK_DM_CLK_EN |
		AUDIO_CLK_DM_CLK_DIV_EN |
		AUDIO_CLK_DM0_CLK_ENABLE;

	if (num_mics > 2U)
		audio_clk |= AUDIO_CLK_DM1_CLK_ENABLE;


	sys_write32(audio_clk, GEAR1_REG(data, AUDIO_CLK_OFFSET));

	return 0;
}

static int dmic_syna_sr100_df_stop_hw(
	const struct dmic_syna_sr100_data *data)
{
	uint32_t amin_cfg;
	uint32_t audio_clk;
	uint32_t as_cfg;

	amin_cfg = sys_read32(LPS_REG(data, MIF_AMIN_OFFSET));
	amin_cfg &= ~MIF_AMIN_APS_EN;
	sys_write32(amin_cfg, LPS_REG(data, MIF_AMIN_OFFSET));

	audio_clk = AUDIO_CLK_SRC_BASE;
	audio_clk &= ~(AUDIO_CLK_ENABLE |
		       AUDIO_CLK_DM0_CLK_ENABLE |
		       AUDIO_CLK_DM1_CLK_ENABLE |
		       AUDIO_CLK_DM_CLK_EN |
		       AUDIO_CLK_DM_CLK_DIV_EN);
	sys_write32(audio_clk, GEAR1_REG(data, AUDIO_CLK_OFFSET));

	as_cfg = sys_read32(LPS_REG(data, LP_SENSE_AS_OFFSET));
	as_cfg &= ~(LP_SENSE_AS_DM_IF0_EN | LP_SENSE_AS_DM_IF1_EN |
		    LP_SENSE_AS_TVALID(0) | LP_SENSE_AS_TVALID(1) |
		    LP_SENSE_AS_TVALID(2) | LP_SENSE_AS_TVALID(3));
	sys_write32(as_cfg, LPS_REG(data, LP_SENSE_AS_OFFSET));

	sys_write32(sys_read32(LPS_REG(data, LPS_INT_EN_OFFSET)) &
		    ~LPS_INT_AMIN_BLOCK_RDY,
		    LPS_REG(data, LPS_INT_EN_OFFSET));

	sys_write32(LPS_STATUS_CLEAR_ALL,
		    LPS_REG(data, LPS_STATUS_OFFSET));

	return 0;
}
static int dmic_syna_sr100_check_capture_started(
	const struct dmic_syna_sr100_data *data)
{
	uint32_t audio_clk;
	uint32_t as_stat = 0U;
	uint8_t num_mics = (uint8_t)data->cfg.channel.act_num_chan;
	uint32_t fifo_empty_mask;

	audio_clk = sys_read32(GEAR1_REG(data, AUDIO_CLK_OFFSET));
	if (audio_clk == 0U)
		return -EACCES;


	fifo_empty_mask = LP_SENSE_AS_STATUS_FIFO0_EMPTY;
	if (num_mics > 2U)
		fifo_empty_mask |= LP_SENSE_AS_STATUS_FIFO1_EMPTY;

	for (uint32_t i = 0U; i < 200U; i++) {
		as_stat = sys_read32(LPS_REG(data, LP_SENSE_AS_STAT_OFFSET));

		if ((as_stat & fifo_empty_mask) == 0U)
			return 0;

		k_msleep(1);
	}

	return -ETIMEDOUT;
}

static int dmic_syna_sr100_configure(const struct device *dev,
				     struct dmic_cfg *cfg)
{
	struct dmic_syna_sr100_data *data = dev->data;
	uint32_t frame_bytes;
	uint32_t pdm_clock;
	int ret;

	if ((cfg == NULL) || (cfg->streams == NULL))
		return -EINVAL;


	k_mutex_lock(&data->lock, K_FOREVER);

	if ((data->state == DMIC_STATE_ACTIVE) ||
	    (data->state == DMIC_STATE_PAUSED)) {
		ret = -EBUSY;
		goto out_unlock;
	}

	if ((cfg->streams[0].pcm_rate != 16000U) &&
	    (cfg->streams[0].pcm_rate != 44100U) &&
	    (cfg->streams[0].pcm_rate != 48000U)) {
		ret = -EINVAL;
		goto out_unlock;
	}

	if ((cfg->channel.req_num_streams != 1U) ||
	    (cfg->channel.req_num_chan < 1U) ||
	    (cfg->channel.req_num_chan > 2U)) {
		ret = -EINVAL;
		goto out_unlock;
	}

	if ((cfg->streams[0].mem_slab == NULL) ||
	    (cfg->streams[0].block_size == 0U)) {
		ret = -EINVAL;
		goto out_unlock;
	}

	if (cfg->streams[0].pcm_width != 32U) {
		ret = -EINVAL;
		goto out_unlock;
	}

	frame_bytes = cfg->channel.req_num_chan * sizeof(uint32_t);
	if (!IS_ALIGNED(cfg->streams[0].block_size, frame_bytes) ||
	    (cfg->streams[0].block_size >
	     cfg->streams[0].mem_slab->info.block_size)) {
		ret = -EINVAL;
		goto out_unlock;
	}

	pdm_clock = cfg->streams[0].pcm_rate * 64U;
	if (((cfg->io.min_pdm_clk_freq != 0U) &&
	     (pdm_clock < cfg->io.min_pdm_clk_freq)) ||
	    ((cfg->io.max_pdm_clk_freq != 0U) &&
	     (pdm_clock > cfg->io.max_pdm_clk_freq))) {
		ret = -EINVAL;
		goto out_unlock;
	}

	dmic_syna_sr100_rx_cleanup(data);

	memset(&data->cfg, 0, sizeof(data->cfg));
	memset(&data->stream_cfg, 0, sizeof(data->stream_cfg));
	memset(&data->df_audio, 0, sizeof(data->df_audio));

	data->cfg = *cfg;
	data->stream_cfg = cfg->streams[0];
	data->cfg.streams = &data->stream_cfg;

	data->cfg.channel.act_num_streams = 1U;
	data->cfg.channel.act_num_chan = cfg->channel.req_num_chan;
	data->cfg.channel.act_chan_map_lo = cfg->channel.req_chan_map_lo;
	data->cfg.channel.act_chan_map_hi = cfg->channel.req_chan_map_hi;

	ret = dmic_syna_sr100_build_df_audio_cfg(data, &data->df_audio);
	if (ret < 0)
		goto configure_error;


	ret = dmic_syna_sr100_hw_configure(data);
	if (ret < 0)
		goto configure_error;


	data->state = DMIC_STATE_CONFIGURED;
	ret = 0;
	goto out_unlock;

configure_error:
	(void)dmic_syna_sr100_df_stop_hw(data);
	dmic_syna_sr100_rx_cleanup(data);

	memset(&data->cfg, 0, sizeof(data->cfg));
	memset(&data->stream_cfg, 0, sizeof(data->stream_cfg));
	memset(&data->df_audio, 0, sizeof(data->df_audio));

	data->state = DMIC_STATE_ERROR;

out_unlock:
	k_mutex_unlock(&data->lock);
	return ret;
}

static int dmic_syna_sr100_init(const struct device *dev)
{
	const struct dmic_syna_sr100_config *cfg = dev->config;
	struct dmic_syna_sr100_data *data = dev->data;
	int ret;

	memset(data, 0, sizeof(*data));

	data->config = cfg;
	data->state = DMIC_STATE_INITIALIZED;

	k_mutex_init(&data->lock);
	k_work_init(&data->amin_block_ready_event,
		    dmic_syna_sr100_handle_amin_block_ready);
	k_work_init_delayable(&data->amin_rx_retry_work,
			      dmic_syna_sr100_handle_rx_retry);

	k_msgq_init(&data->rx_pending_q,
		    data->rx_pending_buf,
		    sizeof(struct dmic_syna_sr100_rx_buf),
		    SYNA_DMIC_RX_QUEUE_LEN);

	k_msgq_init(&data->rx_out_q,
		    data->rx_out_buf,
		    sizeof(struct dmic_syna_sr100_rx_buf),
		    SYNA_DMIC_RX_QUEUE_LEN);

	data->dma.dev = cfg->dma_dev;
	data->dma.channel = cfg->dma_chan;
	data->dma.data = data;

	data->dma_active = false;
	data->stop_pending = false;
	data->amin_sw_rptr_entries = 0U;
	atomic_set(&data->rx_drops, 0);
	atomic_set(&data->rx_overruns, 0);
	atomic_set(&data->dma_errors, 0);

	ret = dmic_syna_sr100_enable_resources(cfg);
	if (ret < 0)
		return ret;


	ret = dmic_syna_sr100_apply_pinctrl(data, "init");
	if (ret < 0)
		return ret;


	if ((data->dma.dev == NULL) || !device_is_ready(data->dma.dev))
		return -ENODEV;


	cfg->irq_config();

	return 0;
}


static const struct _dmic_ops dmic_syna_sr100_api = {
	.configure = dmic_syna_sr100_configure,
	.trigger = dmic_syna_sr100_trigger,
	.read = dmic_syna_sr100_read,
};


#define SYNA_DMIC_DEFINE(inst)                                                   \
	BUILD_ASSERT((DT_INST_PROP_LEN(inst, ddf_profile) % 2) == 0,             \
		     "ddf-profile must contain offset/value pairs");              \
	static const union {                                                       \
		uint32_t cells[DT_INST_PROP_LEN(inst, ddf_profile)];               \
		struct syna_dmic_ddf_reg_write                                    \
			writes[DT_INST_PROP_LEN(inst, ddf_profile) / 2];             \
	} dmic_syna_sr100_ddf_profile_##inst = {                                  \
		.cells = DT_INST_PROP(inst, ddf_profile),                           \
	};                                                                         \
\
	PINCTRL_DT_INST_DEFINE(inst);                                            \
	static void dmic_syna_sr100_irq_config_##inst(void)                      \
	{                                                                         \
		IRQ_CONNECT(DT_INST_IRQN(inst), DT_INST_IRQ(inst, priority),        \
			    dmic_syna_sr100_amin_isr, DEVICE_DT_INST_GET(inst), 0);  \
		irq_enable(DT_INST_IRQN(inst));                                      \
	}                                                                         \
	static const struct dmic_syna_sr100_config dmic_syna_sr100_cfg_##inst = { \
		.dma_dev = COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, dmas),        \
			(DEVICE_DT_GET(DT_INST_DMAS_CTLR(inst))), (NULL)),       \
		.dma_chan = COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, dmas),       \
			(DT_INST_DMAS_CELL_BY_IDX(inst, 0, channel)), (0U)),     \
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(inst)),           \
		.clock_subsys =                                                  \
			(clock_control_subsys_t)DT_INST_CLOCKS_CELL(inst, clkid), \
		.reset = RESET_DT_SPEC_INST_GET(inst),                           \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(inst),                    \
		.irq_config = dmic_syna_sr100_irq_config_##inst,                 \
		.regs = {                                                        \
			.global = DT_INST_REG_ADDR_BY_NAME(inst, global),        \
			.lps = DT_INST_REG_ADDR_BY_NAME(inst, lps),              \
			.gear1 = DT_INST_REG_ADDR_BY_NAME(inst, gear1),          \
			.lp_mem = DT_INST_REG_ADDR_BY_NAME(inst, lp_mem),        \
			.ddf0 = DT_INST_REG_ADDR_BY_NAME(inst, ddf0),            \
			.ddf_stride = DT_INST_PROP(inst, ddf_stride),            \
		},                                                               \
		.ddf_profile = dmic_syna_sr100_ddf_profile_##inst.writes,         \
		.ddf_profile_len =                                                \
			ARRAY_SIZE(dmic_syna_sr100_ddf_profile_##inst.writes),      \
	};                                                                       \
\
	static struct dmic_syna_sr100_data dmic_syna_sr100_data_##inst;          \
\
	DEVICE_DT_INST_DEFINE(inst,                                             \
			      dmic_syna_sr100_init,                             \
			      NULL,                                             \
			      &dmic_syna_sr100_data_##inst,                     \
			      &dmic_syna_sr100_cfg_##inst,                      \
			      POST_KERNEL,                                      \
			      CONFIG_AUDIO_DMIC_INIT_PRIORITY,                  \
			      &dmic_syna_sr100_api)

DT_INST_FOREACH_STATUS_OKAY(SYNA_DMIC_DEFINE)
