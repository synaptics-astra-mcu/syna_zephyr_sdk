/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/device.h>
#include <zephyr/dt-bindings/memory-attr/memory-attr-arm.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/dma/dma_arm350.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/sys/math_extras.h>
#include <zephyr/irq.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(dma_arm_350, CONFIG_DMA_LOG_LEVEL);

#define DT_DRV_COMPAT arm_dma_350

#define DMA_CHANNEL_CHANNEL_BLOCK_SIZE (0x100) /* Size of all registers in a channel */

#define DMA_MAX_PORTS 32
#define DMA_INVALID_PORT 0xff

#define DMA_CH_OFFS(cfg, ch_num) (cfg->regs + (DMA_CHANNEL_CHANNEL_BLOCK_SIZE * ch_num))
#define DMA_CH_MNG(data, ch_num) (data->attr[ch_num])

#define DMA_SECURE_MODE (!ARM_NONSECURE_FIRMWARE && !ARMV8_A_NS)

#define DMA_HAS_2D	BIT(0)
#define DMA_HAS_AUTO	BIT(1)
#define DMA_HAS_TRIG_IN	BIT(2)
#define DMA_HAS_ERR_CB	BIT(3)

struct ch_attr {
	uint8_t flags;
	uint8_t port;
	dma_callback_t dma_callback;
	void *cb_data;
};

struct arm350_dma_cfg {
	mem_addr_t regs;
	mem_addr_t cntxbase;
	void (*irq_configure)(void);
	uint32_t nonsecure_triggers;
#if defined(CONFIG_CLOCK_CONTROL)
	const struct device *clock_dev;
	clock_control_subsys_t clock_subsys;
#endif /* CONFIG_CLOCK_CONTROL */
#if defined(CONFIG_RESET)
	const struct reset_dt_spec reset;
#endif /* CONFIG_RESET */
};

struct arm350_dma_data {
	bool is_dma250;
	uint32_t perif_channel;
	uint32_t channels;
	struct ch_attr attr[DMA_MAX_PORTS];
};

enum dma_ch_interrupt_t
{
	DMA_CH_INTREN_DONE = BIT(0),
	DMA_CH_INTREN_ERR = BIT(1),
	DMA_CH_INTREN_DISABLED = BIT(2),
	DMA_CH_INTREN_STOPPED = BIT(3),
	DMA_CH_INTREN_SRCTRIGINWAIT = BIT(8),
	DMA_CH_INTREN_DESTRIGINWAIT = BIT(9),
	DMA_CH_INTREN_TRIGOUTACKWAIT = BIT(10),

	DMA_CH_STAT_ALL_MASK = DMA_CH_INTREN_DONE | DMA_CH_INTREN_ERR | DMA_CH_INTREN_DISABLED |
			       DMA_CH_INTREN_STOPPED | DMA_CH_INTREN_SRCTRIGINWAIT |
			       DMA_CH_INTREN_DESTRIGINWAIT | DMA_CH_INTREN_TRIGOUTACKWAIT
};

typedef struct dma_trigger_in_transfer
{
	uint8_t sel;
	uint8_t type;		/* Source/dest trigger input type: 0 = SW, 2 = HW Trigger */
	uint8_t mode;
	uint8_t is_source;	/* is_source = 1 Source trigger in, = 0 Destination trigger in */
} dma_trigger_in_transfer_t;

#define DMA350_SCFG_TRIGINSEC0	0x0008
#define DMA350_SEC_CNTXBASE	0x0110
#define DMA350_BUILDCFG0	0x0fb0
#define DMA350_IIDR		0x0fc8
#define DMA350_CH_CMD		0x1000
#define DMA350_CH_STATUS	0x1004
#define DMA350_CH_INTREN	0x1008
#define DMA350_CH_CTRL		0x100c
#define DMA350_CH_SRCADDR	0x1010
#define DMA350_CH_SRCADDRHI	0x1014
#define DMA350_CH_DESADDR	0x1018
#define DMA350_CH_DESADDRHI	0x101c
#define DMA350_CH_XSIZE		0x1020
#define DMA350_CH_XSIZEHI	0x1024
#define DMA350_CH_SRCTRANSCFG	0x1028
#define DMA350_CH_DESTRANSCFG	0x102c
#define DMA350_CH_XADDRINC	0x1030
#define DMA350_CH_YADDRSTRIDE	0x1034
#define DMA350_CH_FILLVAL	0x1038
#define DMA350_CH_YSIZE		0x103c
#define DMA350_CH_SRCTRIGINCFG	0x104c
#define DMA350_CH_DESTRIGINCFG	0x1050
#define DMA350_CH_AUTOCFG	0x1074
#define DMA350_CH_BUILDCFG1	0x10fc

