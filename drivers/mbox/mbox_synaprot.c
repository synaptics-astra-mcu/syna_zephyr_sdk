/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/mbox.h>
#include "mbox_synaprot.h"
#include <zephyr/sys/barrier.h>

#include <zephyr/logging/log.h>

#define DT_DRV_COMPAT syna_synaprot_mbox
LOG_MODULE_REGISTER(synaprot_mbox, CONFIG_MBOX_LOG_LEVEL);

#define SYNA_MBOX_ROLE_BASE_REE        0x00
#define SYNA_MBOX_ROLE_BASE_TEE        0x20

#define SYNA_MBOX_S2H_CSR_OFFSET       0x00
#define SYNA_MBOX_S2H_CSR_HOST_OFFSET  0x04
#define SYNA_MBOX_S2H_DATA_OFFSET      0x08
#define SYNA_MBOX_H2S_CSR_OFFSET       0x0C
#define SYNA_MBOX_H2S_DATA_OFFSET      0x14

/* CSR field layout is shared by the H2S and S2H control/status registers;
 * only the meaning of bit 8 differs by direction (FULL for S2H, EMPTY for H2S).
 */
#define SYNA_MBOX_CSR_LENGTH_POS       0
#define SYNA_MBOX_CSR_LENGTH_WIDTH     3
#define SYNA_MBOX_CSR_LENGTH_MASK      (0x7 << SYNA_MBOX_CSR_LENGTH_POS)
#define SYNA_MBOX_CSR_FULL_POS         8
#define SYNA_MBOX_CSR_FULL_MASK        BIT(SYNA_MBOX_CSR_FULL_POS)
#define SYNA_MBOX_CSR_EMPTY_POS        9
#define SYNA_MBOX_CSR_EMPTY_MASK       BIT(SYNA_MBOX_CSR_EMPTY_POS)

#define SYNA_MBOX_MAX_WORDS            (SYNA_MBOX_CSR_LENGTH_MASK + 1)

/* MCU_SYSCTRL interface lock, arbitrates access to the mailbox block. */
#define SYNA_MBOX_LOCK_CTRL_OFFSET     0x8C
#define SYNA_MBOX_LOCK_CTRL_LOCK_POS   0
#define SYNA_MBOX_LOCK_STAT_OFFSET     0x90
#define SYNA_MBOX_LOCK_STAT_LOCKED_POS 4
#define SYNA_MBOX_LOCK_STAT_LOCKED_MASK BIT(SYNA_MBOX_LOCK_STAT_LOCKED_POS)

#define SYNA_MBOX_LOCK_POLL_STEP_US    10
#define SYNA_MBOX_STATUS_SUCCESS       0

struct syna_mbox_config {
	uintptr_t mbox_base;
	uintptr_t lock_base;
	uint32_t role_base;
	uint32_t channels;
};

struct syna_mbox_data {
	mbox_callback_t cb;
	void *user_data;
};

static bool syna_mbox_lock_is_held(const struct syna_mbox_config *cfg)
{
	return sys_read32(cfg->lock_base + SYNA_MBOX_LOCK_STAT_OFFSET) &
	       SYNA_MBOX_LOCK_STAT_LOCKED_MASK;
}

static int syna_mbox_lock_take(const struct syna_mbox_config *cfg, k_timeout_t timeout)
{
	k_timepoint_t end = sys_timepoint_calc(timeout);

	if (syna_mbox_lock_is_held(cfg)) {
		return -EBUSY;
	}

	sys_write32(BIT(SYNA_MBOX_LOCK_CTRL_LOCK_POS),
		    cfg->lock_base + SYNA_MBOX_LOCK_CTRL_OFFSET);
	barrier_dmem_fence_full();

	while (!syna_mbox_lock_is_held(cfg)) {
		if (sys_timepoint_expired(end)) {
			return -ETIMEDOUT;
		}
		k_busy_wait(SYNA_MBOX_LOCK_POLL_STEP_US);
	}

	return 0;
}

static void syna_mbox_lock_release(const struct syna_mbox_config *cfg)
{
	if (syna_mbox_lock_is_held(cfg)) {
		sys_write32(0, cfg->lock_base + SYNA_MBOX_LOCK_CTRL_OFFSET);
		barrier_dmem_fence_full();
	}
}

/* H2S: host (REE) -> secure co-processor. */
static int syna_mbox_send_request(const struct device *dev, mbox_channel_id_t channel,
				  const struct mbox_msg *msg, bool retain_lock_for_response)
{
	const struct syna_mbox_config *cfg = dev->config;
	const uint32_t *words;
	uint32_t nwords;
	k_timepoint_t end;
	int rc;

	if (msg == NULL || msg->data == NULL ||
		(msg->size % sizeof(uint32_t)) != 0U) {
		return -EINVAL;
	}

	nwords = msg ? (msg->size + sizeof(uint32_t) - 1) / sizeof(uint32_t) : 0;

	if (nwords == 0 || nwords > SYNA_MBOX_MAX_WORDS) {
		LOG_ERR("Invalid message length %u words", nwords);
		return -EMSGSIZE;
	}

	words = msg->data;

	if (channel >= cfg->channels) {
		LOG_ERR("Invalid channel");
		return -EINVAL;
	}

	rc = syna_mbox_lock_take(cfg, K_MSEC(CONFIG_SYNAPROT_MBOX_LOCK_TIMEOUT_MS));
	if (rc != 0) {
		LOG_ERR("Failed to take interface lock: %d", rc);
		return rc;
	}

	sys_write32(nwords - 1, cfg->mbox_base + cfg->role_base + SYNA_MBOX_H2S_CSR_OFFSET);
	for (uint32_t i = 0; i < nwords; i++) {
		sys_write32(words[i], cfg->mbox_base + cfg->role_base + SYNA_MBOX_H2S_DATA_OFFSET);
	}

	/* Wait for the co-processor to consume the message (H2S CSR bit 9, "empty"). */
	end = sys_timepoint_calc(K_USEC(CONFIG_SYNAPROT_MBOX_TX_TIMEOUT_US));
	do {
		uint32_t status = sys_read32(cfg->mbox_base + cfg->role_base + SYNA_MBOX_H2S_CSR_OFFSET);

		if (status & SYNA_MBOX_CSR_EMPTY_MASK) {
			sys_write32(0, cfg->mbox_base + cfg->role_base + SYNA_MBOX_H2S_CSR_OFFSET);
			return 0;
		}
	} while (!sys_timepoint_expired(end));
	syna_mbox_lock_release(cfg);
	return -ETIMEDOUT;
}

