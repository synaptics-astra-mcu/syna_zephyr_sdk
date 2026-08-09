/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT syna_sr100_i2s_sr100

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/cache.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#include <soc.h>

#define QUEUE_LEN                     16U
#define INIT_PRIORITY                 50U
#define I2S_SR100_DMA_CTX_ALIGN       32U

#define PCM1_RX_CLK_FRAMES_MASK        GENMASK(7, 0)
#define PCM1_RX_SYNC_LEN_MASK          GENMASK(15, 8)
#define PCM1_RX_SYNC_LEN_SHIFT         8U
#define PCM1_RX_LRCK_EN                BIT(17)
#define PCM2_TX_CLK_FRAMES_MASK        GENMASK(7, 0)
#define PCM2_TX_SYNC_LEN_MASK          GENMASK(15, 8)
#define PCM2_TX_SYNC_LEN_SHIFT         8U
#define PCM2_TX_LRCK_EN                BIT(17)
#define PCM4_RX_MASTER                 BIT(1)
#define PCM4_RX_DSTART_DLY             BIT(2)
#define PCM4_RX_LRCK_POL               BIT(6)
#define PCM4_RX_SA_WDT_MASK            GENMASK(11, 7)
#define PCM4_RX_SA_WDT_SHIFT           7U
#define PCM5_RX_SD_SEL_MASK            GENMASK(3, 0)
#define PCM6_RX_SLOT_MASK              GENMASK(31, 0)
#define PCM8_RX_EN_MASK                GENMASK(3, 0)
#define PCM8_RX_MUTE                   BIT(4)
#define PCM9_TX_MASTER                 BIT(1)
#define PCM9_TX_DSTART_DLY             BIT(2)
#define PCM9_TX_LRCK_POL               BIT(6)
#define PCM9_TX_OUT_EN                 BIT(7)
#define PCM9_TX_SA_WDT_MASK            GENMASK(13, 9)
#define PCM9_TX_SA_WDT_SHIFT           9U
#define PCM10_TX_SLOT_MASK             GENMASK(31, 0)
#define PCM12_TX_SD_SEL_MASK           GENMASK(3, 0)
#define PCM13_TX_EN_MASK               GENMASK(3, 0)
#define PCM13_TX_MUTE                  BIT(4)
#define PCM_FIFO_FIELD(ch, val)        (((uint32_t)(val) << ((ch) * 8U)) & \
					 GENMASK(((ch) * 8U) + 4U, ((ch) * 8U)))
#define PCM14_TX_AEMPTY(ch, val)       PCM_FIFO_FIELD(ch, val)
#define PCM15_TX_POP(ch, val)          PCM_FIFO_FIELD(ch, val)
#define PCM16_RX_AFULL(ch, val)        PCM_FIFO_FIELD(ch, val)
#define PCM_FIFO_LEVELS(channels, field, val) \
	(field(0U, val) | \
	 (((channels) > 1U) ? field(1U, val) : 0U) | \
	 (((channels) > 2U) ? field(2U, val) : 0U) | \
	 (((channels) > 3U) ? field(3U, val) : 0U))
#define LPS_AUDIO_CLK_OFFSET           0x28U
#define LPS_AUDIO_CLK_SRC_MASK         GENMASK(2, 0)
#define LPS_AUDIO_CLK_SRC_XTAL24M      1U
#define LPS_AUDIO_CLK_ENABLE           BIT(3)
#define LPS_I2S_CLK_EN                 BIT(7)
#define LPS_I2S_CLK_DIV_EN             BIT(9)
#define LPS_I2S_DIV_OFFSET             0x30U
#define LPS_SOF_CTL_OFFSET             0x3CU
#define LPS_SOF_I2S_DIV_CFG_UPD        BIT(2)
#define LPS_I2S_DIV_INT_MASK           GENMASK(7, 0)
#define LPS_I2S_DIV_FRAC_MASK          GENMASK(31, 8)
#define LPS_I2S_DIV_FRAC_SHIFT         8U
#define GLOBAL_I2S_CTRL_OFFSET         0x07B4U
#define GLOBAL_PERIF_I2S_CTRL_OFFSET   0x0A88U
#define GLOBAL_I2S_DICNTL_OFFSET       0x884CU
#define GLOBAL_I2S_FSYNCCNTL_OFFSET    0x8854U
#define GLOBAL_I2S_BCLKCNTL_OFFSET     0x8858U
#define GLOBAL_I2S_PAD_IE              BIT(3)
#define GLOBAL_I2S_PAD_PE              BIT(4)
#define GLOBAL_I2S_LRCK_SEL_RX_OUT     BIT(0)
#define GLOBAL_I2S_CTRL_TX_ENABLE      BIT(0)
#define GLOBAL_I2S_CTRL_RX_ENABLE      BIT(1)
#define GLOBAL_I2S_CTRL_TX_POL         BIT(2)
#define GLOBAL_I2S_CTRL_RX_POL         BIT(3)
#define BCLK_DIV_INT_8K                0x2EU
#define BCLK_DIV_FRAC_8K               0xE00000U
#define BCLK_DIV_INT_16K               0x17U
#define BCLK_DIV_FRAC_16K              0x700000U
#define BCLK_DIV_INT_44K1              0x08U
#define BCLK_DIV_FRAC_44K1             0x80DEE9U
#define BCLK_DIV_INT_48K               0x07U
#define BCLK_DIV_FRAC_48K              0xD00000U
#define AUDIO_SRC_SEL                  BIT(0)
#define TX_AEMPTY_WORDS                4U
#define TX_POP_WORDS                   3U
#define RX_AFULL_WORDS                 8U
#define REG32(base, off)               (*(volatile uint32_t *)((uintptr_t)(base) + (off)))
#define GLOBAL_REG(cfg, off)           REG32((cfg)->regs.global, off)
#define LPS_REG(cfg, off)              REG32((cfg)->regs.lps, off)
#define I2S_CHANNEL_MASK(channels)     (((channels) == 0U) ? 0U : \
					 (((channels) >= 4U) ? 0xFU : (BIT(channels) - 1U)))
#define I2S_DATA_FORMAT(cfg)        ((cfg)->format & I2S_FMT_DATA_FORMAT_MASK)
#define I2S_EFFECTIVE_CHANNELS(cfg)                                                   \
	(((I2S_DATA_FORMAT(cfg) == I2S_FMT_DATA_FORMAT_I2S) ||                        \
	  (I2S_DATA_FORMAT(cfg) == I2S_FMT_DATA_FORMAT_LEFT_JUSTIFIED) ||             \
	  (I2S_DATA_FORMAT(cfg) == I2S_FMT_DATA_FORMAT_RIGHT_JUSTIFIED)) ?            \
	 2U : (cfg)->channels)
#define I2S_SLOT_BYTES(word_size)  sizeof(uint32_t)
#define I2S_BITCLK_INV(format)     (((format) & I2S_FMT_BIT_CLK_INV) != 0U)
#define I2S_FRAMECLK_INV(format)   (((format) & I2S_FMT_FRAME_CLK_INV) != 0U)

struct i2s_syna_sr100_block {
	void *mem_block;
	size_t size;
};

enum i2s_syna_sr100_stop_mode {
	I2S_SR100_STOP_NONE = 0,
	I2S_SR100_STOP,
	I2S_SR100_DRAIN,
};

struct i2s_syna_sr100_stream {
	struct i2s_config cfg;
	enum i2s_state state;
	bool configured;
	enum i2s_syna_sr100_stop_mode stop_mode;
};

struct i2s_syna_sr100_dma_ctx {
	bool dma_busy;
	uint32_t queued_blocks;
	uint32_t completed_blocks;
	struct k_msgq pending_q;
	struct i2s_syna_sr100_block pending_q_buf[QUEUE_LEN];
	struct dma_config dma_cfg;
	struct dma_block_config block_cfg[QUEUE_LEN];
} __aligned(I2S_SR100_DMA_CTX_ALIGN);

struct sr100_i2s_reg {
	volatile uint32_t pcm1;
	volatile uint32_t pcm2;
	volatile uint32_t pcm3;
	volatile uint32_t pcm4;
	volatile uint32_t pcm5;
	volatile uint32_t pcm6;
	volatile uint32_t reserved_018;
	volatile uint32_t pcm8;
	volatile uint32_t pcm9;
	volatile uint32_t pcm10;
	volatile uint32_t reserved_028;
	volatile uint32_t pcm12;
	volatile uint32_t pcm13;
	volatile uint32_t pcm14;
	volatile uint32_t pcm15;
	volatile uint32_t pcm16;
	volatile uint32_t reserved_040;
	volatile uint32_t irq_en;
	volatile uint32_t irq_state;
	volatile uint32_t irq_state_raw;
	volatile uint32_t audio_src;
	volatile uint32_t reserved_054_4FC[299];
	volatile uint32_t txfifo;
	volatile uint32_t reserved_504_9FC[319];
	volatile uint32_t rxfifo;
};