#define DMA350_CH_CTRL_RESET	0x200200

#define RELOAD_SOURCE		(BIT(18) | BIT(19))
#define RELOAD_DEST		(BIT(18) | BIT(20))

#define SRC_POS			0
#define DES_POS			16

#define SEC_CFG_LCK		BIT(31)

#define NONSECATTR		BIT(10)
#define MAXBURSTLEN_POS		16

#define CH_CMD_ENABLECMD	BIT(0)
#define CH_CMD_STOPCMD		BIT(3)

#define CH_STATUS_DONE		BIT(16)
#define CH_STATUS_ERR		BIT(17)
#define CH_STATUS_DISABLED	BIT(18)
#define CH_STATUS_STOPPED	BIT(19)
#define CH_STATUS_CLEAR_ALL	(CH_STATUS_DONE | CH_STATUS_ERR | CH_STATUS_DISABLED | \
				 CH_STATUS_STOPPED)

#define CMDRESTARTINFEN		BIT(16)

#define NUM_CHANNELS		GENMASK(9, 4)

#define TRANSIZE_POS		0
#define CHPRIO_POS		4
#define XTYPE_POS		9
#define YTYPE_POS		12

#define TRIGINSEL_POS		0
#define TRIGINTYPE_POS		8
#define TRIGINMODE_POS		10
#define TRIGINBLKSIZE_POS	16

#define USESRCTRIGIN		BIT(25)
#define USEDESTRIGIN		BIT(26)

static int arm350_dma_channel_config_trigger_in(const struct device *dev, int ch,
						dma_trigger_in_transfer_t *trig_in)
{
	const struct arm350_dma_cfg *cfg = dev->config;
	uint32_t reg;

	reg  = trig_in->sel << TRIGINSEL_POS;
	reg |= trig_in->type << TRIGINTYPE_POS;
	reg |= trig_in->mode << TRIGINMODE_POS;

	if (trig_in->is_source) {
		sys_write32(reg, DMA_CH_OFFS(cfg, ch) + DMA350_CH_SRCTRIGINCFG);
		sys_write32(0, DMA_CH_OFFS(cfg, ch) + DMA350_CH_DESTRIGINCFG);

		reg = sys_read32(DMA_CH_OFFS(cfg, ch) + DMA350_CH_CTRL);
		reg |= USESRCTRIGIN;
		reg &= ~USEDESTRIGIN;
		sys_write32(reg, DMA_CH_OFFS(cfg, ch) + DMA350_CH_CTRL);
	} else {
		sys_write32(reg, DMA_CH_OFFS(cfg, ch) + DMA350_CH_DESTRIGINCFG);
		sys_write32(0, DMA_CH_OFFS(cfg, ch) + DMA350_CH_SRCTRIGINCFG);

		reg = sys_read32(DMA_CH_OFFS(cfg, ch) + DMA350_CH_CTRL);
		reg |= USEDESTRIGIN;
		reg &= ~USESRCTRIGIN;
		sys_write32(reg, DMA_CH_OFFS(cfg, ch) + DMA350_CH_CTRL);
	}

	return 0;
}

static void arm350_dma_reset_channel(const struct device *dev, int ch)
{
	const struct arm350_dma_cfg *cfg = dev->config;
	uint32_t reg;

	/* Remove trigger configuration; disable interrupts */
	reg = sys_read32(DMA_CH_OFFS(cfg, ch) + DMA350_CH_INTREN);
	reg &= (DMA_CH_INTREN_SRCTRIGINWAIT | DMA_CH_INTREN_DESTRIGINWAIT);
	sys_write32(reg, DMA_CH_OFFS(cfg, ch) + DMA350_CH_INTREN);

	sys_write32(0, DMA_CH_OFFS(cfg, ch) + DMA350_CH_SRCTRIGINCFG);
	sys_write32(0, DMA_CH_OFFS(cfg, ch) + DMA350_CH_DESTRIGINCFG);

	/* Drop any stale latched status before reusing the channel. */
	sys_write32(CH_STATUS_CLEAR_ALL, DMA_CH_OFFS(cfg, ch) + DMA350_CH_STATUS);

	sys_write32(DMA350_CH_CTRL_RESET, DMA_CH_OFFS(cfg, ch) + DMA350_CH_CTRL);
}

