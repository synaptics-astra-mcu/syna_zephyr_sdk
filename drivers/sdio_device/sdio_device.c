/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/drivers/sdio_device.h>
#include <zephyr/sys/util.h>

static uint8_t sdio_device_calculate_even_parity_bit(
	struct sdio_device_dma_descriptor *desc)
{
	uint8_t *bytes = (uint8_t *)desc;
	uint8_t acc = 0U;

	desc->parity = 0U;

	for (size_t i = 0; i < sizeof(*desc); i++) {
		acc ^= bytes[i];
	}

	acc ^= acc >> 4;
	acc ^= acc >> 2;
	acc ^= acc >> 1;

	return acc & 1U;
}

int sdio_device_setup_tx_rx_desc(struct sdio_device_dma_descriptor *desc,
				 uint8_t *buf, size_t size)
{
	size_t idx = 0U;
	size_t remaining = size;
	size_t desc_count;

	if ((desc == NULL) || (buf == NULL) || (size == 0U)) {
		return -EINVAL;
	}

	desc_count = DIV_ROUND_UP(size, SDIO_DEVICE_DMA_MAX_XFER_SIZE);
	if (desc_count > SDIO_DEVICE_DMA_MAX_DESCRIPTORS) {
		return -EMSGSIZE;
	}

	for (size_t i = 0U; i < desc_count; i++) {
		size_t xfer_size = MIN(remaining, SDIO_DEVICE_DMA_MAX_XFER_SIZE);

		memset(&desc[i], 0, sizeof(desc[i]));
		desc[i].buf_count = xfer_size;
		desc[i].data_buf_ptr_low = (uint32_t)(uintptr_t)&buf[idx];
		desc[i].data_buf_ptr_high = 0U;
		desc[i].sof = (i == 0U) ? 1U : 0U;

		remaining -= xfer_size;
		idx += xfer_size;
	}

	desc[desc_count - 1U].eof = 1U;
	desc[desc_count - 1U].eot = 1U;
	desc[desc_count - 1U].int_on_comp = 1U;

	for (size_t i = 0U; i < desc_count; i++) {
		desc[i].parity = sdio_device_calculate_even_parity_bit(&desc[i]);
	}

	return 0;
}