struct i2s_syna_sr100_regs {
	uintptr_t i2s;
	uintptr_t global;
	uintptr_t lps;
};

struct i2s_syna_sr100_config {
	struct i2s_syna_sr100_regs regs;
	const struct pinctrl_dev_config *pcfg;
	const struct device *clk_dev;
	clock_control_subsys_t cfg_clk_subsys;
	clock_control_subsys_t tx_clk_subsys;
	clock_control_subsys_t rx_clk_subsys;
	const struct device *dma_tx_dev;
	const struct device *dma_rx_dev;
	uint32_t dma_tx_channel;
	uint32_t dma_rx_channel;
	bool has_dma_tx;
	bool has_dma_rx;
};

struct i2s_syna_sr100_data {
	const struct device *dev;
	struct i2s_syna_sr100_stream tx;
	struct i2s_syna_sr100_stream rx;
	struct k_mutex lock;

	struct k_msgq tx_in_q;
	struct k_msgq rx_out_q;
	char tx_in_q_buf[QUEUE_LEN * sizeof(struct i2s_syna_sr100_block)];
	char rx_out_q_buf[QUEUE_LEN * sizeof(struct i2s_syna_sr100_block)];

	struct i2s_syna_sr100_dma_ctx tx_ctx;
	struct i2s_syna_sr100_dma_ctx rx_ctx;

	bool loopback;
};

static void i2s_syna_sr100_tx_stop_dma(struct i2s_syna_sr100_data *data);
static void i2s_syna_sr100_rx_stop_dma(struct i2s_syna_sr100_data *data);
static void i2s_syna_sr100_tx_cleanup(struct i2s_syna_sr100_data *data);
static void i2s_syna_sr100_rx_cleanup(struct i2s_syna_sr100_data *data);
static int i2s_syna_sr100_tx_submit_batch(const struct device *dev);
static int i2s_syna_sr100_rx_submit_batch(const struct device *dev);

static void i2s_syna_sr100_program_bclk_div(const struct i2s_syna_sr100_config *cfg,
					    const struct i2s_config *i2s_cfg)
{
	uint64_t scaled_div;
	uint64_t bclk_hz;
	uint32_t div_int;
	uint32_t div_frac;
	uint32_t value;

	switch (i2s_cfg->frame_clk_freq) {
	case 8000U:
		div_int = BCLK_DIV_INT_8K;
		div_frac = BCLK_DIV_FRAC_8K;
		break;
	case 16000U:
		div_int = BCLK_DIV_INT_16K;
		div_frac = BCLK_DIV_FRAC_16K;
		break;
	case 44100U:
		div_int = BCLK_DIV_INT_44K1;
		div_frac = BCLK_DIV_FRAC_44K1;
		break;
	case 48000U:
		div_int = BCLK_DIV_INT_48K;
		div_frac = BCLK_DIV_FRAC_48K;
		break;
	default:
		bclk_hz = (uint64_t)i2s_cfg->frame_clk_freq * 64ULL;
		if (bclk_hz == 0U) {
			return;
		}

		scaled_div = (24000000ULL << 24) / bclk_hz;
		div_int = (uint32_t)(scaled_div >> 24);
		if ((div_int == 0U) || (div_int > 0xFFU)) {
			return;
		}

		div_frac = (uint32_t)(scaled_div & 0xFFFFFFU);
		break;
	}

	value = ((div_frac << LPS_I2S_DIV_FRAC_SHIFT) &
		 LPS_I2S_DIV_FRAC_MASK) |
		(div_int & LPS_I2S_DIV_INT_MASK);
	LPS_REG(cfg, LPS_I2S_DIV_OFFSET) = value;
	LPS_REG(cfg, LPS_SOF_CTL_OFFSET) =
		(LPS_REG(cfg, LPS_SOF_CTL_OFFSET) & ~LPS_SOF_I2S_DIV_CFG_UPD) |
		LPS_SOF_I2S_DIV_CFG_UPD;
}

static void i2s_syna_sr100_set_tx_bclk_polarity(const struct i2s_syna_sr100_config *cfg,
						bool invert)
{
	uint32_t value;

	value = GLOBAL_REG(cfg, GLOBAL_I2S_CTRL_OFFSET);
	value = (value & ~GLOBAL_I2S_CTRL_TX_POL) |
		(invert ? GLOBAL_I2S_CTRL_TX_POL : 0U);
	GLOBAL_REG(cfg, GLOBAL_I2S_CTRL_OFFSET) = value;
}

static void i2s_syna_sr100_set_rx_bclk_polarity(const struct i2s_syna_sr100_config *cfg,
						bool invert)
{
	uint32_t value;

	value = GLOBAL_REG(cfg, GLOBAL_I2S_CTRL_OFFSET);
	value = (value & ~GLOBAL_I2S_CTRL_RX_POL) |
		(invert ? GLOBAL_I2S_CTRL_RX_POL : 0U);
	GLOBAL_REG(cfg, GLOBAL_I2S_CTRL_OFFSET) = value;
}

static void i2s_syna_sr100_apply_global_state(const struct i2s_syna_sr100_config *cfg,
					      bool tx, bool rx, bool tx_master,
					      bool rx_master, bool tx_target,
					      bool rx_target)
{
	const uint32_t pad_mask = GLOBAL_I2S_PAD_IE | GLOBAL_I2S_PAD_PE;
	bool route_rx_lrck = rx && !tx;
	uint32_t bclk_pad = 0U;
	uint32_t fsync_pad = 0U;
	uint32_t gctl = 0U;
	uint32_t value;

	if (tx) {
		gctl |= GLOBAL_I2S_CTRL_TX_ENABLE;
	}
	if (rx) {
		gctl |= GLOBAL_I2S_CTRL_RX_ENABLE;
	}

	if ((tx_master || rx_master) && (tx_target || rx_target)) {
		bclk_pad = GLOBAL_I2S_PAD_IE | GLOBAL_I2S_PAD_PE;
		fsync_pad = GLOBAL_I2S_PAD_IE | GLOBAL_I2S_PAD_PE;
	} else if (tx_master || rx_master || tx_target || rx_target) {
		bclk_pad = GLOBAL_I2S_PAD_IE;
		fsync_pad = GLOBAL_I2S_PAD_IE;
	}

	value = GLOBAL_REG(cfg, GLOBAL_I2S_BCLKCNTL_OFFSET);
	value = (value & ~pad_mask) | bclk_pad;
	GLOBAL_REG(cfg, GLOBAL_I2S_BCLKCNTL_OFFSET) = value;

	value = GLOBAL_REG(cfg, GLOBAL_I2S_FSYNCCNTL_OFFSET);
	value = (value & ~pad_mask) | fsync_pad;
	GLOBAL_REG(cfg, GLOBAL_I2S_FSYNCCNTL_OFFSET) = value;

	value = GLOBAL_REG(cfg, GLOBAL_I2S_DICNTL_OFFSET);
	value = (value & ~pad_mask) | (rx ? GLOBAL_I2S_PAD_IE : 0U);
	GLOBAL_REG(cfg, GLOBAL_I2S_DICNTL_OFFSET) = value;

	value = GLOBAL_REG(cfg, GLOBAL_PERIF_I2S_CTRL_OFFSET);
	value = (value & ~GLOBAL_I2S_LRCK_SEL_RX_OUT) |
		(route_rx_lrck ? GLOBAL_I2S_LRCK_SEL_RX_OUT : 0U);
	GLOBAL_REG(cfg, GLOBAL_PERIF_I2S_CTRL_OFFSET) = value;

	value = GLOBAL_REG(cfg, GLOBAL_I2S_CTRL_OFFSET);
	value &= ~(GLOBAL_I2S_CTRL_TX_ENABLE | GLOBAL_I2S_CTRL_RX_ENABLE);
	value |= gctl;
	GLOBAL_REG(cfg, GLOBAL_I2S_CTRL_OFFSET) = value;
}