static int arm350_dma_get_port(const struct arm350_dma_cfg *config, struct arm350_dma_data *data,
			       uint32_t ch)
{
	if (data->is_dma250) {
		/* One-to-one mapping between channels & trigger ports */
		return ch;
	}

	if (ch >= data->channels) {
		return -ENODEV;
	}

	return data->attr[ch].port;
}

static int arm350_dma_get_channel(const struct arm350_dma_cfg *config,
				  struct arm350_dma_data *data, uint32_t ch)
{
	int i;

	if (data->is_dma250) {
		/* One-to-one mapping between channels & trigger ports */
		return ch;
	}

	for (i = 0; i < data->channels; i++) {
		if (data->attr[i].port == ch) {
			return i;
		}
	}

	return -ENODEV;
}

static int arm350_dma_request_channel(const struct arm350_dma_cfg *config,
				      struct arm350_dma_data *data, uint32_t ch, bool perif)
{
	uint32_t start = 0;
	int i;

	if (perif) {
		start = data->perif_channel;
	}

	if (data->is_dma250) {
		/* One-to-one mapping between channels & trigger ports */
		return ch;
	}

	for (i = start; i < data->channels; i++) {
		if (data->attr[i].port == ch) {
			return i;
		}
	}

	for (i = start; i < data->channels; i++) {
		if (data->attr[i].port == DMA_INVALID_PORT) {
			data->attr[i].port = ch;
			return i;
		}
	}

	return -EINVAL;
}

static void arm350_dma_free_channel(const struct arm350_dma_cfg *config,
				    struct arm350_dma_data *data, uint32_t ch)
{
	int i;

	if (data->is_dma250) {
		/* One-to-one mapping between channels & ports */
		return;
	}

	for (i = 0; i < data->channels; i++) {
		if (data->attr[i].port == ch) {
			data->attr[i].port = DMA_INVALID_PORT;
			break;
		}
	}
}

static void arm350_channel_int_callback(const struct device *dev, int ch, uint32_t status)
{
	const struct arm350_dma_cfg *cfg = dev->config;
	struct arm350_dma_data *data = dev->data;
	int err = 0;
	bool notify_client = false;

	if (status & CH_STATUS_DONE) {
		sys_write32(CH_STATUS_DONE, DMA_CH_OFFS(cfg, ch) + DMA350_CH_STATUS);
		notify_client = true;
	}

	if (status & CH_STATUS_ERR) {
		sys_write32(CH_STATUS_ERR, DMA_CH_OFFS(cfg, ch) + DMA350_CH_STATUS);
		err = -EIO;
		notify_client = true;
	}

	if (status & CH_STATUS_STOPPED) {
		sys_write32(CH_STATUS_STOPPED, DMA_CH_OFFS(cfg, ch) + DMA350_CH_STATUS);
	}

	if (notify_client && data->attr[ch].dma_callback) {
		int port = arm350_dma_get_port(cfg, data, ch);

		if ((port >= 0) && (port != DMA_INVALID_PORT)) {
			data->attr[ch].dma_callback(dev, data->attr[ch].cb_data, port, err);
		}
	}
}

