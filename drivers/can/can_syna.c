/*
 * Copyright 2026, Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT syna_can

#include <errno.h>
#include <string.h>

#include <zephyr/cache.h>
#include <zephyr/device.h>
#include <zephyr/drivers/can.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(can_syna, CONFIG_CAN_LOG_LEVEL);

#define CAN_SYNA_REG_INTERRUPT_MASK(index) (0x3fcU + ((index) * 4U))
#define CAN_SYNA_REG_OPERATIONAL 0x404U
#define CAN_SYNA_REG_CONTROL 0x408U
#define CAN_SYNA_REG_LO_TX_FIFO 0x414U
#define CAN_SYNA_REG_REC_TEC_BUS 0x41cU
#define CAN_SYNA_REG_CLASSIC_BAUD 0x424U
#define CAN_SYNA_REG_FD_BAUD 0x428U
#define CAN_SYNA_REG_CLASSIC_JUMP 0x42cU
#define CAN_SYNA_REG_FD_JUMP 0x430U
#define CAN_SYNA_REG_CLASSIC_END_SYNC 0x434U
#define CAN_SYNA_REG_FD_END_SYNC 0x438U
#define CAN_SYNA_REG_CLASSIC_END_PROP 0x43cU
#define CAN_SYNA_REG_FD_END_PROP 0x440U
#define CAN_SYNA_REG_CLASSIC_END_PHASE1 0x444U
#define CAN_SYNA_REG_FD_END_PHASE1 0x448U
#define CAN_SYNA_REG_TUR_INCREMENT 0x450U
#define CAN_SYNA_REG_INTERRUPTS_FROM_HW 0x48cU
#define CAN_SYNA_REG_FILTER_BASE_ADDR 0x490U
#define CAN_SYNA_REG_NUMBER_OF_PAIRS 0x494U
#define CAN_SYNA_REG_WATCHDOG_LIMIT 0x498U
#define CAN_SYNA_REG_ALLOWED_RETRANSMITS 0x4a0U
#define CAN_SYNA_REG_RX_PTR_BUFS 0x4a4U
#define CAN_SYNA_REG_USED_BUFS_PTR 0x4acU
#define CAN_SYNA_REG_USED_BUFS_PTR_COUNT 0x4b0U
#define CAN_SYNA_REG_SSP_DELAY 0x4d0U
#define CAN_SYNA_REG_R_BASE_ADDR 0xf00U
#define CAN_SYNA_REG_W_BASE_ADDR 0xf08U

#define CAN_SYNA_CONTROL_SOFT_RESET BIT(0)
#define CAN_SYNA_CONTROL_DISABLE_ECC BIT(5)
#define CAN_SYNA_CONTROL_ISO_FD BIT(6)
#define CAN_SYNA_CONTROL_DMA_SOFT_RESET BIT(20)

#define CAN_SYNA_OPERATIONAL_ENABLE BIT(0)
#define CAN_SYNA_OPERATIONAL_TIMEOUT_DISABLE BIT(5)
#define CAN_SYNA_OPERATIONAL_SELF_ACK BIT(6)
#define CAN_SYNA_OPERATIONAL_LOOPBACK BIT(7)
#define CAN_SYNA_OPERATIONAL_LISTEN_ONLY BIT(8)

#define CAN_SYNA_REC_TEC_BUS_REC_MASK GENMASK(9, 0)
#define CAN_SYNA_REC_TEC_BUS_TEC_MASK GENMASK(19, 10)
#define CAN_SYNA_REC_TEC_BUS_STATE_MASK GENMASK(22, 20)

#define CAN_SYNA_INTR_CLOSING_GOOD_RX_FRAME BIT(1)
#define CAN_SYNA_INTR_TX_RETRANSMIT_REQUEST BIT(2)
#define CAN_SYNA_INTR_INCREMENT_REC_8 BIT(3)
#define CAN_SYNA_INTR_INCREMENT_REC_1 BIT(4)
#define CAN_SYNA_INTR_INCREMENT_TEC_8 BIT(5)
#define CAN_SYNA_INTR_ERROR_FRAME_DETECTED BIT(6)
#define CAN_SYNA_INTR_ABORTS_HAPPENED BIT(7)
#define CAN_SYNA_INTR_USED_BUFFERS_NOT_EMPTY BIT(11)
#define CAN_SYNA_INTR_CLASSIC_TX_NO_ACK BIT(12)
#define CAN_SYNA_INTR_FD_TX_NO_ACK BIT(13)
#define CAN_SYNA_INTR_CLASSIC_TX_GOOD BIT(14)
#define CAN_SYNA_INTR_FD_TX_GOOD BIT(15)
#define CAN_SYNA_INTR_BUS_STATE1 BIT(16)
#define CAN_SYNA_INTR_BUS_STATE2 BIT(17)
#define CAN_SYNA_INTR_PANIC_TX_START_FAILED BIT(22)
#define CAN_SYNA_INTR_PANIC_TX_WRONG_FRAME BIT(23)
#define CAN_SYNA_INTR_PANICS BIT(26)

static inline mem_addr_t can_syna_reg_addr(uintptr_t base, uint32_t offset)
{
	return (mem_addr_t)(base + offset);
}

static inline uint32_t can_syna_reg_read(uintptr_t base, uint32_t offset)
{
	return sys_read32(can_syna_reg_addr(base, offset));
}

static inline void can_syna_reg_write(uintptr_t base, uint32_t offset, uint32_t value)
{
	sys_write32(value, can_syna_reg_addr(base, offset));
}

static inline void can_syna_reg_set_bits(uintptr_t base, uint32_t offset, uint32_t mask)
{
	sys_set_bits(can_syna_reg_addr(base, offset), mask);
}

static inline void can_syna_reg_clear_bits(uintptr_t base, uint32_t offset, uint32_t mask)
{
	sys_clear_bits(can_syna_reg_addr(base, offset), mask);
}

#define CAN_SYNA_MAX_DATA_LENGTH_FD 64U
#define CAN_SYNA_TX_DESC_RESERVED_BYTES 20U
#define CAN_SYNA_RX_DESC_RESERVED_BYTES 8U
#define CAN_SYNA_RX_HEADER_RAW_WORDS 6U
#define CAN_SYNA_FRAME_TYPE_CLASSIC 1U
#define CAN_SYNA_FRAME_TYPE_FD 2U
#define CAN_SYNA_MAX_TX_FIFO_DEPTH 32U
#define CAN_SYNA_MAX_RX_FIFO_DEPTH 32U
#define CAN_SYNA_MAX_FILTERS 64U
#define CAN_SYNA_USED_BUFFER_TYPE_MASK 0x3U
#define CAN_SYNA_USED_BUFFER_TYPE_TX 0x1U
#define CAN_SYNA_USED_BUFFER_TYPE_RX 0x2U
#define CAN_SYNA_USED_BUFFER_TYPE_ABORT 0x3U
#define CAN_SYNA_STD_ID_UPPER_SHIFT 7U
#define CAN_SYNA_EXT_ID_COMPOSE_SHIFT 18U
#define CAN_SYNA_EXT_ID_LOW_MASK BIT_MASK(18)
#define CAN_SYNA_DEFAULT_RETRANSMITS 20U
#define CAN_SYNA_TIMESTAMP_INCREMENT 0x8000U
#define CAN_SYNA_CACHE_ALIGN 32U
#define CAN_SYNA_SOFT_RESET_DELAY_US 1U
#define CAN_SYNA_BUS_ACTIVE 1U
#define CAN_SYNA_BUS_PASSIVE 2U
#define CAN_SYNA_BUS_OFF 4U

#define CAN_SYNA_SUPPORTED_MODES \
	(CAN_MODE_LOOPBACK | CAN_MODE_LISTENONLY | CAN_MODE_ONE_SHOT | CAN_MODE_FD | \
	 CAN_MODE_MANUAL_RECOVERY)

#define CAN_SYNA_INTERRUPT_MASK                                                       \
	(CAN_SYNA_INTR_USED_BUFFERS_NOT_EMPTY | CAN_SYNA_INTR_CLOSING_GOOD_RX_FRAME | \
	 CAN_SYNA_INTR_TX_RETRANSMIT_REQUEST | CAN_SYNA_INTR_INCREMENT_REC_8 |        \
	 CAN_SYNA_INTR_INCREMENT_REC_1 | CAN_SYNA_INTR_INCREMENT_TEC_8 |              \
	 CAN_SYNA_INTR_ERROR_FRAME_DETECTED | CAN_SYNA_INTR_ABORTS_HAPPENED |         \
	 CAN_SYNA_INTR_CLASSIC_TX_NO_ACK | CAN_SYNA_INTR_FD_TX_NO_ACK |               \
	 CAN_SYNA_INTR_BUS_STATE1 | CAN_SYNA_INTR_BUS_STATE2 |                        \
	 CAN_SYNA_INTR_PANIC_TX_START_FAILED | CAN_SYNA_INTR_PANIC_TX_WRONG_FRAME |   \
	 CAN_SYNA_INTR_PANICS | CAN_SYNA_INTR_CLASSIC_TX_GOOD |                       \
	 CAN_SYNA_INTR_FD_TX_GOOD)

struct can_syna_tx_desc {
	struct {
		union {
			struct {
				uint32_t dlc : 12;
				uint32_t id : 11;
				uint32_t reserved : 1;
				uint32_t esi : 1;
				uint32_t brs : 1;
				uint32_t rtr : 1;
				uint32_t ide : 1;
				uint32_t frame_type : 4;
			} fields;
			uint32_t raw;
		} signal_desc;
		uint32_t ext_id;
		uint32_t reserved;
	} header;
	uint8_t data[CAN_SYNA_MAX_DATA_LENGTH_FD];
	uint8_t reserved[CAN_SYNA_TX_DESC_RESERVED_BYTES];
};

struct can_syna_rx_classic_header {
	union {
		struct {
			uint32_t frame_type : 2;
			uint32_t is_extended : 1;
			uint32_t is_rtr : 1;
			uint32_t status : 10;
			uint32_t dlc : 11;
			uint32_t id_0_6 : 7;
			uint32_t id_7_10 : 4;
			uint32_t ext_id : 18;
			uint32_t reserved_53_55 : 3;
			uint32_t crc_0_6 : 7;
			uint32_t crc_7_14 : 8;
			uint32_t rtr : 1;
			uint32_t ide : 1;
			uint32_t reserved_74_95 : 22;
			uint32_t reserved_96_127;
			uint32_t timestamp_low;
			uint32_t timestamp_high;
		};
	};
};

struct can_syna_rx_fd_header {
	union {
		struct {
			uint32_t frame_type : 2;
			uint32_t is_extended : 1;
			uint32_t is_rtr : 1;
			uint32_t status : 10;
			uint32_t dlc : 11;
			uint32_t id_0_6 : 7;
			uint32_t id_7_10 : 4;
			uint32_t ext_id : 18;
			uint32_t reserved_53_55 : 3;
			uint32_t crc_0_6 : 7;
			uint32_t crc_7_20 : 14;
			uint32_t sbc : 4;
			uint32_t ide : 1;
			uint32_t esi : 1;
			uint32_t brs : 1;
			uint32_t reserved_85_95 : 11;
			uint32_t reserved_96_127;
			uint32_t timestamp_low;
			uint32_t timestamp_high;
		};
	};
};

struct can_syna_rx_desc {
	union {
		struct can_syna_rx_classic_header classic;
		struct can_syna_rx_fd_header fd;
		uint32_t raw[CAN_SYNA_RX_HEADER_RAW_WORDS];
	} header;
	uint8_t data[CAN_SYNA_MAX_DATA_LENGTH_FD];
	uint8_t reserved[CAN_SYNA_RX_DESC_RESERVED_BYTES];
};

struct can_syna_hw_filter {
	uint32_t mask;
	uint32_t id;
};

struct can_syna_hw_data {
	struct can_syna_tx_desc tx_desc[CAN_SYNA_MAX_TX_FIFO_DEPTH];
	struct can_syna_hw_filter filters[CAN_SYNA_MAX_FILTERS];
	struct can_syna_rx_desc rx_desc[CAN_SYNA_MAX_RX_FIFO_DEPTH];
} __aligned(CAN_SYNA_CACHE_ALIGN);

struct can_syna_rx_filter {
	bool in_use;
	can_rx_callback_t callback;
	void *user_data;
	struct can_filter filter;
};

struct can_syna_tx_slot {
	bool in_use;
	can_tx_callback_t callback;
	void *user_data;
};

struct can_syna_config {
	const struct can_driver_config common;
	uintptr_t base;
	const struct device *clock_dev;
	clock_control_subsys_t clock_subsys;
	struct reset_dt_spec reset;
	const struct pinctrl_dev_config *pinctrl;
	uint32_t core_clock;
	uint8_t max_filters;
	uint8_t tx_fifo_depth;
	uint8_t rx_fifo_depth;
	void (*irq_config)(const struct device *dev);
};

struct can_syna_data {
	struct can_driver_data common;
	struct k_sem tx_sem;
	enum can_state state;
	struct can_timing timing;
	struct can_timing timing_data;
	struct can_syna_rx_filter rx_filters[CAN_SYNA_MAX_FILTERS];
	struct can_syna_tx_slot tx_slots[CAN_SYNA_MAX_TX_FIFO_DEPTH];
	struct can_syna_hw_data hw;
};

static enum can_state can_syna_decode_state(const struct device *dev,
					    struct can_bus_err_cnt *err_cnt);
static void can_syna_notify_state(const struct device *dev);

static void can_syna_cache_flush(const void *addr, size_t size)
{
#if defined(CONFIG_CACHE_MANAGEMENT) && defined(CONFIG_DCACHE)
	(void)sys_cache_data_flush_range((void *)addr, size);
#else
	ARG_UNUSED(addr);
	ARG_UNUSED(size);
#endif
}

static void can_syna_cache_invd(const void *addr, size_t size)
{
#if defined(CONFIG_CACHE_MANAGEMENT) && defined(CONFIG_DCACHE)
	(void)sys_cache_data_invd_range((void *)addr, size);
#else
	ARG_UNUSED(addr);
	ARG_UNUSED(size);
#endif
}

static int can_syna_get_core_clock(const struct device *dev, uint32_t *rate)
{
	const struct can_syna_config *config = dev->config;

	if (rate == NULL) {
		return -EINVAL;
	}

	*rate = config->core_clock;
	return 0;
}

static int can_syna_timing_to_regs(const struct can_timing *timing, uint32_t *baud,
					  uint32_t *jump, uint32_t *end_sync,
					  uint32_t *end_prop, uint32_t *end_phase1)
{
	uint32_t prop_end;

	*baud = timing->prescaler *
		(1U + timing->prop_seg + timing->phase_seg1 + timing->phase_seg2);
	*jump = timing->sjw * timing->prescaler;
	*end_sync = timing->prescaler;
	prop_end = timing->prescaler * (1U + timing->prop_seg);
	*end_phase1 = timing->prescaler * (1U + timing->prop_seg + timing->phase_seg1);

	if ((*baud == 0U) || (*jump == 0U) || (*end_sync == 0U) || (prop_end == 0U) ||
	    (*end_phase1 >= *baud)) {
		return -EINVAL;
	}

	*end_prop = prop_end - 1U;
	return 0;
}

static void can_syna_hw_soft_reset(const struct device *dev)
{
	const struct can_syna_config *config = dev->config;
	uint32_t reset_mask = CAN_SYNA_CONTROL_SOFT_RESET |
			      CAN_SYNA_CONTROL_DMA_SOFT_RESET;
	uint32_t control = can_syna_reg_read(config->base, CAN_SYNA_REG_CONTROL);

	can_syna_reg_write(config->base, CAN_SYNA_REG_CONTROL, control | reset_mask);
	k_busy_wait(CAN_SYNA_SOFT_RESET_DELAY_US);
	can_syna_reg_write(config->base, CAN_SYNA_REG_CONTROL, control & ~reset_mask);
}

static void can_syna_reset_tx_state(const struct device *dev)
{
	const struct can_syna_config *config = dev->config;
	struct can_syna_data *data = dev->data;

	memset(data->tx_slots, 0, sizeof(data->tx_slots));
	memset(data->hw.tx_desc, 0, sizeof(data->hw.tx_desc));
	can_syna_cache_flush(data->hw.tx_desc, sizeof(data->hw.tx_desc));

	k_sem_reset(&data->tx_sem);
	for (uint32_t i = 0; i < config->tx_fifo_depth; i++) {
		k_sem_give(&data->tx_sem);
	}
}

static void can_syna_apply_timing(const struct device *dev,
					 const struct can_timing *timing)
{
	const struct can_syna_config *config = dev->config;
	uint32_t baud;
	uint32_t jump;
	uint32_t end_sync;
	uint32_t end_prop;
	uint32_t end_phase1;

	(void)can_syna_timing_to_regs(timing, &baud, &jump, &end_sync, &end_prop,
					     &end_phase1);
	can_syna_reg_write(config->base, CAN_SYNA_REG_CLASSIC_BAUD, baud);
	can_syna_reg_write(config->base, CAN_SYNA_REG_CLASSIC_JUMP, jump);
	can_syna_reg_write(config->base, CAN_SYNA_REG_CLASSIC_END_SYNC, end_sync);
	can_syna_reg_write(config->base, CAN_SYNA_REG_CLASSIC_END_PROP, end_prop);
	can_syna_reg_write(config->base, CAN_SYNA_REG_CLASSIC_END_PHASE1, end_phase1);
}

static void can_syna_apply_timing_data(const struct device *dev,
					      const struct can_timing *timing_data)
{
	const struct can_syna_config *config = dev->config;
	uint32_t baud;
	uint32_t jump;
	uint32_t end_sync;
	uint32_t end_prop;
	uint32_t end_phase1;

	(void)can_syna_timing_to_regs(timing_data, &baud, &jump, &end_sync, &end_prop,
					     &end_phase1);
	can_syna_reg_write(config->base, CAN_SYNA_REG_FD_BAUD, baud);
	can_syna_reg_write(config->base, CAN_SYNA_REG_FD_JUMP, jump);
	can_syna_reg_write(config->base, CAN_SYNA_REG_FD_END_SYNC, end_sync);
	can_syna_reg_write(config->base, CAN_SYNA_REG_FD_END_PROP, end_prop);
	can_syna_reg_write(config->base, CAN_SYNA_REG_FD_END_PHASE1, end_phase1);
}

static int can_syna_set_timing(const struct device *dev, const struct can_timing *timing)
{
	struct can_syna_data *data = dev->data;
	uint32_t baud;
	uint32_t jump;
	uint32_t end_sync;
	uint32_t end_prop;
	uint32_t end_phase1;
	int ret;

	if (data->common.started) {
		return -EBUSY;
	}

	ret = can_syna_timing_to_regs(timing, &baud, &jump, &end_sync, &end_prop,
					     &end_phase1);
	if (ret != 0) {
		return ret;
	}

	can_syna_apply_timing(dev, timing);
	data->timing = *timing;

	return 0;
}

static int can_syna_set_timing_data(const struct device *dev,
					   const struct can_timing *timing_data)
{
	struct can_syna_data *data = dev->data;
	uint32_t baud;
	uint32_t jump;
	uint32_t end_sync;
	uint32_t end_prop;
	uint32_t end_phase1;
	int ret;

	if (data->common.started) {
		return -EBUSY;
	}

	ret = can_syna_timing_to_regs(timing_data, &baud, &jump, &end_sync, &end_prop,
					     &end_phase1);
	if (ret != 0) {
		return ret;
	}

	can_syna_apply_timing_data(dev, timing_data);
	data->timing_data = *timing_data;

	return 0;
}

static int can_syna_get_capabilities(const struct device *dev, can_mode_t *cap)
{
	ARG_UNUSED(dev);

	if (cap == NULL) {
		return -EINVAL;
	}

	*cap = CAN_SYNA_SUPPORTED_MODES;
	return 0;
}

static int can_syna_set_mode(const struct device *dev, can_mode_t mode)
{
	struct can_syna_data *data = dev->data;

	if ((mode & ~CAN_SYNA_SUPPORTED_MODES) != 0U) {
		return -ENOTSUP;
	}

	if (data->common.started) {
		return -EBUSY;
	}

	data->common.mode = mode;
	return 0;
}

static void can_syna_hw_filter_encode(struct can_syna_hw_filter *hw,
				      const struct can_filter *filter)
{
	if ((filter->flags & CAN_FILTER_IDE) != 0U) {
		hw->mask = filter->mask & CAN_EXT_ID_MASK;
		hw->id = filter->id & hw->mask;
	} else {
		hw->mask = filter->mask & CAN_STD_ID_MASK;
		hw->id = filter->id & hw->mask;
	}
}

static void can_syna_program_filters(const struct device *dev)
{
	const struct can_syna_config *config = dev->config;
	struct can_syna_data *data = dev->data;
	uint32_t count = 0U;

	memset(data->hw.filters, 0, sizeof(data->hw.filters));

	for (uint32_t i = 0; i < config->max_filters; i++) {
		if (!data->rx_filters[i].in_use) {
			continue;
		}

		can_syna_hw_filter_encode(&data->hw.filters[count],
					  &data->rx_filters[i].filter);
		count++;
	}

	can_syna_cache_flush(data->hw.filters, sizeof(data->hw.filters));
	can_syna_reg_write(config->base, CAN_SYNA_REG_FILTER_BASE_ADDR,
			   (uint32_t)(uintptr_t)&data->hw.filters[0]);
	can_syna_reg_write(config->base, CAN_SYNA_REG_NUMBER_OF_PAIRS, count);
}

static void can_syna_seed_rx_fifo(const struct device *dev)
{
	const struct can_syna_config *config = dev->config;
	struct can_syna_data *data = dev->data;

	for (uint32_t i = 0; i < config->rx_fifo_depth; i++) {
		memset(&data->hw.rx_desc[i], 0, sizeof(data->hw.rx_desc[i]));
		can_syna_cache_flush(&data->hw.rx_desc[i], sizeof(data->hw.rx_desc[i]));
		can_syna_reg_write(config->base, CAN_SYNA_REG_RX_PTR_BUFS,
				    (uint32_t)(uintptr_t)&data->hw.rx_desc[i]);
	}
}

static void can_syna_hw_configure(const struct device *dev)
{
	const struct can_syna_config *config = dev->config;
	struct can_syna_data *data = dev->data;
	uint32_t control;
	uint32_t operational;

	can_syna_reg_write(config->base, CAN_SYNA_REG_OPERATIONAL, 0U);
	can_syna_reg_write(config->base, CAN_SYNA_REG_INTERRUPT_MASK(0), 0U);
	can_syna_reg_write(config->base, CAN_SYNA_REG_INTERRUPT_MASK(1), 0U);
	can_syna_reg_write(config->base, CAN_SYNA_REG_INTERRUPTS_FROM_HW, UINT32_MAX);
	can_syna_reg_write(config->base, CAN_SYNA_REG_R_BASE_ADDR,
			   (uint32_t)(uintptr_t)&data->hw);
	can_syna_reg_write(config->base, CAN_SYNA_REG_W_BASE_ADDR,
			   (uint32_t)(uintptr_t)&data->hw.rx_desc[0]);
	can_syna_reg_write(config->base, CAN_SYNA_REG_FILTER_BASE_ADDR,
			   (uint32_t)(uintptr_t)&data->hw.filters[0]);
	can_syna_reg_write(config->base, CAN_SYNA_REG_TUR_INCREMENT,
			   CAN_SYNA_TIMESTAMP_INCREMENT);
	can_syna_reg_write(config->base, CAN_SYNA_REG_SSP_DELAY, 0U);
	can_syna_reg_write(config->base, CAN_SYNA_REG_WATCHDOG_LIMIT, 0U);
	can_syna_reg_write(config->base, CAN_SYNA_REG_ALLOWED_RETRANSMITS,
			   (data->common.mode & CAN_MODE_ONE_SHOT) ?
			   0U : CAN_SYNA_DEFAULT_RETRANSMITS);

	control = CAN_SYNA_CONTROL_DISABLE_ECC;
	if ((data->common.mode & CAN_MODE_FD) != 0U) {
		control |= CAN_SYNA_CONTROL_ISO_FD;
	}
	can_syna_reg_write(config->base, CAN_SYNA_REG_CONTROL, control);
	can_syna_apply_timing(dev, &data->timing);
	if ((data->common.mode & CAN_MODE_FD) != 0U) {
		can_syna_apply_timing_data(dev, &data->timing_data);
	}

	operational = CAN_SYNA_OPERATIONAL_TIMEOUT_DISABLE;
	if ((data->common.mode & CAN_MODE_LOOPBACK) != 0U) {
		operational |= CAN_SYNA_OPERATIONAL_LOOPBACK |
			       CAN_SYNA_OPERATIONAL_SELF_ACK;
	}
	if ((data->common.mode & CAN_MODE_LISTENONLY) != 0U) {
		operational |= CAN_SYNA_OPERATIONAL_LISTEN_ONLY;
	}
	can_syna_reg_write(config->base, CAN_SYNA_REG_OPERATIONAL, operational);

	can_syna_program_filters(dev);
	can_syna_seed_rx_fifo(dev);
	can_syna_reg_write(config->base, CAN_SYNA_REG_INTERRUPT_MASK(0),
			   CAN_SYNA_INTERRUPT_MASK);
}

static int can_syna_start(const struct device *dev)
{
	const struct can_syna_config *config = dev->config;
	struct can_syna_data *data = dev->data;

	if (data->common.started) {
		return -EALREADY;
	}

	CAN_STATS_RESET(dev);
	can_syna_hw_configure(dev);
	can_syna_reg_set_bits(config->base, CAN_SYNA_REG_OPERATIONAL,
			      CAN_SYNA_OPERATIONAL_ENABLE);
	data->common.started = true;
	can_syna_notify_state(dev);

	return 0;
}

static int can_syna_stop(const struct device *dev)
{
	const struct can_syna_config *config = dev->config;
	struct can_syna_data *data = dev->data;

	if (!data->common.started) {
		return -EALREADY;
	}

	can_syna_reg_clear_bits(config->base, CAN_SYNA_REG_OPERATIONAL,
				CAN_SYNA_OPERATIONAL_ENABLE);
	can_syna_reg_write(config->base, CAN_SYNA_REG_INTERRUPT_MASK(0), 0U);
	can_syna_reg_write(config->base, CAN_SYNA_REG_INTERRUPT_MASK(1), 0U);
	can_syna_reg_write(config->base, CAN_SYNA_REG_INTERRUPTS_FROM_HW, UINT32_MAX);
	can_syna_hw_soft_reset(dev);
	can_syna_reset_tx_state(dev);
	data->common.started = false;
	can_syna_notify_state(dev);

	return 0;
}

static int can_syna_get_max_filters(const struct device *dev, bool ide)
{
	const struct can_syna_config *config = dev->config;

	ARG_UNUSED(ide);

	return config->max_filters;
}

static int can_syna_recover(const struct device *dev, k_timeout_t timeout)
{
	const struct can_syna_config *config = dev->config;
	struct can_syna_data *data = dev->data;
	struct can_bus_err_cnt err_cnt;
	enum can_state state;
	int64_t end_time;

	if (!data->common.started) {
		return -ENETDOWN;
	}

	if ((data->common.mode & CAN_MODE_MANUAL_RECOVERY) == 0U) {
		return -ENOTSUP;
	}

	if (K_TIMEOUT_EQ(timeout, K_FOREVER)) {
		end_time = INT64_MAX;
	} else if (K_TIMEOUT_EQ(timeout, K_NO_WAIT)) {
		end_time = k_uptime_get();
	} else {
		end_time = k_uptime_get() + k_ticks_to_ms_floor64(timeout.ticks);
	}

	can_syna_reg_clear_bits(config->base, CAN_SYNA_REG_OPERATIONAL,
				CAN_SYNA_OPERATIONAL_ENABLE);
	can_syna_reg_write(config->base, CAN_SYNA_REG_INTERRUPT_MASK(0), 0U);
	can_syna_reg_write(config->base, CAN_SYNA_REG_INTERRUPT_MASK(1), 0U);
	can_syna_reg_write(config->base, CAN_SYNA_REG_INTERRUPTS_FROM_HW, UINT32_MAX);

	can_syna_hw_soft_reset(dev);
	can_syna_reset_tx_state(dev);
	can_syna_hw_configure(dev);

	can_syna_reg_set_bits(config->base, CAN_SYNA_REG_OPERATIONAL,
			      CAN_SYNA_OPERATIONAL_ENABLE);

	do {
		state = can_syna_decode_state(dev, &err_cnt);
		if (state != CAN_STATE_BUS_OFF) {
			data->state = state;

			if (data->common.state_change_cb != NULL) {
				data->common.state_change_cb(
					dev, state, err_cnt,
					data->common.state_change_cb_user_data);
			}

			return 0;
		}

		if (K_TIMEOUT_EQ(timeout, K_NO_WAIT)) {
			break;
		}

		k_sleep(K_MSEC(1));
	} while (k_uptime_get() < end_time);

	return -EAGAIN;
}

static int can_syna_add_rx_filter(const struct device *dev, can_rx_callback_t callback,
				  void *user_data, const struct can_filter *filter)
{
	const struct can_syna_config *config = dev->config;
	struct can_syna_data *data = dev->data;
	int filter_id = -ENOSPC;

	if ((callback == NULL) || (filter == NULL)) {
		return -EINVAL;
	}

	if ((filter->flags & CAN_FILTER_IDE) != 0U) {
		if ((filter->id > CAN_EXT_ID_MASK) || (filter->mask > CAN_EXT_ID_MASK)) {
			return -EINVAL;
		}
	} else if ((filter->id > CAN_STD_ID_MASK) || (filter->mask > CAN_STD_ID_MASK)) {
		return -EINVAL;
	}

	for (int i = 0; i < config->max_filters; i++) {
		if (!data->rx_filters[i].in_use) {
			data->rx_filters[i].callback = callback;
			data->rx_filters[i].user_data = user_data;
			data->rx_filters[i].filter = *filter;
			data->rx_filters[i].in_use = true;
			filter_id = i;
			break;
		}
	}

	if (filter_id >= 0) {
		can_syna_program_filters(dev);
	}

	return filter_id;
}

static void can_syna_remove_rx_filter(const struct device *dev, int filter_id)
{
	const struct can_syna_config *config = dev->config;
	struct can_syna_data *data = dev->data;

	if ((filter_id < 0) || (filter_id >= config->max_filters)) {
		return;
	}

	memset(&data->rx_filters[filter_id], 0, sizeof(data->rx_filters[filter_id]));
	can_syna_program_filters(dev);
}

static int can_syna_find_tx_slot(const struct device *dev)
{
	const struct can_syna_config *config = dev->config;
	struct can_syna_data *data = dev->data;

	for (int i = 0; i < config->tx_fifo_depth; i++) {
		if (!data->tx_slots[i].in_use) {
			return i;
		}
	}

	return -ENOSPC;
}

static int can_syna_validate_frame(const struct can_frame *frame, can_mode_t mode)
{
	bool fd;

	if (frame == NULL) {
		return -EINVAL;
	}

	fd = (frame->flags & CAN_FRAME_FDF) != 0U;

	if ((frame->flags & CAN_FRAME_IDE) != 0U) {
		if (frame->id > CAN_EXT_ID_MASK) {
			return -EINVAL;
		}
	} else if (frame->id > CAN_STD_ID_MASK) {
		return -EINVAL;
	}

	if (!fd) {
		if (frame->dlc > CAN_MAX_DLC) {
			return -EINVAL;
		}

		if ((frame->flags & (CAN_FRAME_BRS | CAN_FRAME_ESI)) != 0U) {
			return -EINVAL;
		}

		return 0;
	}

	if ((mode & CAN_MODE_FD) == 0U) {
		return -ENOTSUP;
	}

	if (frame->dlc > CANFD_MAX_DLC) {
		return -EINVAL;
	}

	if ((frame->flags & CAN_FRAME_RTR) != 0U) {
		return -EINVAL;
	}

	return 0;
}

static void can_syna_fill_tx_desc(struct can_syna_tx_desc *desc,
				  const struct can_frame *frame)
{
	bool ext = (frame->flags & CAN_FRAME_IDE) != 0U;
	bool fd = (frame->flags & CAN_FRAME_FDF) != 0U;
	uint32_t std_id;

	memset(desc, 0, sizeof(*desc));
	desc->header.signal_desc.fields.dlc = frame->dlc;
	desc->header.signal_desc.fields.rtr = (frame->flags & CAN_FRAME_RTR) != 0U;
	desc->header.signal_desc.fields.ide = ext;
	desc->header.signal_desc.fields.frame_type =
		fd ? CAN_SYNA_FRAME_TYPE_FD : CAN_SYNA_FRAME_TYPE_CLASSIC;
	desc->header.signal_desc.fields.brs = (frame->flags & CAN_FRAME_BRS) != 0U;
	desc->header.signal_desc.fields.esi = (frame->flags & CAN_FRAME_ESI) != 0U;

	if (ext) {
		std_id = frame->id >> CAN_SYNA_EXT_ID_COMPOSE_SHIFT;
		desc->header.signal_desc.fields.id = std_id;
		desc->header.ext_id = frame->id & CAN_SYNA_EXT_ID_LOW_MASK;
	} else {
		desc->header.signal_desc.fields.id = frame->id;
	}

	memcpy(desc->data, frame->data, can_dlc_to_bytes(frame->dlc));
}

static int can_syna_send(const struct device *dev, const struct can_frame *frame,
				k_timeout_t timeout, can_tx_callback_t callback, void *user_data)
{
	const struct can_syna_config *config = dev->config;
	struct can_syna_data *data = dev->data;
	struct can_syna_tx_desc *desc;
	int slot;
	int ret;

	if (!data->common.started) {
		return -ENETDOWN;
	}

	ret = can_syna_validate_frame(frame, data->common.mode);
	if (ret != 0) {
		return ret;
	}

	if (k_sem_take(&data->tx_sem, timeout) != 0) {
		return -EAGAIN;
	}

	slot = can_syna_find_tx_slot(dev);
	if (slot < 0) {
		k_sem_give(&data->tx_sem);
		return slot;
	}

	data->tx_slots[slot].in_use = true;
	data->tx_slots[slot].callback = callback;
	data->tx_slots[slot].user_data = user_data;
	desc = &data->hw.tx_desc[slot];
	can_syna_fill_tx_desc(desc, frame);
	can_syna_cache_flush(desc, sizeof(*desc));
	can_syna_reg_write(config->base, CAN_SYNA_REG_LO_TX_FIFO,
			   (uint32_t)(uintptr_t)desc);

	return 0;
}

static enum can_state can_syna_decode_state(const struct device *dev,
						   struct can_bus_err_cnt *err_cnt)
{
	const struct can_syna_config *config = dev->config;
	struct can_syna_data *data = dev->data;
	uint32_t rectecbus = can_syna_reg_read(config->base, CAN_SYNA_REG_REC_TEC_BUS);
	uint32_t rec = FIELD_GET(CAN_SYNA_REC_TEC_BUS_REC_MASK, rectecbus);
	uint32_t tec = FIELD_GET(CAN_SYNA_REC_TEC_BUS_TEC_MASK, rectecbus);
	uint32_t bus = FIELD_GET(CAN_SYNA_REC_TEC_BUS_STATE_MASK, rectecbus);

	if (err_cnt != NULL) {
		err_cnt->rx_err_cnt = MIN(rec, UINT8_MAX);
		err_cnt->tx_err_cnt = MIN(tec, UINT8_MAX);
	}

	if (!data->common.started) {
		return CAN_STATE_STOPPED;
	}

	switch (bus) {
	case CAN_SYNA_BUS_OFF:
		return CAN_STATE_BUS_OFF;
	case CAN_SYNA_BUS_PASSIVE:
		return CAN_STATE_ERROR_PASSIVE;
	case CAN_SYNA_BUS_ACTIVE:
	default:
		if ((rec >= 96U) || (tec >= 96U)) {
			return CAN_STATE_ERROR_WARNING;
		}
		return CAN_STATE_ERROR_ACTIVE;
	}
}

static int can_syna_get_state(const struct device *dev, enum can_state *state,
				     struct can_bus_err_cnt *err_cnt)
{
	enum can_state current_state = can_syna_decode_state(dev, err_cnt);

	if (state != NULL) {
		*state = current_state;
	}

	return 0;
}

static void can_syna_set_state_change_callback(const struct device *dev,
						      can_state_change_callback_t callback,
						      void *user_data)
{
	struct can_syna_data *data = dev->data;

	data->common.state_change_cb = callback;
	data->common.state_change_cb_user_data = user_data;
}

static void can_syna_notify_state(const struct device *dev)
{
	struct can_syna_data *data = dev->data;
	struct can_bus_err_cnt err_cnt;
	enum can_state state = can_syna_decode_state(dev, &err_cnt);

	if (state == data->state) {
		return;
	}

	data->state = state;
	if (data->common.state_change_cb != NULL) {
		data->common.state_change_cb(dev, state, err_cnt,
					     data->common.state_change_cb_user_data);
	}
}

static int can_syna_tx_index(const struct device *dev, uintptr_t ptr)
{
	const struct can_syna_config *config = dev->config;
	struct can_syna_data *data = dev->data;

	for (int i = 0; i < config->tx_fifo_depth; i++) {
		if (ptr == (uintptr_t)&data->hw.tx_desc[i]) {
			return i;
		}
	}

	return -EINVAL;
}

static int can_syna_rx_index(const struct device *dev, uintptr_t ptr)
{
	const struct can_syna_config *config = dev->config;
	struct can_syna_data *data = dev->data;

	for (int i = 0; i < config->rx_fifo_depth; i++) {
		if (ptr == (uintptr_t)&data->hw.rx_desc[i]) {
			return i;
		}
	}

	return -EINVAL;
}

static void can_syna_complete_tx(const struct device *dev, uintptr_t ptr, int error)
{
	struct can_syna_data *data = dev->data;
	can_tx_callback_t callback;
	void *user_data;
	int slot = can_syna_tx_index(dev, ptr);

	if (slot < 0) {
		LOG_WRN("invalid TX used-buffer pointer 0x%lx", (unsigned long)ptr);
		return;
	}

	callback = data->tx_slots[slot].callback;
	user_data = data->tx_slots[slot].user_data;
	memset(&data->tx_slots[slot], 0, sizeof(data->tx_slots[slot]));
	memset(&data->hw.tx_desc[slot], 0, sizeof(data->hw.tx_desc[slot]));
	k_sem_give(&data->tx_sem);

	if (callback != NULL) {
		callback(dev, error, user_data);
	}
}

static int can_syna_decode_rx(const struct device *dev, struct can_frame *frame,
			      const struct can_syna_rx_desc *desc)
{
	struct can_syna_data *data = dev->data;
	bool fd = desc->header.classic.frame_type == CAN_SYNA_FRAME_TYPE_FD;
	uint32_t status;
	uint32_t id;
	uint8_t dlc;

	memset(frame, 0, sizeof(*frame));

	if (fd) {
		if ((data->common.mode & CAN_MODE_FD) == 0U) {
			return -ENOTSUP;
		}

		status = desc->header.fd.status;
		dlc = desc->header.fd.dlc;
		if (dlc > CANFD_MAX_DLC) {
			return -EINVAL;
		}

		id = desc->header.fd.id_0_6 |
		     (desc->header.fd.id_7_10 << CAN_SYNA_STD_ID_UPPER_SHIFT);

		if (desc->header.fd.is_extended || desc->header.fd.ide) {
			frame->flags |= CAN_FRAME_IDE;
			id = (id << CAN_SYNA_EXT_ID_COMPOSE_SHIFT) | desc->header.fd.ext_id;
		}
		if (desc->header.fd.brs) {
			frame->flags |= CAN_FRAME_BRS;
		}
		if (desc->header.fd.esi) {
			frame->flags |= CAN_FRAME_ESI;
		}
		frame->flags |= CAN_FRAME_FDF;
	} else {
		status = desc->header.classic.status;
		dlc = desc->header.classic.dlc;
		if (dlc > CAN_MAX_DLC) {
			return -EINVAL;
		}

		id = desc->header.classic.id_0_6 |
		     (desc->header.classic.id_7_10 << CAN_SYNA_STD_ID_UPPER_SHIFT);

		if (desc->header.classic.is_extended || desc->header.classic.ide) {
			frame->flags |= CAN_FRAME_IDE;
			id = (id << CAN_SYNA_EXT_ID_COMPOSE_SHIFT) | desc->header.classic.ext_id;
		}
		if (desc->header.classic.is_rtr || desc->header.classic.rtr) {
			frame->flags |= CAN_FRAME_RTR;
		}
	}

	if (status != 0U) {
		return -EIO;
	}

	frame->id = id;
	frame->dlc = dlc;
	memcpy(frame->data, desc->data, can_dlc_to_bytes(frame->dlc));

	return 0;
}

static void can_syna_dispatch_rx(const struct device *dev, uintptr_t ptr)
{
	const struct can_syna_config *config = dev->config;
	struct can_syna_data *data = dev->data;
	struct can_frame frame;
	struct can_syna_rx_desc *desc;
	int slot = can_syna_rx_index(dev, ptr);

	if (slot < 0) {
		LOG_WRN("invalid RX used-buffer pointer 0x%lx", (unsigned long)ptr);
		return;
	}

	desc = &data->hw.rx_desc[slot];
	can_syna_cache_invd(desc, sizeof(*desc));

	if (can_syna_decode_rx(dev, &frame, desc) == 0) {
		if (!IS_ENABLED(CONFIG_CAN_ACCEPT_RTR) &&
		    ((frame.flags & CAN_FRAME_RTR) != 0U)) {
			goto requeue;
		}

		for (uint32_t i = 0; i < config->max_filters; i++) {
			if (data->rx_filters[i].in_use &&
			    can_frame_matches_filter(&frame, &data->rx_filters[i].filter)) {
				data->rx_filters[i].callback(dev, &frame,
							     data->rx_filters[i].user_data);
			}
		}
	} else {
		CAN_STATS_RX_OVERRUN_INC(dev);
	}

requeue:
	memset(desc, 0, sizeof(*desc));
	can_syna_cache_flush(desc, sizeof(*desc));
	can_syna_reg_write(config->base, CAN_SYNA_REG_RX_PTR_BUFS, (uint32_t)(uintptr_t)desc);
}

static void can_syna_handle_error_interrupts(const struct device *dev, uint32_t pending)
{
	if ((pending & (CAN_SYNA_INTR_CLASSIC_TX_NO_ACK |
		       CAN_SYNA_INTR_FD_TX_NO_ACK)) != 0U) {
		CAN_STATS_ACK_ERROR_INC(dev);
	}

	if ((pending & CAN_SYNA_INTR_ERROR_FRAME_DETECTED) != 0U) {
		CAN_STATS_BIT_ERROR_INC(dev);
	}
}

static void can_syna_isr(const struct device *dev)
{
	const struct can_syna_config *config = dev->config;
	uint32_t pending = can_syna_reg_read(config->base, CAN_SYNA_REG_INTERRUPTS_FROM_HW);
	uint32_t count;

	can_syna_reg_write(config->base, CAN_SYNA_REG_INTERRUPTS_FROM_HW, pending);

	if ((pending & CAN_SYNA_INTR_USED_BUFFERS_NOT_EMPTY) != 0U) {
		count = can_syna_reg_read(config->base, CAN_SYNA_REG_USED_BUFS_PTR_COUNT);
		while (count-- != 0U) {
			uint32_t entry = can_syna_reg_read(config->base,
							   CAN_SYNA_REG_USED_BUFS_PTR);
			uintptr_t ptr = entry & ~(uintptr_t)CAN_SYNA_USED_BUFFER_TYPE_MASK;

			switch (entry & CAN_SYNA_USED_BUFFER_TYPE_MASK) {
			case CAN_SYNA_USED_BUFFER_TYPE_TX:
				can_syna_complete_tx(dev, ptr, 0);
				break;
			case CAN_SYNA_USED_BUFFER_TYPE_ABORT:
				can_syna_complete_tx(dev, ptr, -EIO);
				break;
			case CAN_SYNA_USED_BUFFER_TYPE_RX:
				can_syna_dispatch_rx(dev, ptr);
				break;
			default:
				LOG_WRN("invalid used-buffer entry 0x%x", entry);
				break;
			}
		}
	}

	can_syna_handle_error_interrupts(dev, pending);

	if ((pending & (CAN_SYNA_INTR_BUS_STATE1 |
		       CAN_SYNA_INTR_BUS_STATE2 |
		       CAN_SYNA_INTR_INCREMENT_REC_8 |
		       CAN_SYNA_INTR_INCREMENT_REC_1 |
		       CAN_SYNA_INTR_INCREMENT_TEC_8)) != 0U) {
		can_syna_notify_state(dev);
	}
}

static int can_syna_init(const struct device *dev)
{
	const struct can_syna_config *config = dev->config;
	struct can_syna_data *data = dev->data;
	struct can_timing timing;
	int ret;

	if (!device_is_ready(config->clock_dev)) {
		LOG_ERR("clock controller is not ready");
		return -ENODEV;
	}

	ret = clock_control_on(config->clock_dev, config->clock_subsys);
	if (ret != 0) {
		LOG_ERR("failed to enable clock: %d", ret);
		return ret;
	}

	if (config->reset.dev != NULL) {
		if (!device_is_ready(config->reset.dev)) {
			LOG_ERR("reset controller is not ready");
			return -ENODEV;
		}

		ret = reset_line_toggle_dt(&config->reset);
		if (ret != 0) {
			LOG_ERR("failed to reset CAN: %d", ret);
			return ret;
		}
	}

	ret = pinctrl_apply_state(config->pinctrl, PINCTRL_STATE_DEFAULT);
	if (ret != 0) {
		LOG_ERR("failed to apply pinctrl: %d", ret);
		return ret;
	}

	k_sem_init(&data->tx_sem, config->tx_fifo_depth, config->tx_fifo_depth);
	data->state = CAN_STATE_STOPPED;

	can_syna_hw_soft_reset(dev);
	can_syna_reg_write(config->base, CAN_SYNA_REG_INTERRUPTS_FROM_HW, UINT32_MAX);
	can_syna_reg_write(config->base, CAN_SYNA_REG_INTERRUPT_MASK(0), 0U);
	can_syna_reg_write(config->base, CAN_SYNA_REG_INTERRUPT_MASK(1), 0U);

	ret = can_calc_timing(dev, &timing, config->common.bitrate, config->common.sample_point);
	if (ret != 0) {
		LOG_ERR("failed to calculate classic timing: %d", ret);
		return ret;
	}

	ret = can_syna_set_timing(dev, &timing);
	if (ret != 0) {
		return ret;
	}

	ret = can_calc_timing_data(dev, &timing, config->common.bitrate_data,
				   config->common.sample_point_data);
	if (ret != 0) {
		LOG_ERR("failed to calculate CAN FD timing: %d", ret);
		return ret;
	}

	ret = can_syna_set_timing_data(dev, &timing);
	if (ret != 0) {
		return ret;
	}

	config->irq_config(dev);

	return 0;
}

static DEVICE_API(can, can_syna_api) = {
	.get_capabilities = can_syna_get_capabilities,
	.start = can_syna_start,
	.stop = can_syna_stop,
	.set_mode = can_syna_set_mode,
	.set_timing = can_syna_set_timing,
	.send = can_syna_send,
	.add_rx_filter = can_syna_add_rx_filter,
	.remove_rx_filter = can_syna_remove_rx_filter,
	.get_state = can_syna_get_state,
	.set_state_change_callback = can_syna_set_state_change_callback,
	.get_core_clock = can_syna_get_core_clock,
	.get_max_filters = can_syna_get_max_filters,
	.recover = can_syna_recover,
	.timing_min = {
		.sjw = 1,
		.prop_seg = 1,
		.phase_seg1 = 1,
		.phase_seg2 = 1,
		.prescaler = 1,
	},
	.timing_max = {
		.sjw = 1024,
		.prop_seg = 1024,
		.phase_seg1 = 1024,
		.phase_seg2 = 1024,
		.prescaler = 1024,
	},
	.set_timing_data = can_syna_set_timing_data,
	.timing_data_min = {
		.sjw = 1,
		.prop_seg = 1,
		.phase_seg1 = 1,
		.phase_seg2 = 1,
		.prescaler = 1,
	},
	.timing_data_max = {
		.sjw = 1024,
		.prop_seg = 1024,
		.phase_seg1 = 1024,
		.phase_seg2 = 1024,
		.prescaler = 1024,
	},
};

#define CAN_SYNA_DEFINE(inst)                                                            \
	BUILD_ASSERT(DT_INST_PROP(inst, tx_fifo_depth) <=                              \
		     CAN_SYNA_MAX_TX_FIFO_DEPTH,                                       \
		     "tx-fifo-depth exceeds CAN_SYNA_MAX_TX_FIFO_DEPTH");              \
	BUILD_ASSERT(DT_INST_PROP(inst, rx_fifo_depth) <=                              \
		     CAN_SYNA_MAX_RX_FIFO_DEPTH,                                       \
		     "rx-fifo-depth exceeds CAN_SYNA_MAX_RX_FIFO_DEPTH");              \
	BUILD_ASSERT(DT_INST_PROP(inst, max_filters) <=                               \
		     CAN_SYNA_MAX_FILTERS,                                            \
		     "max-filters exceeds CAN_SYNA_MAX_FILTERS");                      \
	PINCTRL_DT_INST_DEFINE(inst);                                                  \
	static void can_syna_irq_config_##inst(const struct device *dev)                \
	{                                                                              \
		ARG_UNUSED(dev);                                                       \
		IRQ_CONNECT(DT_INST_IRQN(inst), DT_INST_IRQ(inst, priority),           \
			    can_syna_isr, DEVICE_DT_INST_GET(inst), 0);                \
		irq_enable(DT_INST_IRQN(inst));                                        \
	}                                                                              \
	static struct can_syna_data can_syna_data_##inst;                              \
	static const struct can_syna_config can_syna_config_##inst = {                  \
		.common = CAN_DT_DRIVER_CONFIG_INST_GET(inst, 1, 8000000),             \
		.base = DT_INST_REG_ADDR(inst),                                        \
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(inst)),                 \
		.clock_subsys = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(inst, clkid), \
		.reset = RESET_DT_SPEC_INST_GET_OR(inst, {}),                          \
		.pinctrl = PINCTRL_DT_INST_DEV_CONFIG_GET(inst),                       \
		.core_clock = DT_INST_PROP(inst, clock_frequency),                     \
		.max_filters = DT_INST_PROP(inst, max_filters),                        \
		.tx_fifo_depth = DT_INST_PROP(inst, tx_fifo_depth),                    \
		.rx_fifo_depth = DT_INST_PROP(inst, rx_fifo_depth),                    \
		.irq_config = can_syna_irq_config_##inst,                              \
	};                                                                             \
	CAN_DEVICE_DT_INST_DEFINE(inst, can_syna_init, NULL,                            \
				  &can_syna_data_##inst,                              \
				  &can_syna_config_##inst, POST_KERNEL,               \
				  CONFIG_CAN_INIT_PRIORITY, &can_syna_api);

DT_INST_FOREACH_STATUS_OKAY(CAN_SYNA_DEFINE)