static void i2s_syna_sr100_hw_config_tx(const struct i2s_syna_sr100_config *cfg,
					const struct i2s_config *tx_cfg)
{
	struct sr100_i2s_reg *i2s = (void *)cfg->regs.i2s;
	uint8_t channels = I2S_EFFECTIVE_CHANNELS(tx_cfg);
	uint32_t ch_mask = I2S_CHANNEL_MASK(channels);
	uint32_t frame_clks = (32U * channels) - 1U;
	uint32_t sync_half = (channels > 1U) ? (channels / 2U) : 1U;
	uint32_t sync_len = (32U * sync_half) - 1U;
	uint32_t wdt = tx_cfg->word_size - 1U;
	uint8_t fmt = tx_cfg->format & I2S_FMT_DATA_FORMAT_MASK;
	bool master = ((tx_cfg->options & I2S_OPT_BIT_CLK_TARGET) == 0U);
	bool dstart_dly = false;
	bool lrck_inv_base = false;
	uint32_t slot_offset = 0U;
	bool lrck_inv;
	uint32_t pcm2;
	uint32_t pcm9 = 0U;
	uint32_t pcm10;
	uint32_t pcm12;
	uint32_t pcm14;
	uint32_t pcm15;

	switch (fmt) {
	case I2S_FMT_DATA_FORMAT_I2S:
		dstart_dly = true;
		break;
	case I2S_FMT_DATA_FORMAT_LEFT_JUSTIFIED:
	case I2S_FMT_DATA_FORMAT_RIGHT_JUSTIFIED:
		lrck_inv_base = true;
		break;
	default:
		break;
	}

	lrck_inv = lrck_inv_base ^ I2S_FRAMECLK_INV(tx_cfg->format);

	pcm2 = (frame_clks & PCM2_TX_CLK_FRAMES_MASK) |
	       ((sync_len << PCM2_TX_SYNC_LEN_SHIFT) & PCM2_TX_SYNC_LEN_MASK);
	i2s->pcm2 = pcm2;

	if (master) {
		pcm9 |= PCM9_TX_MASTER;
	}
	if (dstart_dly) {
		pcm9 |= PCM9_TX_DSTART_DLY;
	}
	if (lrck_inv) {
		pcm9 |= PCM9_TX_LRCK_POL;
	}
	pcm9 |= (wdt << PCM9_TX_SA_WDT_SHIFT) & PCM9_TX_SA_WDT_MASK;
	i2s->pcm9 = pcm9;

	pcm10 = slot_offset & PCM10_TX_SLOT_MASK;
	i2s->pcm10 = pcm10;

	pcm12 = ch_mask & PCM12_TX_SD_SEL_MASK;
	i2s->pcm12 = pcm12;

	pcm14 = PCM_FIFO_LEVELS(channels, PCM14_TX_AEMPTY, TX_AEMPTY_WORDS);
	pcm15 = PCM_FIFO_LEVELS(channels, PCM15_TX_POP, TX_POP_WORDS);
	i2s->pcm14 = pcm14;
	i2s->pcm15 = pcm15;

	i2s_syna_sr100_set_tx_bclk_polarity(cfg, I2S_BITCLK_INV(tx_cfg->format));

	if (master) {
		i2s_syna_sr100_program_bclk_div(cfg, tx_cfg);
	}
}

static void i2s_syna_sr100_hw_config_rx(const struct i2s_syna_sr100_config *cfg,
					const struct i2s_config *rx_cfg)
{
	struct sr100_i2s_reg *i2s = (void *)cfg->regs.i2s;
	uint8_t channels = I2S_EFFECTIVE_CHANNELS(rx_cfg);
	uint32_t ch_mask = I2S_CHANNEL_MASK(channels);
	uint32_t frame_clks = (32U * channels) - 1U;
	uint32_t sync_half = (channels > 1U) ? (channels / 2U) : 1U;
	uint32_t sync_len = (32U * sync_half) - 1U;
	uint32_t wdt = rx_cfg->word_size - 1U;
	uint8_t fmt = rx_cfg->format & I2S_FMT_DATA_FORMAT_MASK;
	bool master = ((rx_cfg->options & I2S_OPT_BIT_CLK_TARGET) == 0U);
	bool dstart_dly = false;
	bool lrck_inv_base = false;
	uint32_t slot_offset = 0U;
	bool lrck_inv;
	uint32_t pcm1;
	uint32_t pcm4 = 0U;
	uint32_t pcm5;
	uint32_t pcm6;
	uint32_t pcm16;

	switch (fmt) {
	case I2S_FMT_DATA_FORMAT_I2S:
		dstart_dly = true;
		break;
	case I2S_FMT_DATA_FORMAT_LEFT_JUSTIFIED:
	case I2S_FMT_DATA_FORMAT_RIGHT_JUSTIFIED:
		lrck_inv_base = true;
		break;
	default:
		break;
	}

	lrck_inv = lrck_inv_base ^ I2S_FRAMECLK_INV(rx_cfg->format);

	pcm1 = (frame_clks & PCM1_RX_CLK_FRAMES_MASK) |
	       ((sync_len << PCM1_RX_SYNC_LEN_SHIFT) & PCM1_RX_SYNC_LEN_MASK);
	i2s->pcm1 = pcm1;

	if (master) {
		pcm4 |= PCM4_RX_MASTER;
	}
	if (dstart_dly) {
		pcm4 |= PCM4_RX_DSTART_DLY;
	}
	if (lrck_inv) {
		pcm4 |= PCM4_RX_LRCK_POL;
	}
	pcm4 |= (wdt << PCM4_RX_SA_WDT_SHIFT) & PCM4_RX_SA_WDT_MASK;
	i2s->pcm4 = pcm4;

	pcm5 = ch_mask & PCM5_RX_SD_SEL_MASK;
	i2s->pcm5 = pcm5;

	pcm6 = slot_offset & PCM6_RX_SLOT_MASK;
	i2s->pcm6 = pcm6;

	pcm16 = PCM_FIFO_LEVELS(channels, PCM16_RX_AFULL, RX_AFULL_WORDS);
	i2s->pcm16 = pcm16;

	i2s_syna_sr100_set_rx_bclk_polarity(cfg, I2S_BITCLK_INV(rx_cfg->format));

	if (master) {
		i2s_syna_sr100_program_bclk_div(cfg, rx_cfg);
	}
}

static void i2s_syna_sr100_apply_hw_state(const struct i2s_syna_sr100_config *cfg,
					  const struct i2s_syna_sr100_data *data,
					  bool tx, bool rx)
{
	struct sr100_i2s_reg *i2s = (void *)cfg->regs.i2s;
	uint8_t tx_channels = (tx && (data != NULL)) ?
			      I2S_EFFECTIVE_CHANNELS(&data->tx.cfg) : 0U;
	uint8_t rx_channels = (rx && (data != NULL)) ?
			      I2S_EFFECTIVE_CHANNELS(&data->rx.cfg) : 0U;
	uint32_t tx_mask = I2S_CHANNEL_MASK(tx_channels);
	uint32_t rx_mask = I2S_CHANNEL_MASK(rx_channels);
	bool tx_master = tx && (data != NULL) && data->tx.configured &&
			 ((data->tx.cfg.options & I2S_OPT_BIT_CLK_TARGET) == 0U);
	bool rx_master = rx && (data != NULL) && data->rx.configured &&
			 ((data->rx.cfg.options & I2S_OPT_BIT_CLK_TARGET) == 0U);
	bool tx_target = tx && (data != NULL) && data->tx.configured &&
			 ((data->tx.cfg.options & I2S_OPT_BIT_CLK_TARGET) != 0U);
	bool rx_target = rx && (data != NULL) && data->rx.configured &&
			 ((data->rx.cfg.options & I2S_OPT_BIT_CLK_TARGET) != 0U);
	uint32_t audio_clk;
	uint32_t pcm1;
	uint32_t pcm2;
	uint32_t pcm8;
	uint32_t pcm9;
	uint32_t pcm13;

	if (tx || rx) {
		audio_clk = LPS_REG(cfg, LPS_AUDIO_CLK_OFFSET);
		audio_clk &= ~LPS_AUDIO_CLK_SRC_MASK;
		audio_clk |= LPS_AUDIO_CLK_SRC_XTAL24M;
		audio_clk |= LPS_AUDIO_CLK_ENABLE |
			     LPS_I2S_CLK_EN |
			     LPS_I2S_CLK_DIV_EN;
		LPS_REG(cfg, LPS_AUDIO_CLK_OFFSET) = audio_clk;
	} else {
		LPS_REG(cfg, LPS_AUDIO_CLK_OFFSET) &=
			~(LPS_I2S_CLK_EN | LPS_I2S_CLK_DIV_EN);
	}

	i2s_syna_sr100_apply_global_state(cfg, tx, rx, tx_master, rx_master,
					  tx_target, rx_target);

	pcm13 = i2s->pcm13;
	pcm13 &= ~(PCM13_TX_EN_MASK | PCM13_TX_MUTE);
	pcm13 |= tx_mask & PCM13_TX_EN_MASK;
	i2s->pcm13 = pcm13;

	pcm2 = i2s->pcm2;
	pcm2 = tx ? (pcm2 | PCM2_TX_LRCK_EN) : (pcm2 & ~PCM2_TX_LRCK_EN);
	i2s->pcm2 = pcm2;

	pcm9 = i2s->pcm9;
	pcm9 = tx ? (pcm9 | PCM9_TX_OUT_EN) : (pcm9 & ~PCM9_TX_OUT_EN);
	i2s->pcm9 = pcm9;

	pcm1 = i2s->pcm1;
	pcm1 = rx ? (pcm1 | PCM1_RX_LRCK_EN) : (pcm1 & ~PCM1_RX_LRCK_EN);
	i2s->pcm1 = pcm1;

	pcm8 = i2s->pcm8;
	pcm8 &= ~(PCM8_RX_EN_MASK | PCM8_RX_MUTE);
	pcm8 |= rx_mask & PCM8_RX_EN_MASK;
	i2s->pcm8 = pcm8;

	i2s->irq_en = 0U;
}