static int arm350_dma_config(const struct device *dev, uint32_t channel, struct dma_config *config)
{
	const struct arm350_dma_cfg *cfg = dev->config;
	struct arm350_dma_data *data = dev->data;
	dma_trigger_in_transfer_t trig_in = { 0 };
	uint16_t src_x_size;
	uint16_t dst_x_size;
	uint16_t src_x_addr_inc;
	uint16_t dst_x_addr_inc;
	uint16_t src_y_addr_stride;
	uint16_t dst_y_addr_stride;
	uint8_t src_mpu_attr = 0xff; /* Normal memory */
	uint8_t dst_mpu_attr = 0xff; /* Normal memory */
	uint32_t transfer_size;
	uint32_t reg;
	int ch;

	if (config->head_block->dest_scatter_en || config->head_block->source_gather_en) {
		/* Not yet supported */
		return -ENOTSUP;
	}

	ch = arm350_dma_request_channel(cfg, data, channel,
					config->channel_direction != MEMORY_TO_MEMORY);
	if (ch < 0) {
		return ch;
	}

	if (config->channel_direction == MEMORY_TO_PERIPHERAL) {
		config->head_block->dest_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
		trig_in.is_source = 0;
		dst_mpu_attr = 0; /* Device memory */
	} else if (config->channel_direction == PERIPHERAL_TO_MEMORY) {
		config->head_block->source_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
		trig_in.is_source = 1;
		src_mpu_attr = 0; /* Device memory */
	}

	switch (config->head_block->source_addr_adj) {
	case DMA_ADDR_ADJ_INCREMENT:
		src_x_addr_inc = 1;
		break;
	case DMA_ADDR_ADJ_DECREMENT:
		src_x_addr_inc = -1;
		break;
	default:
		src_x_addr_inc = 0;
		break;
	}

	switch (config->head_block->dest_addr_adj) {
	case DMA_ADDR_ADJ_INCREMENT:
		dst_x_addr_inc = 1;
		break;
	case DMA_ADDR_ADJ_DECREMENT:
		dst_x_addr_inc = -1;
		break;
	default:
		dst_x_addr_inc = 0;
		break;
	}

	arm350_dma_reset_channel(dev, ch);

	data->attr[ch].flags &= ~DMA_HAS_ERR_CB;
	if (!config->error_callback_dis) {
		data->attr[ch].flags |= DMA_HAS_ERR_CB;
	}
	data->attr[ch].dma_callback = config->dma_callback;
	data->attr[ch].cb_data = config->user_data;

	reg = sys_read32(DMA_CH_OFFS(cfg, ch) + DMA350_CH_SRCTRANSCFG);
	reg &= ~(0x3ff | NONSECATTR | (0xf << MAXBURSTLEN_POS));
	reg |= src_mpu_attr;
#if !defined(DMA_SECURE_MODE)
	reg |= NONSECATTR;
#endif
	reg |= (config->source_burst_length - 1) << MAXBURSTLEN_POS;
	sys_write32(reg, DMA_CH_OFFS(cfg, ch) + DMA350_CH_SRCTRANSCFG);

	sys_write32(config->head_block->source_address, DMA_CH_OFFS(cfg, ch) + DMA350_CH_SRCADDR);
#ifdef CONFIG_DMA_64BIT
	sys_write32(config->head_block->source_address >> 32,
		    DMA_CH_OFFS(cfg, ch) + DMA350_CH_SRCADDRHI);
#endif

	reg = sys_read32(DMA_CH_OFFS(cfg, ch) + DMA350_CH_DESTRANSCFG);
	reg &= ~(0x3ff | NONSECATTR | (0xf << MAXBURSTLEN_POS));
	reg |= dst_mpu_attr;
#if !defined(DMA_SECURE_MODE)
	reg |= NONSECATTR;
#endif
	reg |= (config->dest_burst_length - 1) << MAXBURSTLEN_POS;
	sys_write32(reg, DMA_CH_OFFS(cfg, ch) + DMA350_CH_DESTRANSCFG);

	sys_write32(config->head_block->dest_address, DMA_CH_OFFS(cfg, ch) + DMA350_CH_DESADDR);
#ifdef CONFIG_DMA_64BIT
	sys_write32(config->head_block->dest_address >> 32,
		    DMA_CH_OFFS(cfg, ch) + DMA350_CH_DESADDRHI);
#endif

	transfer_size = config->head_block->block_size;
	reg = sys_read32(DMA_CH_OFFS(cfg, ch) + DMA350_CH_CTRL);
	reg &= ~(7 << TRANSIZE_POS); /* transfer entity size in bytes = 2^0 */
	reg &= ~(USESRCTRIGIN | USEDESTRIGIN);

	if (config->source_data_size <= 128) {
		reg |= u32_count_trailing_zeros(config->source_data_size) << TRANSIZE_POS;
		transfer_size /= config->source_data_size;
	}

	reg &= ~(0x7 << XTYPE_POS);
	reg |=     1 << XTYPE_POS; /* continuous (1D) copy */
	reg &= ~(0x7 << YTYPE_POS); /* disable 2D */
	reg &= ~(0xf << CHPRIO_POS);
	reg |= config->channel_priority << CHPRIO_POS;
	if (data->attr[ch].flags & DMA_HAS_AUTO) {
		if (config->head_block->source_reload_en) {
			reg |= RELOAD_SOURCE;
		}
		if (config->head_block->dest_reload_en) {
			reg |= RELOAD_DEST;
		}
	}
	sys_write32(reg, DMA_CH_OFFS(cfg, ch) + DMA350_CH_CTRL);

	if ((data->attr[ch].flags & DMA_HAS_AUTO) &&
	    (config->head_block->dest_reload_en || config->head_block->source_reload_en)) {
		sys_write32(CMDRESTARTINFEN, DMA_CH_OFFS(cfg, ch) + DMA350_CH_AUTOCFG);
	}

	sys_write32(0, DMA_CH_OFFS(cfg, ch) + DMA350_CH_FILLVAL);

	src_x_size = dst_x_size = transfer_size; /* Assume same size for src & dest */

	if ((data->attr[ch].flags & DMA_HAS_2D) && config->dma_slot &&
	    (config->channel_direction == MEMORY_TO_MEMORY)) {
		src_y_addr_stride = dst_y_addr_stride = 1;
		sys_write32(config->head_block->source_address + (src_x_size - 1),
			    DMA_CH_OFFS(cfg, ch) + DMA350_CH_SRCADDR);
		src_x_addr_inc = -1;
		src_y_addr_stride = src_x_size;

		switch (config->dma_slot) {
		case DMA_ARM350_ROTATE_90:
			dst_x_addr_inc = -src_x_size;
			dst_y_addr_stride = -1;
			sys_write32(config->head_block->dest_address + ((src_x_size * src_x_size) - 1),
				    DMA_CH_OFFS(cfg, ch) + DMA350_CH_DESADDR);
			break;
		case DMA_ARM350_ROTATE_180:
			dst_x_addr_inc = 1;
			dst_y_addr_stride = -src_x_size;
			sys_write32(config->head_block->dest_address + (src_x_size * (src_x_size - 1)),
				    DMA_CH_OFFS(cfg, ch) + DMA350_CH_DESADDR);
			break;
		case DMA_ARM350_ROTATE_270:
			dst_x_addr_inc = src_x_size;
			dst_y_addr_stride = 1;
			break;
		case DMA_ARM350_FLIP_HORIZONTAL:
			dst_x_addr_inc = 1;
			dst_y_addr_stride = src_x_size;
			break;
		case DMA_ARM350_FLIP_VERTICAL:
			dst_x_addr_inc = -1;
			dst_y_addr_stride = -src_x_size;
			sys_write32(config->head_block->dest_address + ((src_x_size * src_x_size) - 1),
				    DMA_CH_OFFS(cfg, ch) + DMA350_CH_DESADDR);
			break;
		case DMA_ARM350_FLIP_DIAG:
			dst_x_addr_inc = -src_x_size;
			dst_y_addr_stride = 1;
			sys_write32(config->head_block->dest_address + (src_x_size * (src_x_size - 1)),
				    DMA_CH_OFFS(cfg, ch) + DMA350_CH_DESADDR);
			break;
		case DMA_ARM350_FLIP_DIAG_ANTI:
			dst_x_addr_inc = src_x_size;
			dst_y_addr_stride = -1;
			sys_write32(config->head_block->dest_address + (src_x_size - 1),
				    DMA_CH_OFFS(cfg, ch) + DMA350_CH_DESADDR);
			break;
		default:
			return -EINVAL;
		}

		reg = (src_y_addr_stride << SRC_POS) | (dst_y_addr_stride << DES_POS);
		sys_write32(reg, DMA_CH_OFFS(cfg, ch) + DMA350_CH_YADDRSTRIDE);

		reg = (src_x_size << SRC_POS) | (dst_x_size << DES_POS);
		sys_write32(reg, DMA_CH_OFFS(cfg, ch) + DMA350_CH_YSIZE);

		reg = sys_read32(DMA_CH_OFFS(cfg, ch) + DMA350_CH_CTRL);
		reg |= 1 << YTYPE_POS; /* 2D: continue */
		sys_write32(reg, DMA_CH_OFFS(cfg, ch) + DMA350_CH_CTRL);
	}

	reg = (src_x_size << SRC_POS) | (dst_x_size << DES_POS);
	sys_write32(reg, DMA_CH_OFFS(cfg, ch) + DMA350_CH_XSIZE);

	reg = ((src_x_size >> 16) << SRC_POS) | ((dst_x_size >> 16) << DES_POS);
	sys_write32(reg, DMA_CH_OFFS(cfg, ch) + DMA350_CH_XSIZEHI);

	reg = (src_x_addr_inc << SRC_POS) | (dst_x_addr_inc << DES_POS);
	sys_write32(reg, DMA_CH_OFFS(cfg, ch) + DMA350_CH_XADDRINC);

	if ((data->attr[ch].flags & DMA_HAS_TRIG_IN) &&
	    config->channel_direction != MEMORY_TO_MEMORY) {
		trig_in.sel = channel;
		trig_in.type = 2; /* hardware */
		trig_in.mode = 3; /* per flow */

		return arm350_dma_channel_config_trigger_in(dev, ch, &trig_in);
	}

	return 0;
}

