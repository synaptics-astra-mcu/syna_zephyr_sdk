/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT syna_sdio_device

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/arch/common/sys_io.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/drivers/sdio_device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#define SDIO_CTRL_REG_OFFSET        0x00U
#define SDIO_CORE_STATUS_REG_OFFSET 0x04U
#define SDIO_INTSTATUS_REG_OFFSET   0x20U
#define SDIO_F2_DMA_BASE_OFFSET     0x200U

#define SDIO_F2_READY_BIT           BIT(2)
#define SDIO_F2_ENABLED_BIT         BIT(2)

#define SDIO_DMA_XMT_RCV_EN         BIT(0)
#define SDIO_DMA_CTRL_BURST_LEN     BIT(18)
#define SDIO_DMA_STATUS_MASK        0xfU
#define SDIO_DMA_STATUS_SHIFT       28U
#define SDIO_DMA_ERROR_MASK         0xfU
#define SDIO_DMA_ERROR_SHIFT        28U

struct syna_sdio_device_dma_regs {
	uint32_t control;
	uint32_t ptr;
	uint32_t addrlow;
	uint32_t addrhigh;
	uint32_t status0;
	uint32_t status1;
};

struct syna_sdio_device_dma_map {
	struct syna_sdio_device_dma_regs xmt;
	uint32_t pad1[2];
	struct syna_sdio_device_dma_regs rcv;
	uint32_t pad2[2];
};

struct syna_sdio_device_config {
	mem_addr_t base;
	struct reset_dt_spec reset;
	const struct pinctrl_dev_config *pcfg;
};

static inline mem_addr_t syna_sdio_device_reg(
	const struct syna_sdio_device_config *config, uint32_t offset)
{
	return config->base + offset;
}

static inline mem_addr_t syna_sdio_device_dma_reg(
	const struct syna_sdio_device_config *config, uintptr_t member_offset)
{
	return config->base + SDIO_F2_DMA_BASE_OFFSET + member_offset;
}

static uint32_t syna_sdio_device_desc_count(
	struct sdio_device_dma_descriptor *desc)
{
	uint32_t desc_cnt = 1U;
	struct sdio_device_dma_descriptor *desc_ptr = desc;

	while ((desc_ptr->eof != 1U) &&
	       (desc_cnt < SDIO_DEVICE_DMA_MAX_DESCRIPTORS)) {
		desc_cnt++;
		desc_ptr++;
	}

	return desc_cnt;
}

static int syna_sdio_device_func_check(enum sdio_device_func func)
{
	switch (func) {
	case SDIO_DEVICE_FUNC_2:
		return 0;
	case SDIO_DEVICE_FUNC_3:
		return -ENOTSUP;
	default:
		return -EINVAL;
	}
}

static int syna_sdio_device_func_dma_regs(enum sdio_device_func func, bool tx,
					  uintptr_t *regs_offset)
{
	int ret = syna_sdio_device_func_check(func);

	if (ret < 0) {
		return ret;
	}

	*regs_offset = tx ? offsetof(struct syna_sdio_device_dma_map, xmt) :
				 offsetof(struct syna_sdio_device_dma_map, rcv);

	return 0;
}

static uint32_t syna_sdio_device_get_irq_status(const struct device *dev)
{
	const struct syna_sdio_device_config *config = dev->config;

	return sys_read32(syna_sdio_device_reg(config, SDIO_INTSTATUS_REG_OFFSET));
}

static int syna_sdio_device_func_ready(const struct device *dev,
					enum sdio_device_func func)
{
	const struct syna_sdio_device_config *config = dev->config;
	uint32_t val;
	int ret = syna_sdio_device_func_check(func);

	if (ret < 0) {
		return ret;
	}

	val = sys_read32(syna_sdio_device_reg(config, SDIO_CTRL_REG_OFFSET));
	sys_write32(val | SDIO_F2_READY_BIT,
		    syna_sdio_device_reg(config, SDIO_CTRL_REG_OFFSET));

	return 0;
}

static int syna_sdio_device_func_not_ready(const struct device *dev,
					 enum sdio_device_func func)
{
	const struct syna_sdio_device_config *config = dev->config;
	uint32_t val;
	int ret = syna_sdio_device_func_check(func);

	if (ret < 0) {
		return ret;
	}

	val = sys_read32(syna_sdio_device_reg(config, SDIO_CTRL_REG_OFFSET));
	sys_write32(val & ~SDIO_F2_READY_BIT,
		    syna_sdio_device_reg(config, SDIO_CTRL_REG_OFFSET));

	return 0;
}