static void i2s_syna_sr100_runtime_reset(struct i2s_syna_sr100_dma_ctx *ctx)
{
	ctx->dma_busy = false;
	ctx->queued_blocks = 0U;
	ctx->completed_blocks = 0U;
}

static void i2s_syna_sr100_tx_error(struct i2s_syna_sr100_data *data)
{
	i2s_syna_sr100_tx_stop_dma(data);

	data->tx.state = I2S_STATE_ERROR;
	data->tx.stop_mode = I2S_SR100_STOP_NONE;

	i2s_syna_sr100_apply_hw_state(data->dev->config, data, false,
		(data->rx.state == I2S_STATE_RUNNING) ||
		(data->rx.state == I2S_STATE_STOPPING));
}

static void i2s_syna_sr100_rx_error(struct i2s_syna_sr100_data *data)
{
	i2s_syna_sr100_rx_stop_dma(data);

	data->rx.state = I2S_STATE_ERROR;
	data->rx.stop_mode = I2S_SR100_STOP_NONE;

	i2s_syna_sr100_apply_hw_state(data->dev->config, data,
		(data->tx.state == I2S_STATE_RUNNING) ||
		(data->tx.state == I2S_STATE_STOPPING),
		false);
}


static void i2s_syna_sr100_dma_tx_cb(const struct device *dma_dev, void *user_data,
				     uint32_t channel, int status)
{
	const struct device *dev = user_data;
	struct i2s_syna_sr100_data *data = dev->data;
	struct i2s_syna_sr100_block done;
	int ret;

	ARG_UNUSED(dma_dev);
	ARG_UNUSED(channel);

	if ((status != DMA_STATUS_BLOCK) && (status != DMA_STATUS_COMPLETE)) {
		i2s_syna_sr100_tx_error(data);
		return;
	}

	ret = k_msgq_get(&data->tx_ctx.pending_q, &done, K_NO_WAIT);
	if (ret != 0) {
		i2s_syna_sr100_tx_error(data);
		return;
	}

	if ((data->tx.cfg.mem_slab != NULL) && (done.mem_block != NULL)) {
		k_mem_slab_free(data->tx.cfg.mem_slab, done.mem_block);
	}
	data->tx_ctx.completed_blocks++;

	if (data->tx.stop_mode == I2S_SR100_STOP) {
		i2s_syna_sr100_tx_stop_dma(data);
		data->tx.state = I2S_STATE_READY;
		data->tx.stop_mode = I2S_SR100_STOP_NONE;
		i2s_syna_sr100_apply_hw_state(dev->config, data,
					      (data->tx.state == I2S_STATE_RUNNING) ||
					      (data->tx.state == I2S_STATE_STOPPING),
					      (data->rx.state == I2S_STATE_RUNNING) ||
					      (data->rx.state == I2S_STATE_STOPPING));
		return;
	}

	if (status == DMA_STATUS_COMPLETE) {
		data->tx_ctx.dma_busy = false;

		if ((data->tx.state == I2S_STATE_RUNNING) ||
		    (data->tx.state == I2S_STATE_STOPPING)) {
			ret = i2s_syna_sr100_tx_submit_batch(dev);
			if ((ret != 0) && (ret != -ENODATA)) {
				i2s_syna_sr100_tx_error(data);
				return;
			}
		}

		if ((data->tx.stop_mode == I2S_SR100_DRAIN) &&
		    !data->tx_ctx.dma_busy &&
		    (k_msgq_num_used_get(&data->tx_in_q) == 0U) &&
		    (k_msgq_num_used_get(&data->tx_ctx.pending_q) == 0U) &&
		    (data->tx_ctx.completed_blocks == data->tx_ctx.queued_blocks)) {
			i2s_syna_sr100_runtime_reset(&data->tx_ctx);
			data->tx.stop_mode = I2S_SR100_STOP_NONE;
			data->tx.state = I2S_STATE_READY;
			i2s_syna_sr100_apply_hw_state(dev->config, data,
						      (data->tx.state == I2S_STATE_RUNNING) ||
						      (data->tx.state == I2S_STATE_STOPPING),
						      (data->rx.state == I2S_STATE_RUNNING) ||
						      (data->rx.state == I2S_STATE_STOPPING));
		}
	}

}

static void i2s_syna_sr100_dma_rx_cb(const struct device *dma_dev, void *user_data,
				     uint32_t channel, int status)
{
	const struct device *dev = user_data;
	struct i2s_syna_sr100_data *data = dev->data;
	struct i2s_syna_sr100_block done;
	int ret;

	ARG_UNUSED(dma_dev);
	ARG_UNUSED(channel);

	if ((status != DMA_STATUS_BLOCK) && (status != DMA_STATUS_COMPLETE)) {
		i2s_syna_sr100_rx_error(data);
		return;
	}

	ret = k_msgq_get(&data->rx_ctx.pending_q, &done, K_NO_WAIT);
	if (ret != 0) {
		i2s_syna_sr100_rx_error(data);
		return;
	}

	(void)sys_cache_data_invd_range(done.mem_block, done.size);
	ret = k_msgq_put(&data->rx_out_q, &done, K_NO_WAIT);
	if (ret != 0) {
		if ((data->rx.cfg.mem_slab != NULL) && (done.mem_block != NULL)) {
			k_mem_slab_free(data->rx.cfg.mem_slab, done.mem_block);
		}
		i2s_syna_sr100_rx_error(data);
		return;
	}

	data->rx_ctx.completed_blocks++;

	if (status == DMA_STATUS_COMPLETE) {
		data->rx_ctx.dma_busy = false;

		if (data->rx.state == I2S_STATE_RUNNING) {
			ret = i2s_syna_sr100_rx_submit_batch(dev);
			if (ret != 0) {
				i2s_syna_sr100_rx_error(data);
				i2s_syna_sr100_apply_hw_state(dev->config, data,
							      (data->tx.state == I2S_STATE_RUNNING) ||
							      (data->tx.state == I2S_STATE_STOPPING),
							      (data->rx.state == I2S_STATE_RUNNING) ||
							      (data->rx.state == I2S_STATE_STOPPING));
			}
		}
	}

}