#ifdef CONFIG_DMA_64BIT
static int arm350_dma_reload(const struct device *dev, uint32_t channel, uint64_t src, uint64_t dst,
			     size_t size)
#else
static int arm350_dma_reload(const struct device *dev, uint32_t channel, uint32_t src, uint32_t dst,
			     size_t size)
#endif
{
	const struct arm350_dma_cfg *cfg = dev->config;
	struct arm350_dma_data *data = dev->data;
	uint32_t reg;
	int ch;

	ch = arm350_dma_get_channel(cfg, data, channel);
	if (ch < 0) {
		return ch;
	}

	sys_write32(CH_STATUS_CLEAR_ALL, DMA_CH_OFFS(cfg, ch) + DMA350_CH_STATUS);

	sys_write32(src, DMA_CH_OFFS(cfg, ch) + DMA350_CH_SRCADDR);
	sys_write32(dst, DMA_CH_OFFS(cfg, ch) + DMA350_CH_DESADDR);
#ifdef CONFIG_DMA_64BIT
	sys_write32(src >> 32, DMA_CH_OFFS(cfg, ch) + DMA350_CH_SRCADDRHI);
	sys_write32(dst >> 32, DMA_CH_OFFS(cfg, ch) + DMA350_CH_DESADDRHI);
#endif

	reg = (size << SRC_POS) | (size << DES_POS); /* Assume same size for src & dest */
	sys_write32(reg, DMA_CH_OFFS(cfg, ch) + DMA350_CH_XSIZE);
	reg = ((size >> 16) << SRC_POS) | ((size >> 16) << DES_POS);
	sys_write32(reg, DMA_CH_OFFS(cfg, ch) + DMA350_CH_XSIZEHI);

	return 0;
}

