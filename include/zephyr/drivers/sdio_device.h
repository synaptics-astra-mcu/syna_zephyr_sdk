/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_SDIO_DEVICE_H_
#define ZEPHYR_INCLUDE_DRIVERS_SDIO_DEVICE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/toolchain.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SDIO_DEVICE_DMA_MAX_DESCRIPTORS 10U
#define SDIO_DEVICE_DMA_MAX_XFER_SIZE   (32U * 1024U)
#define SDIO_DEVICE_DMA_STATUS_DONE     2U

struct sdio_device_dma_descriptor {
	uint32_t reserved_0_12 : 13;
	uint32_t low_priority : 1;
	uint32_t barrier : 1;
	uint32_t bzero_bcmp : 1;
	uint32_t fixed_burst : 1;
	uint32_t coherent : 1;
	uint32_t not_pcie : 1;
	uint32_t delay_suspend : 1;
	uint32_t flags : 8;
	uint32_t eot : 1;
	uint32_t int_on_comp : 1;
	uint32_t eof : 1;
	uint32_t sof : 1;

	uint32_t buf_count : 16;
	uint32_t addr_ext : 2;
	uint32_t parity : 1;
	uint32_t reserved_51_63 : 13;

	uint32_t data_buf_ptr_low;
	uint32_t data_buf_ptr_high;
} __packed;

enum sdio_device_func {
	SDIO_DEVICE_FUNC_2 = 2,
	SDIO_DEVICE_FUNC_3 = 3,
};

__subsystem struct sdio_device_driver_api {
	/*
	 * Function readiness is controlled by the SDIO device, while function
	 * enablement is controlled by the SDIO host. func_ready() and
	 * func_not_ready() update the device ready indication for a function.
	 * func_is_enabled() reports whether the host has enabled that function.
	 */
	int (*func_ready)(const struct device *dev, enum sdio_device_func func);
	int (*func_not_ready)(const struct device *dev, enum sdio_device_func func);
	int (*func_is_enabled)(const struct device *dev, enum sdio_device_func func,
			       bool *enabled);
	int (*tx_submit)(const struct device *dev, enum sdio_device_func func,
			 struct sdio_device_dma_descriptor *desc);
	int (*tx_cancel)(const struct device *dev, enum sdio_device_func func);
	int (*rx_submit)(const struct device *dev, enum sdio_device_func func,
			 struct sdio_device_dma_descriptor *desc);
	int (*rx_cancel)(const struct device *dev, enum sdio_device_func func);
	int (*get_rx_status)(const struct device *dev, enum sdio_device_func func,
			     uint32_t *status);
	int (*get_tx_status)(const struct device *dev, enum sdio_device_func func,
			     uint32_t *status);
	int (*get_rx_error)(const struct device *dev, enum sdio_device_func func,
			    uint32_t *error);
	int (*get_tx_error)(const struct device *dev, enum sdio_device_func func,
			    uint32_t *error);
	uint32_t (*get_irq_status)(const struct device *dev);
};

static inline int sdio_device_func_ready(const struct device *dev,
					  enum sdio_device_func func)
{
	return DEVICE_API_GET(sdio_device, dev)->func_ready(dev, func);
}

static inline int sdio_device_func_not_ready(const struct device *dev,
					   enum sdio_device_func func)
{
	return DEVICE_API_GET(sdio_device, dev)->func_not_ready(dev, func);
}

static inline int sdio_device_func_is_enabled(const struct device *dev,
					      enum sdio_device_func func,
					      bool *enabled)
{
	return DEVICE_API_GET(sdio_device, dev)->func_is_enabled(dev, func, enabled);
}

static inline int sdio_device_tx_submit(
	const struct device *dev, enum sdio_device_func func,
	struct sdio_device_dma_descriptor *desc)
{
	return DEVICE_API_GET(sdio_device, dev)->tx_submit(dev, func, desc);
}

static inline int sdio_device_tx_cancel(const struct device *dev,
					enum sdio_device_func func)
{
	return DEVICE_API_GET(sdio_device, dev)->tx_cancel(dev, func);
}

static inline int sdio_device_rx_submit(
	const struct device *dev, enum sdio_device_func func,
	struct sdio_device_dma_descriptor *desc)
{
	return DEVICE_API_GET(sdio_device, dev)->rx_submit(dev, func, desc);
}

static inline int sdio_device_rx_cancel(const struct device *dev,
					enum sdio_device_func func)
{
	return DEVICE_API_GET(sdio_device, dev)->rx_cancel(dev, func);
}

static inline int sdio_device_get_rx_status(const struct device *dev,
					    enum sdio_device_func func,
					    uint32_t *status)
{
	return DEVICE_API_GET(sdio_device, dev)->get_rx_status(dev, func, status);
}

static inline int sdio_device_get_tx_status(const struct device *dev,
					    enum sdio_device_func func,
					    uint32_t *status)
{
	return DEVICE_API_GET(sdio_device, dev)->get_tx_status(dev, func, status);
}

static inline int sdio_device_get_rx_error(const struct device *dev,
					   enum sdio_device_func func,
					   uint32_t *error)
{
	return DEVICE_API_GET(sdio_device, dev)->get_rx_error(dev, func, error);
}

static inline int sdio_device_get_tx_error(const struct device *dev,
					   enum sdio_device_func func,
					   uint32_t *error)
{
	return DEVICE_API_GET(sdio_device, dev)->get_tx_error(dev, func, error);
}

static inline uint32_t sdio_device_get_irq_status(const struct device *dev)
{
	return DEVICE_API_GET(sdio_device, dev)->get_irq_status(dev);
}

int sdio_device_setup_tx_rx_desc(struct sdio_device_dma_descriptor *desc,
				 uint8_t *buf, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_SDIO_DEVICE_H_ */