static int i2s_syna_sr100_tx_submit_batch(const struct device *dev)
{
	const struct i2s_syna_sr100_config *cfg;
	struct sr100_i2s_reg *i2s;
	struct i2s_syna_sr100_data *data;
	struct i2s_syna_sr100_block block;
	uint8_t count = 0U;
	int ret;

	if ((dev == NULL) || (dev->config == NULL) || (dev->data == NULL)) {
		return -EINVAL;
	}

	cfg = dev->config;
	data = dev->data;
	i2s = (void *)cfg->regs.i2s;

	if (!cfg->has_dma_tx || (cfg->dma_tx_dev == NULL) ||
	    !device_is_ready(cfg->dma_tx_dev)) {
		return -ENODEV;
	}

	if (data->tx_ctx.dma_busy) {
		return 0;
	}

	while (count < QUEUE_LEN) {
		ret = k_msgq_get(&data->tx_in_q, &block, K_NO_WAIT);
		if (ret != 0) {
			break;
		}

		memset(&data->tx_ctx.block_cfg[count], 0, sizeof(data->tx_ctx.block_cfg[count]));

		data->tx_ctx.block_cfg[count].source_address =
			(uint32_t)(uintptr_t)block.mem_block;
		data->tx_ctx.block_cfg[count].dest_address =
			(uint32_t)(uintptr_t)&i2s->txfifo;
		data->tx_ctx.block_cfg[count].block_size = block.size;
		data->tx_ctx.block_cfg[count].source_addr_adj = DMA_ADDR_ADJ_INCREMENT;
		data->tx_ctx.block_cfg[count].dest_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;

		(void)sys_cache_data_flush_range(block.mem_block, block.size);

		ret = k_msgq_put(&data->tx_ctx.pending_q, &block, K_NO_WAIT);

		if (ret != 0) {
			if ((data->tx.cfg.mem_slab != NULL) &&
			    (block.mem_block != NULL)) {
				k_mem_slab_free(data->tx.cfg.mem_slab, block.mem_block);
			}

			data->tx.state = I2S_STATE_ERROR;

			return -ENOMEM;
		}

		data->tx_ctx.block_cfg[count].next_block = (count + 1U < QUEUE_LEN) ?
			&data->tx_ctx.block_cfg[count + 1U] : NULL;

		count++;
	}

	if (count == 0U) {
		return -ENODATA;
	}

	data->tx_ctx.block_cfg[count - 1U].next_block = NULL;

	memset(&data->tx_ctx.dma_cfg, 0, sizeof(data->tx_ctx.dma_cfg));

	data->tx_ctx.dma_cfg.channel_direction = MEMORY_TO_PERIPHERAL;
	data->tx_ctx.dma_cfg.source_data_size = 4U;
	data->tx_ctx.dma_cfg.dest_data_size = 4U;
	data->tx_ctx.dma_cfg.source_burst_length = 1U;
	data->tx_ctx.dma_cfg.dest_burst_length = 1U;
	data->tx_ctx.dma_cfg.complete_callback_en = 1U;
	data->tx_ctx.dma_cfg.block_count = count;
	data->tx_ctx.dma_cfg.head_block = &data->tx_ctx.block_cfg[0];
	data->tx_ctx.dma_cfg.user_data = (void *)dev;
	data->tx_ctx.dma_cfg.dma_callback = i2s_syna_sr100_dma_tx_cb;

	ret = dma_config(cfg->dma_tx_dev, cfg->dma_tx_channel, &data->tx_ctx.dma_cfg);

	if (ret != 0) {
		data->tx.state = I2S_STATE_ERROR;
		return ret;
	}

	data->tx_ctx.queued_blocks += count;

	data->tx_ctx.dma_busy = true;

	ret = dma_start(cfg->dma_tx_dev, cfg->dma_tx_channel);

	if (ret != 0) {
		data->tx_ctx.dma_busy = false;
		data->tx.state = I2S_STATE_ERROR;
		return ret;
	}

	return 0;
}

static int i2s_syna_sr100_rx_submit_batch(const struct device *dev)
{
	const struct i2s_syna_sr100_config *cfg;
	struct sr100_i2s_reg *i2s;
	struct i2s_syna_sr100_data *data;
	struct i2s_syna_sr100_block block;
	uint8_t count = 0U;
	int ret;

	if ((dev == NULL) || (dev->config == NULL) || (dev->data == NULL)) {
		return -EINVAL;
	}

	cfg = dev->config;
	data = dev->data;
	i2s = (void *)cfg->regs.i2s;

	if (!cfg->has_dma_rx || (cfg->dma_rx_dev == NULL) ||
	    !device_is_ready(cfg->dma_rx_dev)) {
		return -ENODEV;
	}

	if (data->rx_ctx.dma_busy) {
		return 0;
	}

	while (count < QUEUE_LEN) {
		ret = k_mem_slab_alloc(data->rx.cfg.mem_slab, &block.mem_block, K_NO_WAIT);

		if (ret != 0) {
			break;
		}

		block.size = data->rx.cfg.block_size;

		memset(&data->rx_ctx.block_cfg[count], 0, sizeof(data->rx_ctx.block_cfg[count]));

		data->rx_ctx.block_cfg[count].source_address =
			(uint32_t)(uintptr_t)&i2s->rxfifo;
		data->rx_ctx.block_cfg[count].dest_address =
			(uint32_t)(uintptr_t)block.mem_block;
		data->rx_ctx.block_cfg[count].block_size = block.size;
		data->rx_ctx.block_cfg[count].source_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
		data->rx_ctx.block_cfg[count].dest_addr_adj = DMA_ADDR_ADJ_INCREMENT;

		ret = k_msgq_put(&data->rx_ctx.pending_q, &block, K_NO_WAIT);

		if (ret != 0) {
			if ((data->rx.cfg.mem_slab != NULL) &&
				(block.mem_block != NULL)) {
				k_mem_slab_free(data->rx.cfg.mem_slab, block.mem_block);
			}

			data->rx.state = I2S_STATE_ERROR;

			return -ENOMEM;
		}

		data->rx_ctx.block_cfg[count].next_block = (count + 1U < QUEUE_LEN) ?
			&data->rx_ctx.block_cfg[count + 1U] : NULL;

		count++;
	}

	if (count == 0U) {
		return -ENOMEM;
	}

	data->rx_ctx.block_cfg[count - 1U].next_block = NULL;

	memset(&data->rx_ctx.dma_cfg, 0, sizeof(data->rx_ctx.dma_cfg));

	data->rx_ctx.dma_cfg.channel_direction = PERIPHERAL_TO_MEMORY;
	data->rx_ctx.dma_cfg.source_data_size = 4U;
	data->rx_ctx.dma_cfg.dest_data_size = 4U;
	data->rx_ctx.dma_cfg.source_burst_length = 1U;
	data->rx_ctx.dma_cfg.dest_burst_length = 1U;
	data->rx_ctx.dma_cfg.complete_callback_en = 1U;
	data->rx_ctx.dma_cfg.block_count = count;
	data->rx_ctx.dma_cfg.head_block = &data->rx_ctx.block_cfg[0];
	data->rx_ctx.dma_cfg.user_data = (void *)dev;
	data->rx_ctx.dma_cfg.dma_callback = i2s_syna_sr100_dma_rx_cb;

	ret = dma_config(cfg->dma_rx_dev, cfg->dma_rx_channel, &data->rx_ctx.dma_cfg);

	if (ret != 0) {
		data->rx.state = I2S_STATE_ERROR;
		return ret;
	}

	data->rx_ctx.queued_blocks += count;

	data->rx_ctx.dma_busy = true;

	ret = dma_start(cfg->dma_rx_dev, cfg->dma_rx_channel);

	if (ret != 0) {
		data->rx_ctx.dma_busy = false;
		data->rx.state = I2S_STATE_ERROR;
		return ret;
	}

	return 0;
}

static void i2s_syna_sr100_tx_stop_dma(struct i2s_syna_sr100_data *data)
{
	const struct i2s_syna_sr100_config *cfg = data->dev->config;

	if (data->tx_ctx.dma_busy) {
		(void)dma_stop(cfg->dma_tx_dev, cfg->dma_tx_channel);
	}
	data->tx_ctx.dma_busy = false;
}

static void i2s_syna_sr100_rx_stop_dma(struct i2s_syna_sr100_data *data)
{
	const struct i2s_syna_sr100_config *cfg = data->dev->config;

	if (data->rx_ctx.dma_busy) {
		(void)dma_stop(cfg->dma_rx_dev, cfg->dma_rx_channel);
	}
	data->rx_ctx.dma_busy = false;
}

static void i2s_syna_sr100_tx_cleanup(struct i2s_syna_sr100_data *data)
{
	struct i2s_syna_sr100_block block;

	while (k_msgq_get(&data->tx_in_q, &block, K_NO_WAIT) == 0) {
		if ((data->tx.cfg.mem_slab != NULL) && (block.mem_block != NULL)) {
			k_mem_slab_free(data->tx.cfg.mem_slab, block.mem_block);
		}
	}

	while (k_msgq_get(&data->tx_ctx.pending_q, &block, K_NO_WAIT) == 0) {
		if ((data->tx.cfg.mem_slab != NULL) && (block.mem_block != NULL)) {
			k_mem_slab_free(data->tx.cfg.mem_slab, block.mem_block);
		}
	}

	i2s_syna_sr100_runtime_reset(&data->tx_ctx);
	data->tx.stop_mode = I2S_SR100_STOP_NONE;
}