static int syna_mbox_send(const struct device *dev, mbox_channel_id_t channel,
			  const struct mbox_msg *msg)
{
	return syna_mbox_send_request(dev, channel, msg, true);
}

/* S2H: secure co-processor -> host (REE). Polled; there is no IRQ for this block.
 * Public API (mbox_synaprot.h): Zephyr's generic mbox.h has no polled-receive
 * primitive, so callers needing a synchronous request/response (e.g. the MCUboot
 * secure-validation porting layer) call this directly instead of register_callback().
 */
int mbox_synaprot_poll_response(const struct device *dev, uint32_t *data,
				     uint32_t *nwords, k_timeout_t timeout)
{
	const struct syna_mbox_config *cfg = dev->config;
	k_timepoint_t end = sys_timepoint_calc(timeout);
	uint32_t status;
	uint32_t len;

	if (data == NULL || nwords == NULL) {
		syna_mbox_lock_release(cfg);
		return -EINVAL;
	}

	do {
		status = sys_read32(cfg->mbox_base + cfg->role_base + SYNA_MBOX_S2H_CSR_HOST_OFFSET);
		if (status & SYNA_MBOX_CSR_FULL_MASK) {
			break;
		}
		if (sys_timepoint_expired(end)) {
			syna_mbox_lock_release(cfg);
			return -ETIMEDOUT;
		}
	} while (true);

	len = (status & SYNA_MBOX_CSR_LENGTH_MASK) + 1;
	if (len > SYNA_MBOX_MAX_WORDS) {
		syna_mbox_lock_release(cfg);
		return -EMSGSIZE;
	}

	for (uint32_t i = 0; i < len; i++) {
		data[i] = sys_read32(cfg->mbox_base + cfg->role_base + SYNA_MBOX_S2H_DATA_OFFSET);
	}

	syna_mbox_lock_release(cfg);

	*nwords = len;

	return (len > 0 && data[0] == SYNA_MBOX_STATUS_SUCCESS) ? 0 : -EIO;
}

static int syna_mbox_register_callback(const struct device *dev, mbox_channel_id_t channel,
					mbox_callback_t cb, void *user_data)
{
	const struct syna_mbox_config *cfg = dev->config;

	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(user_data);

	if (channel >= cfg->channels) {
		return -EINVAL;
	}

	/* The reference implementation is polling-only; no callback can be fired. */
	return -ENOTSUP;
}

static int syna_mbox_mtu_get(const struct device *dev)
{
	ARG_UNUSED(dev);

	return SYNA_MBOX_MAX_WORDS * sizeof(uint32_t);
}

static uint32_t syna_mbox_max_channels_get(const struct device *dev)
{
	const struct syna_mbox_config *cfg = dev->config;

	return cfg->channels;
}

static int syna_mbox_set_enabled(const struct device *dev, mbox_channel_id_t channel, bool enable)
{
	/* The synaprot mailbox is always enabled; nothing to toggle. */
	ARG_UNUSED(dev);
	ARG_UNUSED(channel);
	ARG_UNUSED(enable);

	return 0;
}

static int syna_mbox_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 0;
}

static DEVICE_API(mbox, syna_mbox_driver_api) = {
	.send = syna_mbox_send,
	.register_callback = syna_mbox_register_callback,
	.mtu_get = syna_mbox_mtu_get,
	.max_channels_get = syna_mbox_max_channels_get,
	.set_enabled = syna_mbox_set_enabled,
};

#define SYNA_MBOX_INIT(inst)                                                              \
	static const struct syna_mbox_config syna_mbox_cfg_##inst = {                     \
		.mbox_base = DT_INST_REG_ADDR_BY_NAME(inst, mbox),                        \
		.lock_base = DT_INST_REG_ADDR_BY_NAME(inst, lock),                        \
		.role_base = DT_INST_ENUM_IDX(inst, syna_mailbox_role) == 1 ?             \
			     SYNA_MBOX_ROLE_BASE_TEE : SYNA_MBOX_ROLE_BASE_REE,           \
		.channels = 1,                                                            \
	};                                                                                 \
	static struct syna_mbox_data syna_mbox_data_##inst;                               \
	DEVICE_DT_INST_DEFINE(inst, &syna_mbox_init, NULL, &syna_mbox_data_##inst,        \
			      &syna_mbox_cfg_##inst, PRE_KERNEL_2,                        \
			      CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &syna_mbox_driver_api);

DT_INST_FOREACH_STATUS_OKAY(SYNA_MBOX_INIT)