static int syna_sdio_device_func_is_enabled(const struct device *dev,
					    enum sdio_device_func func,
					    bool *enabled)
{
	const struct syna_sdio_device_config *config = dev->config;
	int ret;

	if (enabled == NULL) {
		return -EINVAL;
	}

	ret = syna_sdio_device_func_check(func);
	if (ret < 0) {
		return ret;
	}

	*enabled = (sys_read32(syna_sdio_device_reg(config, SDIO_CORE_STATUS_REG_OFFSET)) &
		    SDIO_F2_ENABLED_BIT) != 0U;

	return 0;
}

static void syna_sdio_device_dma_start(
	const struct device *dev, struct sdio_device_dma_descriptor *desc,
	uintptr_t regs_offset)
{
	const struct syna_sdio_device_config *config = dev->config;
	uint32_t control = SDIO_DMA_XMT_RCV_EN | SDIO_DMA_CTRL_BURST_LEN;
	uint32_t desc_cnt = syna_sdio_device_desc_count(desc);

	sys_write32((uint32_t)(uintptr_t)desc,
		    syna_sdio_device_dma_reg(
			    config, regs_offset +
				    offsetof(struct syna_sdio_device_dma_regs, addrlow)));
	sys_write32(0U,
		    syna_sdio_device_dma_reg(
			    config, regs_offset +
				    offsetof(struct syna_sdio_device_dma_regs, addrhigh)));
	sys_write32(control,
		    syna_sdio_device_dma_reg(
			    config, regs_offset +
				    offsetof(struct syna_sdio_device_dma_regs, control)));
	sys_write32((uint16_t)((uintptr_t)desc +
				(desc_cnt * sizeof(struct sdio_device_dma_descriptor))),
		    syna_sdio_device_dma_reg(
			    config, regs_offset +
				    offsetof(struct syna_sdio_device_dma_regs, ptr)));
}

static int syna_sdio_device_tx_submit(
	const struct device *dev, enum sdio_device_func func,
	struct sdio_device_dma_descriptor *desc)
{
	uintptr_t regs_offset;
	int ret;

	if (desc == NULL) {
		return -EINVAL;
	}

	ret = syna_sdio_device_func_dma_regs(func, true, &regs_offset);
	if (ret < 0) {
		return ret;
	}

	syna_sdio_device_dma_start(dev, desc, regs_offset);
	return 0;
}

static int syna_sdio_device_tx_cancel(const struct device *dev,
				      enum sdio_device_func func)
{
	const struct syna_sdio_device_config *config = dev->config;
	uintptr_t regs_offset;
	int ret;

	ret = syna_sdio_device_func_dma_regs(func, true, &regs_offset);
	if (ret < 0) {
		return ret;
	}

	sys_write32(0U,
		    syna_sdio_device_dma_reg(
			    config, regs_offset +
				    offsetof(struct syna_sdio_device_dma_regs, control)));

	return 0;
}

static int syna_sdio_device_rx_submit(
	const struct device *dev, enum sdio_device_func func,
	struct sdio_device_dma_descriptor *desc)
{
	uintptr_t regs_offset;
	int ret;

	if (desc == NULL) {
		return -EINVAL;
	}

	ret = syna_sdio_device_func_dma_regs(func, false, &regs_offset);
	if (ret < 0) {
		return ret;
	}

	syna_sdio_device_dma_start(dev, desc, regs_offset);
	return 0;
}

static int syna_sdio_device_rx_cancel(const struct device *dev,
				      enum sdio_device_func func)
{
	const struct syna_sdio_device_config *config = dev->config;
	uintptr_t regs_offset;
	int ret;

	ret = syna_sdio_device_func_dma_regs(func, false, &regs_offset);
	if (ret < 0) {
		return ret;
	}

	sys_write32(0U,
		    syna_sdio_device_dma_reg(
			    config, regs_offset +
				    offsetof(struct syna_sdio_device_dma_regs, control)));

	return 0;
}

static uint32_t syna_sdio_device_dma_status(const struct device *dev,
					    uintptr_t regs_offset)
{
	const struct syna_sdio_device_config *config = dev->config;
	uint32_t status;

	status = sys_read32(syna_sdio_device_dma_reg(
		config, regs_offset +
			offsetof(struct syna_sdio_device_dma_regs, status0)));

	return FIELD_GET(SDIO_DMA_STATUS_MASK << SDIO_DMA_STATUS_SHIFT, status);
}

static uint32_t syna_sdio_device_dma_error(const struct device *dev,
					   uintptr_t regs_offset)
{
	const struct syna_sdio_device_config *config = dev->config;
	uint32_t error;

	error = sys_read32(syna_sdio_device_dma_reg(
		config, regs_offset +
			offsetof(struct syna_sdio_device_dma_regs, status1)));

	return FIELD_GET(SDIO_DMA_ERROR_MASK << SDIO_DMA_ERROR_SHIFT, error);
}