static int arm350_dma_start(const struct device *dev, uint32_t channel)
{
	const struct arm350_dma_cfg *cfg = dev->config;
	struct arm350_dma_data *data = dev->data;
	uint32_t reg;
	int ch;

	ch = arm350_dma_get_channel(cfg, data, channel);
	if (ch < 0) {
		return ch;
	}

	reg = sys_read32(DMA_CH_OFFS(cfg, ch) + DMA350_CH_INTREN);
	reg |= DMA_CH_INTREN_DONE;
	if (data->attr[ch].flags & DMA_HAS_ERR_CB) {
		reg |= DMA_CH_INTREN_ERR;
	}
	sys_write32(reg, DMA_CH_OFFS(cfg, ch) + DMA350_CH_INTREN);

	reg = sys_read32(DMA_CH_OFFS(cfg, ch) + DMA350_CH_CMD);
	reg |= CH_CMD_ENABLECMD;
	sys_write32(reg, DMA_CH_OFFS(cfg, ch) + DMA350_CH_CMD);

	if ((sys_read32(DMA_CH_OFFS(cfg, ch) + DMA350_CH_STATUS) & CH_STATUS_ERR) != 0) {
		LOG_ERR("status error 0x%x", sys_read32(DMA_CH_OFFS(cfg, ch) + DMA350_CH_STATUS));
		return -EINVAL;
	}

	return 0;
}