static void i2s_syna_sr100_rx_cleanup(struct i2s_syna_sr100_data *data)
{
	struct i2s_syna_sr100_block block;

	while (k_msgq_get(&data->rx_ctx.pending_q, &block, K_NO_WAIT) == 0) {
		if ((data->rx.cfg.mem_slab != NULL) && (block.mem_block != NULL)) {
			k_mem_slab_free(data->rx.cfg.mem_slab, block.mem_block);
		}
	}

	while (k_msgq_get(&data->rx_out_q, &block, K_NO_WAIT) == 0) {
		if ((data->rx.cfg.mem_slab != NULL) && (block.mem_block != NULL)) {
			k_mem_slab_free(data->rx.cfg.mem_slab, block.mem_block);
		}
	}

	i2s_syna_sr100_runtime_reset(&data->rx_ctx);
	data->rx.stop_mode = I2S_SR100_STOP_NONE;
}

static int i2s_syna_sr100_configure(const struct device *dev, enum i2s_dir dir,
				    const struct i2s_config *cfg)
{
	struct i2s_syna_sr100_data *data;
	const struct i2s_syna_sr100_config *dev_cfg;
	uint8_t fmt;
	int frame_bytes;
	int ret = 0;

	if ((dev == NULL) || (dev->config == NULL) || (dev->data == NULL)) {
		return -EINVAL;
	}

	data = dev->data;
	dev_cfg = dev->config;

	k_mutex_lock(&data->lock, K_FOREVER);

	if ((cfg != NULL) && (cfg->frame_clk_freq == 0U)) {
		if ((dir == I2S_DIR_TX) || (dir == I2S_DIR_BOTH)) {
			i2s_syna_sr100_tx_stop_dma(data);
			i2s_syna_sr100_tx_cleanup(data);
			memset(&data->tx.cfg, 0, sizeof(data->tx.cfg));
			data->tx.configured = false;
			data->tx.state = I2S_STATE_NOT_READY;
			data->tx.stop_mode = I2S_SR100_STOP_NONE;
		}
		if ((dir == I2S_DIR_RX) || (dir == I2S_DIR_BOTH)) {
			i2s_syna_sr100_rx_stop_dma(data);
			i2s_syna_sr100_rx_cleanup(data);
			memset(&data->rx.cfg, 0, sizeof(data->rx.cfg));
			data->rx.configured = false;
			data->rx.state = I2S_STATE_NOT_READY;
			data->rx.stop_mode = I2S_SR100_STOP_NONE;
		}
		i2s_syna_sr100_apply_hw_state(dev_cfg, data, false, false);
		k_mutex_unlock(&data->lock);
		return 0;
	}

	if (cfg == NULL) {
		ret = -EINVAL;
		goto out;
	}

	if ((cfg->mem_slab == NULL) || (cfg->block_size == 0U) || (cfg->frame_clk_freq == 0U)) {
		ret = -EINVAL;
		goto out;
	}

	if ((cfg->word_size != 8U) && (cfg->word_size != 16U) &&
	    (cfg->word_size != 24U) && (cfg->word_size != 32U)) {
		ret = -EINVAL;
		goto out;
	}

	if ((cfg->channels == 0U) || (cfg->channels > 4U)) {
		ret = -EINVAL;
		goto out;
	}

	fmt = I2S_DATA_FORMAT(cfg);
	if ((fmt != I2S_FMT_DATA_FORMAT_I2S) &&
	    (fmt != I2S_FMT_DATA_FORMAT_LEFT_JUSTIFIED) &&
	    (fmt != I2S_FMT_DATA_FORMAT_RIGHT_JUSTIFIED)) {
		ret = -ENOTSUP;
		goto out;
	}

	frame_bytes = (int)((uint32_t)I2S_EFFECTIVE_CHANNELS(cfg) * I2S_SLOT_BYTES(cfg->word_size));
	if ((frame_bytes <= 0) || ((cfg->block_size % (size_t)frame_bytes) != 0U)) {
		ret = -EINVAL;
		goto out;
	}

	switch (dir) {
	case I2S_DIR_TX:
		if ((data->tx.state != I2S_STATE_NOT_READY) &&
		    (data->tx.state != I2S_STATE_READY)) {
			ret = -EBUSY;
			break;
		}
		i2s_syna_sr100_tx_stop_dma(data);
		i2s_syna_sr100_tx_cleanup(data);
		data->tx.cfg = *cfg;
		data->tx.configured = true;
		data->tx.state = I2S_STATE_READY;
		data->tx.stop_mode = I2S_SR100_STOP_NONE;
		i2s_syna_sr100_hw_config_tx(dev_cfg, cfg);
		break;
	case I2S_DIR_RX:
		if ((data->rx.state != I2S_STATE_NOT_READY) &&
		    (data->rx.state != I2S_STATE_READY)) {
			ret = -EBUSY;
			break;
		}
		i2s_syna_sr100_rx_stop_dma(data);
		i2s_syna_sr100_rx_cleanup(data);
		data->rx.cfg = *cfg;
		data->rx.configured = true;
		data->rx.state = I2S_STATE_READY;
		data->rx.stop_mode = I2S_SR100_STOP_NONE;
		i2s_syna_sr100_hw_config_rx(dev_cfg, cfg);
		break;
	case I2S_DIR_BOTH:
		if (((data->tx.state != I2S_STATE_NOT_READY) &&
		     (data->tx.state != I2S_STATE_READY)) ||
		    ((data->rx.state != I2S_STATE_NOT_READY) &&
		     (data->rx.state != I2S_STATE_READY))) {
			ret = -EBUSY;
			break;
		}
		i2s_syna_sr100_tx_stop_dma(data);
		i2s_syna_sr100_tx_cleanup(data);
		i2s_syna_sr100_rx_stop_dma(data);
		i2s_syna_sr100_rx_cleanup(data);

		data->tx.cfg = *cfg;
		data->tx.configured = true;
		data->tx.state = I2S_STATE_READY;
		data->tx.stop_mode = I2S_SR100_STOP_NONE;
		i2s_syna_sr100_hw_config_tx(dev_cfg, cfg);

		data->rx.cfg = *cfg;
		data->rx.configured = true;
		data->rx.state = I2S_STATE_READY;
		data->rx.stop_mode = I2S_SR100_STOP_NONE;
		i2s_syna_sr100_hw_config_rx(dev_cfg, cfg);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (ret == 0) {
		data->loopback =
			((data->tx.configured && ((data->tx.cfg.options & I2S_OPT_LOOPBACK) != 0U)) ||
			 (data->rx.configured && ((data->rx.cfg.options & I2S_OPT_LOOPBACK) != 0U)));
	}

out:
	k_mutex_unlock(&data->lock);

	return ret;
}

static const struct i2s_config *i2s_syna_sr100_config_get(const struct device *dev,
							  enum i2s_dir dir)
{
	struct i2s_syna_sr100_data *data;

	data = dev->data;

	if (dir == I2S_DIR_TX) {
		return data->tx.configured ? &data->tx.cfg : NULL;
	}

	if (dir == I2S_DIR_RX) {
		return data->rx.configured ? &data->rx.cfg : NULL;
	}

	return NULL;
}

static int i2s_syna_sr100_read(const struct device *dev, void **mem_block, size_t *size)
{
	struct i2s_syna_sr100_data *data;
	struct i2s_syna_sr100_block block;
	int32_t timeout_ms;
	k_timeout_t timeout;
	int ret;

	if ((dev == NULL) || (dev->config == NULL) || (dev->data == NULL)) {
		return -EINVAL;
	}

	if ((mem_block == NULL) || (size == NULL)) {
		return -EINVAL;
	}

	data = dev->data;

	if (!data->rx.configured || (data->rx.state == I2S_STATE_NOT_READY)) {
		return -EIO;
	}

	timeout_ms = data->rx.cfg.timeout;
	if (timeout_ms == 0) {
		timeout = K_NO_WAIT;
	} else if (timeout_ms == SYS_FOREVER_MS) {
		timeout = K_FOREVER;
	} else {
		timeout = K_MSEC(timeout_ms);
	}

	ret = k_msgq_get(&data->rx_out_q, &block, timeout);
	if (ret != 0) {
		if ((data->rx.state == I2S_STATE_ERROR) &&
		    ((ret == -ENOMSG) || (ret == -EAGAIN))) {
			return -EIO;
		}
		if ((ret == -ENOMSG) && (timeout_ms == 0)) {
			return -EBUSY;
		}
		if ((ret == -EAGAIN) || (ret == -ENOMSG)) {
			return -EAGAIN;
		}
		return ret;
	}

	*mem_block = block.mem_block;
	*size = block.size;

	return 0;
}

static int i2s_syna_sr100_write(const struct device *dev, void *mem_block, size_t size)
{
	struct i2s_syna_sr100_data *data;
	struct i2s_syna_sr100_block block;
	int32_t timeout_ms;
	k_timeout_t timeout;
	int ret;

	if ((dev == NULL) || (dev->config == NULL) || (dev->data == NULL)) {
		return -EINVAL;
	}

	if ((mem_block == NULL) || (size == 0U)) {
		return -EINVAL;
	}

	data = dev->data;

	if (!data->tx.configured) {
		return -EIO;
	}

	if ((data->tx.state != I2S_STATE_READY) && (data->tx.state != I2S_STATE_RUNNING)) {
		return -EIO;
	}

	if (size != data->tx.cfg.block_size) {
		return -EINVAL;
	}

	(void)sys_cache_data_flush_range(mem_block, size);
	block.mem_block = mem_block;
	block.size = size;
	timeout_ms = data->tx.cfg.timeout;

	if (timeout_ms == 0) {
		timeout = K_NO_WAIT;
	} else if (timeout_ms == SYS_FOREVER_MS) {
		timeout = K_FOREVER;
	} else {
		timeout = K_MSEC(timeout_ms);
	}

	ret = k_msgq_put(&data->tx_in_q, &block, timeout);
	if (ret != 0) {
		if ((ret == -ENOMSG) && (timeout_ms == 0)) {
			return -EBUSY;
		}
		if ((ret == -EAGAIN) || (ret == -ENOMSG)) {
			return -EAGAIN;
		}
		return ret;
	}

	if (data->tx.state == I2S_STATE_RUNNING) {
		ret = i2s_syna_sr100_tx_submit_batch(dev);
		if ((ret != 0) && (ret != -ENODATA)) {
			data->tx.state = I2S_STATE_ERROR;
			return ret;
		}
	}

	return 0;
}

static int i2s_syna_sr100_start_tx(const struct device *dev)
{
	struct i2s_syna_sr100_data *data;
	int ret;

	if ((dev == NULL) || (dev->config == NULL) || (dev->data == NULL)) {
		return -EINVAL;
	}

	data = dev->data;

	if (!data->tx.configured || (data->tx.state != I2S_STATE_READY)) {
		return -EIO;
	}

	if (k_msgq_num_used_get(&data->tx_in_q) == 0U) {
		return -EIO;
	}

	data->tx.state = I2S_STATE_RUNNING;
	data->tx.stop_mode = I2S_SR100_STOP_NONE;
	i2s_syna_sr100_apply_hw_state(dev->config, data, true,
				      (data->rx.state == I2S_STATE_RUNNING) ||
				      (data->rx.state == I2S_STATE_STOPPING));
	ret = i2s_syna_sr100_tx_submit_batch(dev);
	if (ret != 0) {
		data->tx.state = I2S_STATE_ERROR;
		i2s_syna_sr100_apply_hw_state(dev->config, data,
					      (data->tx.state == I2S_STATE_RUNNING) ||
					      (data->tx.state == I2S_STATE_STOPPING),
					      (data->rx.state == I2S_STATE_RUNNING) ||
					      (data->rx.state == I2S_STATE_STOPPING));
		return ret;
	}

	return 0;
}

static int i2s_syna_sr100_start_rx(const struct device *dev)
{
	struct i2s_syna_sr100_data *data;
	int ret;

	if ((dev == NULL) || (dev->config == NULL) || (dev->data == NULL)) {
		return -EINVAL;
	}

	data = dev->data;

	if (!data->rx.configured || (data->rx.state != I2S_STATE_READY)) {
		return -EIO;
	}

	data->rx.state = I2S_STATE_RUNNING;
	data->rx.stop_mode = I2S_SR100_STOP_NONE;
	i2s_syna_sr100_apply_hw_state(dev->config, data,
				      (data->tx.state == I2S_STATE_RUNNING) ||
				      (data->tx.state == I2S_STATE_STOPPING), true);
	ret = i2s_syna_sr100_rx_submit_batch(dev);
	if (ret != 0) {
		data->rx.state = I2S_STATE_ERROR;
		i2s_syna_sr100_apply_hw_state(dev->config, data,
					      (data->tx.state == I2S_STATE_RUNNING) ||
					      (data->tx.state == I2S_STATE_STOPPING),
					      (data->rx.state == I2S_STATE_RUNNING) ||
					      (data->rx.state == I2S_STATE_STOPPING));
		return ret;
	}

	return 0;
}

static int i2s_syna_sr100_stop_tx(struct i2s_syna_sr100_data *data, bool drain)
{
	if ((data->tx.state != I2S_STATE_RUNNING) && (data->tx.state != I2S_STATE_STOPPING)) {
		return -EIO;
	}

	if (!drain && (data->tx.state == I2S_STATE_STOPPING) &&
	    (data->tx.stop_mode == I2S_SR100_DRAIN)) {
		return 0;
	}

	if (!data->tx_ctx.dma_busy && (k_msgq_num_used_get(&data->tx_in_q) == 0U) &&
	    (k_msgq_num_used_get(&data->tx_ctx.pending_q) == 0U)) {
		i2s_syna_sr100_runtime_reset(&data->tx_ctx);
		data->tx.stop_mode = I2S_SR100_STOP_NONE;
		data->tx.state = I2S_STATE_READY;
		return 0;
	}

	data->tx.state = I2S_STATE_STOPPING;
	data->tx.stop_mode = drain ? I2S_SR100_DRAIN : I2S_SR100_STOP;

	return 0;
}

static int i2s_syna_sr100_stop_rx(struct i2s_syna_sr100_data *data)
{
	if ((data->rx.state != I2S_STATE_RUNNING) && (data->rx.state != I2S_STATE_STOPPING)) {
		return -EIO;
	}

	i2s_syna_sr100_rx_stop_dma(data);
	i2s_syna_sr100_rx_cleanup(data);
	data->rx.state = I2S_STATE_READY;

	return 0;
}

static int i2s_syna_sr100_drop_tx(struct i2s_syna_sr100_data *data)
{
	if (data->tx.state == I2S_STATE_NOT_READY) {
		return -EIO;
	}

	i2s_syna_sr100_tx_stop_dma(data);
	i2s_syna_sr100_tx_cleanup(data);
	data->tx.state = I2S_STATE_READY;

	return 0;
}

static int i2s_syna_sr100_drop_rx(struct i2s_syna_sr100_data *data)
{
	if (data->rx.state == I2S_STATE_NOT_READY) {
		return -EIO;
	}

	i2s_syna_sr100_rx_stop_dma(data);
	i2s_syna_sr100_rx_cleanup(data);
	data->rx.state = I2S_STATE_READY;

	return 0;
}

static int i2s_syna_sr100_prepare_tx(struct i2s_syna_sr100_data *data)
{
	if (data->tx.state != I2S_STATE_ERROR) {
		return -EIO;
	}

	i2s_syna_sr100_tx_stop_dma(data);
	i2s_syna_sr100_tx_cleanup(data);

	data->tx.state = I2S_STATE_READY;
	data->tx.stop_mode = I2S_SR100_STOP_NONE;

	return 0;
}

static int i2s_syna_sr100_prepare_rx(struct i2s_syna_sr100_data *data)
{
	if (data->rx.state != I2S_STATE_ERROR) {
		return -EIO;
	}

	i2s_syna_sr100_rx_stop_dma(data);
	i2s_syna_sr100_rx_cleanup(data);

	data->rx.state = I2S_STATE_READY;
	data->rx.stop_mode = I2S_SR100_STOP_NONE;

	return 0;
}

static int i2s_syna_sr100_trigger(const struct device *dev, enum i2s_dir dir,
				  enum i2s_trigger_cmd cmd)
{
	struct i2s_syna_sr100_data *data;
	bool do_tx = (dir == I2S_DIR_TX) || (dir == I2S_DIR_BOTH);
	bool do_rx = (dir == I2S_DIR_RX) || (dir == I2S_DIR_BOTH);
	int ret = 0;

	if ((dev == NULL) || (dev->config == NULL) || (dev->data == NULL)) {
		return -EINVAL;
	}

	data = dev->data;

	k_mutex_lock(&data->lock, K_FOREVER);

	if (!do_tx && !do_rx) {
		ret = -EINVAL;
		goto out;
	}

	switch (cmd) {
	case I2S_TRIGGER_START:
		if (do_tx) {
			ret = i2s_syna_sr100_start_tx(dev);
		}
		if ((ret == 0) && do_rx) {
			ret = i2s_syna_sr100_start_rx(dev);
		}
		break;
	case I2S_TRIGGER_STOP:
		if (do_tx) {
			ret = i2s_syna_sr100_stop_tx(data, false);
		}
		if ((ret == 0) && do_rx) {
			ret = i2s_syna_sr100_stop_rx(data);
		}
		break;
	case I2S_TRIGGER_DRAIN:
		if (do_tx) {
			ret = i2s_syna_sr100_stop_tx(data, true);
		}
		if ((ret == 0) && do_rx) {
			ret = i2s_syna_sr100_stop_rx(data);
		}
		break;
	case I2S_TRIGGER_DROP:
		if (do_tx) {
			ret = i2s_syna_sr100_drop_tx(data);
		}
		if ((ret == 0) && do_rx) {
			ret = i2s_syna_sr100_drop_rx(data);
		}
		break;
	case I2S_TRIGGER_PREPARE:
		if (do_tx) {
			ret = i2s_syna_sr100_prepare_tx(data);
		}
		if ((ret == 0) && do_rx) {
			ret = i2s_syna_sr100_prepare_rx(data);
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}

out:
	i2s_syna_sr100_apply_hw_state(dev->config, data,
				      (data->tx.state == I2S_STATE_RUNNING) ||
				      (data->tx.state == I2S_STATE_STOPPING),
				      (data->rx.state == I2S_STATE_RUNNING) ||
				      (data->rx.state == I2S_STATE_STOPPING));

	k_mutex_unlock(&data->lock);

	return ret;
}

static DEVICE_API(i2s, i2s_syna_sr100_api) = {
	.configure = i2s_syna_sr100_configure,
	.config_get = i2s_syna_sr100_config_get,
	.read = i2s_syna_sr100_read,
	.write = i2s_syna_sr100_write,
	.trigger = i2s_syna_sr100_trigger,
};

static int i2s_syna_sr100_hw_init(const struct i2s_syna_sr100_config *cfg,
				  const struct i2s_config *initial_cfg)
{
	struct sr100_i2s_reg *i2s = (void *)cfg->regs.i2s;
	int ret;

	if ((cfg->clk_dev == NULL) || !device_is_ready(cfg->clk_dev)) {
		return -ENODEV;
	}

	ret = clock_control_on(cfg->clk_dev, cfg->cfg_clk_subsys);
	if (ret != 0) {
		return ret;
	}

	ret = clock_control_on(cfg->clk_dev, cfg->tx_clk_subsys);
	if (ret != 0) {
		return ret;
	}

	ret = clock_control_on(cfg->clk_dev, cfg->rx_clk_subsys);
	if (ret != 0) {
		return ret;
	}

	LPS_REG(cfg, LPS_AUDIO_CLK_OFFSET) =
		(LPS_REG(cfg, LPS_AUDIO_CLK_OFFSET) & ~LPS_AUDIO_CLK_SRC_MASK) |
		LPS_AUDIO_CLK_SRC_XTAL24M |
		LPS_AUDIO_CLK_ENABLE;
	i2s->pcm3 = 0U;
	i2s->audio_src &= ~AUDIO_SRC_SEL;
	i2s->pcm13 = 0U;
	i2s->pcm8 = 0U;
	i2s->irq_en = 0U;

	if (initial_cfg != NULL) {
		i2s_syna_sr100_program_bclk_div(cfg, initial_cfg);
	}

	i2s_syna_sr100_apply_hw_state(cfg, NULL, false, false);

	return 0;
}

static void i2s_syna_sr100_init_queues(struct i2s_syna_sr100_data *data)
{
	k_msgq_init(&data->tx_in_q, data->tx_in_q_buf, sizeof(struct i2s_syna_sr100_block),
		    QUEUE_LEN);
	k_msgq_init(&data->rx_out_q, data->rx_out_q_buf, sizeof(struct i2s_syna_sr100_block),
		    QUEUE_LEN);
	k_msgq_init(&data->tx_ctx.pending_q, (char *)data->tx_ctx.pending_q_buf,
		    sizeof(struct i2s_syna_sr100_block), QUEUE_LEN);
	k_msgq_init(&data->rx_ctx.pending_q, (char *)data->rx_ctx.pending_q_buf,
		    sizeof(struct i2s_syna_sr100_block), QUEUE_LEN);
}

static int i2s_syna_sr100_init(const struct device *dev)
{
	const struct i2s_syna_sr100_config *cfg;
	struct i2s_syna_sr100_data *data;
	int ret;

	if ((dev == NULL) || (dev->config == NULL) || (dev->data == NULL)) {
		return -EINVAL;
	}

	cfg = dev->config;
	data = dev->data;

	if (!cfg->has_dma_tx || !cfg->has_dma_rx ||
	    (cfg->dma_tx_dev == NULL) || (cfg->dma_rx_dev == NULL)) {
		return -ENODEV;
	}

	if (!device_is_ready(cfg->dma_tx_dev) || !device_is_ready(cfg->dma_rx_dev)) {
		return -ENODEV;
	}

	ret = pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		return ret;
	}

	data->dev = dev;
	data->tx.state = I2S_STATE_NOT_READY;
	data->rx.state = I2S_STATE_NOT_READY;
	data->tx.configured = false;
	data->rx.configured = false;
	data->tx.stop_mode = I2S_SR100_STOP_NONE;
	data->rx.stop_mode = I2S_SR100_STOP_NONE;
	data->loopback = false;
	i2s_syna_sr100_runtime_reset(&data->tx_ctx);
	i2s_syna_sr100_runtime_reset(&data->rx_ctx);
	k_mutex_init(&data->lock);
	i2s_syna_sr100_init_queues(data);

	return i2s_syna_sr100_hw_init(cfg, NULL);
}

#define DMA_TX_DEV_INIT(n)                                                            \
	COND_CODE_1(DT_INST_DMAS_HAS_NAME(n, tx),                                               \
			(DEVICE_DT_GET(DT_INST_DMAS_CTLR_BY_NAME(n, tx))),                            \
			(NULL))

#define DMA_RX_DEV_INIT(n)                                                            \
	COND_CODE_1(DT_INST_DMAS_HAS_NAME(n, rx),                                               \
			(DEVICE_DT_GET(DT_INST_DMAS_CTLR_BY_NAME(n, rx))),                            \
			(NULL))

#define I2S_INIT(n)                                                                        \
	PINCTRL_DT_INST_DEFINE(n);                                                               \
	static const struct i2s_syna_sr100_config i2s_syna_sr100_config_##n = {                  \
		.regs = {                                                                        \
			.i2s = DT_INST_REG_ADDR_BY_NAME(n, i2s),                                 \
			.global = DT_INST_REG_ADDR_BY_NAME(n, global),                           \
			.lps = DT_INST_REG_ADDR_BY_NAME(n, lps),                                 \
		},                                                                              \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                                       \
		.clk_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR_BY_IDX(n, 0)),                      \
		.cfg_clk_subsys = (clock_control_subsys_t)(uintptr_t)DT_INST_CLOCKS_CELL_BY_IDX( \
			n, 0, clkid),                                                            \
		.tx_clk_subsys = (clock_control_subsys_t)(uintptr_t)DT_INST_CLOCKS_CELL_BY_IDX(  \
			n, 1, clkid),                                                            \
		.rx_clk_subsys = (clock_control_subsys_t)(uintptr_t)DT_INST_CLOCKS_CELL_BY_IDX(  \
			n, 2, clkid),                                                            \
		.has_dma_tx = DT_INST_DMAS_HAS_NAME(n, tx),                                      \
		.has_dma_rx = DT_INST_DMAS_HAS_NAME(n, rx),                                      \
		.dma_tx_dev = DMA_TX_DEV_INIT(n),                                      \
		.dma_rx_dev = DMA_RX_DEV_INIT(n),                                      \
		.dma_tx_channel = COND_CODE_1(DT_INST_DMAS_HAS_NAME(n, tx),                      \
					      (DT_INST_DMAS_CELL_BY_NAME(n, tx, channel)), (0)),  \
		.dma_rx_channel = COND_CODE_1(DT_INST_DMAS_HAS_NAME(n, rx),                      \
					      (DT_INST_DMAS_CELL_BY_NAME(n, rx, channel)), (0)),  \
	};                                                                                       \
	static struct i2s_syna_sr100_data i2s_syna_sr100_data_##n;                               \
	DEVICE_DT_INST_DEFINE(n, i2s_syna_sr100_init, NULL, &i2s_syna_sr100_data_##n,            \
			      &i2s_syna_sr100_config_##n, POST_KERNEL,                           \
			      INIT_PRIORITY, &i2s_syna_sr100_api);

DT_INST_FOREACH_STATUS_OKAY(I2S_INIT)