static int syna_sdio_device_get_rx_status(const struct device *dev,
					  enum sdio_device_func func,
					  uint32_t *status)
{
	uintptr_t regs_offset;
	int ret;

	if (status == NULL) {
		return -EINVAL;
	}

	ret = syna_sdio_device_func_dma_regs(func, false, &regs_offset);
	if (ret < 0) {
		return ret;
	}

	*status = syna_sdio_device_dma_status(dev, regs_offset);
	return 0;
}

static int syna_sdio_device_get_tx_status(const struct device *dev,
					  enum sdio_device_func func,
					  uint32_t *status)
{
	uintptr_t regs_offset;
	int ret;

	if (status == NULL) {
		return -EINVAL;
	}

	ret = syna_sdio_device_func_dma_regs(func, true, &regs_offset);
	if (ret < 0) {
		return ret;
	}

	*status = syna_sdio_device_dma_status(dev, regs_offset);
	return 0;
}

static int syna_sdio_device_get_rx_error(const struct device *dev,
					 enum sdio_device_func func,
					 uint32_t *error)
{
	uintptr_t regs_offset;
	int ret;

	if (error == NULL) {
		return -EINVAL;
	}

	ret = syna_sdio_device_func_dma_regs(func, false, &regs_offset);
	if (ret < 0) {
		return ret;
	}

	*error = syna_sdio_device_dma_error(dev, regs_offset);
	return 0;
}

static int syna_sdio_device_get_tx_error(const struct device *dev,
					 enum sdio_device_func func,
					 uint32_t *error)
{
	uintptr_t regs_offset;
	int ret;

	if (error == NULL) {
		return -EINVAL;
	}

	ret = syna_sdio_device_func_dma_regs(func, true, &regs_offset);
	if (ret < 0) {
		return ret;
	}

	*error = syna_sdio_device_dma_error(dev, regs_offset);
	return 0;
}

static int syna_sdio_device_init(const struct device *dev)
{
	const struct syna_sdio_device_config *config = dev->config;
	int ret;

	if (!device_is_ready(config->reset.dev)) {
		return -ENODEV;
	}

	ret = reset_line_deassert_dt(&config->reset);
	if (ret < 0) {
		return ret;
	}

	if (config->pcfg != NULL) {
		ret = pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

static DEVICE_API(sdio_device, syna_sdio_device_api) = {
	.func_ready = syna_sdio_device_func_ready,
	.func_not_ready = syna_sdio_device_func_not_ready,
	.func_is_enabled = syna_sdio_device_func_is_enabled,
	.tx_submit = syna_sdio_device_tx_submit,
	.tx_cancel = syna_sdio_device_tx_cancel,
	.rx_submit = syna_sdio_device_rx_submit,
	.rx_cancel = syna_sdio_device_rx_cancel,
	.get_rx_status = syna_sdio_device_get_rx_status,
	.get_tx_status = syna_sdio_device_get_tx_status,
	.get_rx_error = syna_sdio_device_get_rx_error,
	.get_tx_error = syna_sdio_device_get_tx_error,
	.get_irq_status = syna_sdio_device_get_irq_status,
};

#define SYNA_SDIO_DEVICE_PINCTRL_DEFINE(n)                                      \
	COND_CODE_1(DT_INST_PINCTRL_HAS_NAME(n, default),                         \
		    (PINCTRL_DT_INST_DEFINE(n)), ())

#define SYNA_SDIO_DEVICE_PINCTRL_GET(n)                                         \
	COND_CODE_1(DT_INST_PINCTRL_HAS_NAME(n, default),                         \
		    (PINCTRL_DT_INST_DEV_CONFIG_GET(n)), (NULL))

#define SYNA_SDIO_DEVICE_INIT(n)                                                \
	SYNA_SDIO_DEVICE_PINCTRL_DEFINE(n);                                      \
	static const struct syna_sdio_device_config syna_sdio_device_config_##n = { \
		.base = DT_INST_REG_ADDR(n),                                   \
		.reset = RESET_DT_SPEC_INST_GET(n),                            \
		.pcfg = SYNA_SDIO_DEVICE_PINCTRL_GET(n),                       \
	};                                                                      \
	DEVICE_DT_INST_DEFINE(n, syna_sdio_device_init, NULL, NULL,            \
			      &syna_sdio_device_config_##n, POST_KERNEL,       \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE,                 \
			      &syna_sdio_device_api);

DT_INST_FOREACH_STATUS_OKAY(SYNA_SDIO_DEVICE_INIT)