static int arm350_dma_stop(const struct device *dev, uint32_t channel)
{
	const struct arm350_dma_cfg *cfg = dev->config;
	struct arm350_dma_data *data = dev->data;
	uint32_t reg;
	int ch;

	ch = arm350_dma_get_channel(cfg, data, channel);
	if (ch < 0) {
		return ch;
	}

	reg = sys_read32(DMA_CH_OFFS(cfg, ch) + DMA350_CH_INTREN);
	reg &= ~(DMA_CH_INTREN_DONE | DMA_CH_INTREN_ERR);
	sys_write32(reg, DMA_CH_OFFS(cfg, ch) + DMA350_CH_INTREN);

	reg = sys_read32(DMA_CH_OFFS(cfg, ch) + DMA350_CH_CMD);
	reg |= CH_CMD_STOPCMD;

	/*
	 * Detach client callback and channel mapping before issuing STOP so that
	 * any late IRQ from the previous transfer cannot be delivered upstream.
	 */
	data->attr[ch].dma_callback = NULL;
	data->attr[ch].cb_data = NULL;
	arm350_dma_free_channel(cfg, data, channel);

	sys_write32(reg, DMA_CH_OFFS(cfg, ch) + DMA350_CH_CMD);

	if ((sys_read32(DMA_CH_OFFS(cfg, ch) + DMA350_CH_STATUS) & CH_STATUS_ERR) != 0) {
		LOG_ERR("status error 0x%x", sys_read32(DMA_CH_OFFS(cfg, ch) + DMA350_CH_STATUS));
		return -EINVAL;
	}

	return 0;
}

static int arm350_dma_get_status(const struct device *dev, uint32_t channel,
				 struct dma_status *stat)
{
	const struct arm350_dma_cfg *cfg = dev->config;
	struct arm350_dma_data *data = dev->data;
	int src_x_addr_inc, dst_x_addr_inc;
	uint32_t reg;
	int ch;

	ch = arm350_dma_get_channel(cfg, data, channel);
	if (ch < 0) {
		return ch;
	}

	if (sys_read32(DMA_CH_OFFS(cfg, ch) + DMA350_CH_CMD) & CH_CMD_ENABLECMD) {
		stat->busy = 1;
	}

	reg = sys_read32(DMA_CH_OFFS(cfg, ch) + DMA350_CH_XADDRINC);
	src_x_addr_inc = reg & (1 << SRC_POS);
	dst_x_addr_inc = reg & (1 << DES_POS);

	if (src_x_addr_inc && dst_x_addr_inc) {
		stat->dir = MEMORY_TO_MEMORY;
	} else if (src_x_addr_inc) {
		stat->dir = MEMORY_TO_PERIPHERAL;
	} else if (dst_x_addr_inc) {
		stat->dir = PERIPHERAL_TO_MEMORY;
	} else {
		stat->dir = PERIPHERAL_TO_PERIPHERAL;
	}

	stat->pending_length = (sys_read32(DMA_CH_OFFS(cfg, ch) + DMA350_CH_XSIZE)) & 0xffff;

	return 0;
}

static void arm350_dma_isr(const struct device *dev)
{
	const struct arm350_dma_cfg *cfg = dev->config;
	struct arm350_dma_data *data = dev->data;
	uint32_t status;
	int ch;

	for (ch = 0; ch < data->channels; ch++) {
		status = sys_read32(DMA_CH_OFFS(cfg, ch) + DMA350_CH_STATUS);

		if (status & DMA_CH_STAT_ALL_MASK) {
			arm350_channel_int_callback(dev, ch, status);
		}
	}
}

