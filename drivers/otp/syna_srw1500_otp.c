/*
 * Copyright (c) 2026 Synaptics Incorporated
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT syna_srw1500_otp

#include <errno.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/otp.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/math_extras.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define SYNA_SRW1500_OTP_REG_MASK                    0x1488U
#define SYNA_SRW1500_OTP_REG_PASSWORD0               0x14a0U
#define SYNA_SRW1500_OTP_REG_PASSWORD_MATCH          0x14b0U
#define SYNA_SRW1500_OTP_REG_PASSWORD_VALID          0x14b4U
#define SYNA_SRW1500_OTP_REG_INT_STATUS              0x14bcU

#define SYNA_SRW1500_OTP_STATUS_COPY_DONE            BIT(0)
#define SYNA_SRW1500_OTP_STATUS_PROGRAM_DONE         BIT(4)
#define SYNA_SRW1500_OTP_STATUS_PROGRAM_FAIL         BIT(7)

#define SYNA_SRW1500_OTP_STATUS_RETRIES              2000U
#define SYNA_SRW1500_OTP_PASSWORD_RETRIES            3U

#if defined(CONFIG_OTP_PROGRAM)
static const uint32_t syna_srw1500_otp_password[] = {
	0x7ddc55f7U, 0x168e3172U, 0x61cb04a6U, 0xbe15afcfU,
};

K_MUTEX_DEFINE(syna_srw1500_otp_lock);
#endif

struct syna_srw1500_otp_config {
	uintptr_t base;
	size_t size;
};

static int syna_srw1500_otp_read(const struct device *dev, off_t offset, void *data, size_t len)
{
	const struct syna_srw1500_otp_config *config = dev->config;
	uint8_t *out = data;
	size_t end;

	if (offset < 0 || data == NULL ||
	    size_add_overflow((size_t)offset, len, &end) || end > config->size) {
		return -EINVAL;
	}

	for (size_t i = 0; i < len; i++) {
		uintptr_t addr = config->base + (uintptr_t)offset + i;
		uint32_t word = sys_read32(addr & ~(uintptr_t)0x3U);

		out[i] = (uint8_t)(word >> ((addr & 0x3U) * 8U));
	}

	return 0;
}

#if defined(CONFIG_OTP_PROGRAM)
static int syna_srw1500_otp_wait_status(uintptr_t status_reg, uint32_t done)
{
	for (uint32_t retry = 0; retry < SYNA_SRW1500_OTP_STATUS_RETRIES; retry++) {
		uint32_t status = sys_read32(status_reg);

		if ((status & SYNA_SRW1500_OTP_STATUS_PROGRAM_FAIL) != 0U) {
			return -EIO;
		}
		if ((status & done) != 0U) {
			return 0;
		}
		k_busy_wait(1U);
	}

	return -ETIMEDOUT;
}

static int syna_srw1500_otp_unlock(uintptr_t base)
{
	if ((sys_read32(base + SYNA_SRW1500_OTP_REG_PASSWORD_VALID) & 1U) != 0U &&
	    (sys_read32(base + SYNA_SRW1500_OTP_REG_PASSWORD_MATCH) & 1U) != 0U) {
		return 0;
	}

	for (size_t i = 0; i < ARRAY_SIZE(syna_srw1500_otp_password); i++) {
		sys_write32(syna_srw1500_otp_password[i],
			    base + SYNA_SRW1500_OTP_REG_PASSWORD0 + (i * 4U));
	}
	sys_write32(1U, base + SYNA_SRW1500_OTP_REG_PASSWORD_VALID);

	for (uint32_t retry = 0; retry < SYNA_SRW1500_OTP_PASSWORD_RETRIES; retry++) {
		if ((sys_read32(base + SYNA_SRW1500_OTP_REG_PASSWORD_MATCH) & 1U) != 0U) {
			return 0;
		}
		k_busy_wait(SYNA_SRW1500_OTP_STATUS_RETRIES);
	}

	return -EACCES;
}

static int syna_srw1500_otp_program(const struct device *dev, off_t offset, const void *data,
					size_t len)
{
	const struct syna_srw1500_otp_config *config = dev->config;
	const uint8_t *in = data;
	size_t end;
	int ret;

	if (offset < 0 || data == NULL || len == 0U ||
	    (((size_t)offset | len) & (sizeof(uint32_t) - 1U)) != 0U ||
	    size_add_overflow((size_t)offset, len, &end) || end > config->size) {
		return -EINVAL;
	}

	k_mutex_lock(&syna_srw1500_otp_lock, K_FOREVER);

	ret = syna_srw1500_otp_unlock(config->base);
	if (ret != 0) {
		goto out;
	}

	ret = syna_srw1500_otp_wait_status(config->base + SYNA_SRW1500_OTP_REG_INT_STATUS,
					   SYNA_SRW1500_OTP_STATUS_COPY_DONE);
	if (ret != 0) {
		goto out;
	}

	for (size_t i = 0; i < len; i += sizeof(uint32_t)) {
		uintptr_t address = config->base + (uintptr_t)offset + i;
		uint32_t requested = sys_get_le32(&in[i]);
		uint32_t current = sys_read32(address);

		if ((requested & current) != current) {
			ret = -EROFS;
			goto out;
		}
		if (requested == current) {
			continue;
		}

		sys_write32(0U, config->base + SYNA_SRW1500_OTP_REG_MASK);
		sys_write32(requested, address);

		ret = syna_srw1500_otp_wait_status(config->base + SYNA_SRW1500_OTP_REG_INT_STATUS,
					   SYNA_SRW1500_OTP_STATUS_PROGRAM_DONE);
		sys_write32(SYNA_SRW1500_OTP_STATUS_PROGRAM_DONE,
			    config->base + SYNA_SRW1500_OTP_REG_INT_STATUS);
		if (ret != 0) {
			goto out;
		}
		if (sys_read32(address) != requested) {
			ret = -EIO;
			goto out;
		}
	}
	ret = 0;

out:
	k_mutex_unlock(&syna_srw1500_otp_lock);
	return ret;
}
#endif

static DEVICE_API(otp, syna_srw1500_otp_api) = {
	.read = syna_srw1500_otp_read,
#if defined(CONFIG_OTP_PROGRAM)
	.program = syna_srw1500_otp_program,
#endif
};

#define SYNA_SRW1500_OTP_INIT(inst)                                                \
	static const struct syna_srw1500_otp_config syna_srw1500_otp_config_##inst = { \
		.base = DT_INST_REG_ADDR(inst),                                        \
		.size = DT_INST_PROP(inst, otp_size),                                  \
	};                                                                             \
	DEVICE_DT_INST_DEFINE(inst, NULL, NULL, NULL, &syna_srw1500_otp_config_##inst, \
			      PRE_KERNEL_1, CONFIG_OTP_INIT_PRIORITY,                    \
			      &syna_srw1500_otp_api);

DT_INST_FOREACH_STATUS_OKAY(SYNA_SRW1500_OTP_INIT)