static int arm350_dma_init(const struct device *dev)
{
	const struct arm350_dma_cfg *cfg = dev->config;
	struct arm350_dma_data *data = dev->data;
	int ch;
	int ret = 0;

#if defined(CONFIG_CLOCK_CONTROL)
	if (device_is_ready(cfg->clock_dev)) {
		ret = clock_control_on(cfg->clock_dev, cfg->clock_subsys);
		if (ret != 0 && ret != -EALREADY && ret != -ENOSYS) {
			LOG_ERR("failed to enable clock");
			return ret;
		}
	}
#endif

#if defined(CONFIG_RESET)
	if (device_is_ready(cfg->reset.dev)) {
		ret = reset_line_deassert_dt(&cfg->reset);
		if (ret < 0) {
			LOG_ERR("Failed to de-assert reset");
			return ret;
		}
	}
#endif

	cfg->irq_configure();

	data->channels = ((sys_read32(cfg->regs + DMA350_BUILDCFG0) >> 4) & 0x3f) + 1;
	for (ch = 0; ch < data->channels; ch++) {
		uint32_t buildcfg1 = sys_read32(DMA_CH_OFFS(cfg, ch) + DMA350_CH_BUILDCFG1);

		data->attr[ch].flags = 0;
		if (buildcfg1 & BIT(2)) {
			data->attr[ch].flags |= DMA_HAS_2D;
		}
		if (buildcfg1 & BIT(5)) {
			data->attr[ch].flags |= DMA_HAS_TRIG_IN;
		}
		if (buildcfg1 & BIT(9)) {
			data->attr[ch].flags |= DMA_HAS_AUTO;
		}
		data->attr[ch].port = DMA_INVALID_PORT;
	}

	if ((sys_read32(cfg->regs + DMA350_IIDR) >> 20) == 0x250) {
		data->is_dma250 = 1;
	}

#ifdef DMA_SECURE_MODE
	/* Default: all secured */
	sys_write32(cfg->nonsecure_triggers, cfg->regs + DMA350_SCFG_TRIGINSEC0);

	if (data->is_dma250) {
		sys_write32(cfg->cntxbase, cfg->regs + DMA350_SEC_CNTXBASE);
	}
#endif

	return ret;
}

static const struct dma_driver_api arm350_dma_driver_api = {
	.config = arm350_dma_config,
	.reload = arm350_dma_reload,
	.start = arm350_dma_start,
	.stop = arm350_dma_stop,
	.get_status = arm350_dma_get_status,
};

#define ARM350_DMA_IRQ_CONNECT(n, inst)                                                        \
	IRQ_CONNECT(DT_INST_IRQ_BY_IDX(inst, n, irq), DT_INST_IRQ_BY_IDX(inst, n, priority),   \
		    arm350_dma_isr, DEVICE_DT_INST_GET(inst), 0);                              \
	irq_enable(DT_INST_IRQ_BY_IDX(inst, n, irq));

#define CONFIGURE_ALL_IRQS(inst, n) LISTIFY(n, ARM350_DMA_IRQ_CONNECT, (), inst)

#if defined(CONFIG_CLOCK_CONTROL)
#define ARM350_DMA_CLOCK_CONFIG(n)                                                             \
	IF_ENABLED(DT_INST_NODE_HAS_PROP(n, clocks),                                           \
		   (.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(n)),                        \
		    .clock_subsys = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(n, clkid),))
#else
#define ARM350_DMA_CLOCK_CONFIG(n)
#endif

#if defined(CONFIG_RESET)
#define ARM350_DMA_RESET_CONFIG(n)                                                             \
	IF_ENABLED(DT_INST_NODE_HAS_PROP(n, resets),                                           \
		   (.reset = RESET_DT_SPEC_INST_GET(n),))
#else
#define ARM350_DMA_RESET_CONFIG(n)
#endif

#define ARM350_DMA_INIT(inst)                                                                  \
	static void arm350_dma##inst##_irq_configure(void)                                     \
	{                                                                                      \
		CONFIGURE_ALL_IRQS(inst, DT_NUM_IRQS(DT_DRV_INST(inst)));                      \
	}                                                                                      \
	static const struct arm350_dma_cfg arm350_dma##inst##_cfg = {                          \
		.regs = (mem_addr_t)DT_INST_REG_ADDR(inst),                                    \
		.cntxbase = (mem_addr_t)DT_INST_PROP_OR(inst, cntxbase, 0),                    \
		.nonsecure_triggers = DT_INST_PROP_OR(inst, nonsecure_triggers, 0),            \
		.irq_configure = arm350_dma##inst##_irq_configure,                             \
		ARM350_DMA_CLOCK_CONFIG(inst)                                                  \
		ARM350_DMA_RESET_CONFIG(inst)                                                  \
	};                                                                                     \
	static struct arm350_dma_data arm350_dma##inst##_data = {                              \
		.is_dma250 = 0,                                                                \
		.perif_channel = DT_INST_PROP_OR(inst, perif_channel, 0),                      \
	};                                                                                     \
	DEVICE_DT_INST_DEFINE(inst, &arm350_dma_init, NULL, &arm350_dma##inst##_data,          \
			      &arm350_dma##inst##_cfg, PRE_KERNEL_1, CONFIG_DMA_INIT_PRIORITY, \
			      &arm350_dma_driver_api);

DT_INST_FOREACH_STATUS_OKAY(ARM350_DMA_INIT)
