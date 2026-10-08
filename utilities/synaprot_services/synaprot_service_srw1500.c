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
#include "synaprot_service_srw1500.h"
#include <zephyr/sys/barrier.h>

#include <zephyr/logging/log.h>

#define DT_DRV_COMPAT syna_synaprot_services
LOG_MODULE_REGISTER(synaprot_services, CONFIG_MBOX_LOG_LEVEL);

int syna_image_verify_signature(const uint8_t *pubkey, size_t pubkey_len,
				const uint8_t *hash, size_t hash_len,
				const uint8_t *sig, size_t sig_len)
{
	struct mailbox_command cmd = { 0 };
	unsigned int data[MAX_MAILBOX_DATA_LEN] = { 0 };
	uint32_t response_words;
	const struct device *dev;
	struct mbox_msg msg;
	int ret;
	size_t i;
	size_t msg_words;
	size_t msg_size;
    const struct mbox_driver_api *api;

	dev = DEVICE_DT_GET(DT_CHOSEN(sprot_mbox));
	api = (const struct mbox_driver_api *)dev->api;

	if (!device_is_ready(dev)) {
		LOG_ERR("Secure mailbox device is not ready");
		return -ENODEV;
	}

	if ((hash == NULL) || (sig == NULL)) {
		LOG_ERR("Invalid hash/signature pointer");
		return -EINVAL;
	}

	if ((hash_len == 0U) || (sig_len == 0U)) {
		LOG_ERR("Invalid hash/signature length");
		return -EINVAL;
	}

	ARG_UNUSED(pubkey);
	ARG_UNUSED(pubkey_len);

	if (5U > MAX_MAILBOX_PARAM_NUM) {
		LOG_ERR("Mailbox parameter count exceeds limit");
		return -EINVAL;
	}

	cmd.command = MAILBOX_COMMAND(
		HOST2SYNA_SENDER_M52,
		MAILBOX_TEE,
		HOST2SYNA_CMD_SOC,
		SPI_PI_VERIFY_SIGNATURE);

	cmd.param_num = 5U;

	cmd.param[0] = IMAGE_TYPE_SM;
	cmd.param[1] = SHA_CMD_ALGORITHM_SHA256;
	cmd.param[2] = (uint32_t)(uintptr_t)sig;
	cmd.param[3] = (uint32_t)(uintptr_t)hash;
	cmd.param[4] = 0U;

	msg_words = 1U + cmd.param_num;

	if (msg_words > MAX_MAILBOX_DATA_LEN) {
		LOG_ERR("Mailbox data buffer too small");
		return -ENOMEM;
	}

	data[0] = cmd.command;

	for (i = 0; i < cmd.param_num; i++) {
		data[i + 1U] = cmd.param[i];
	}

	msg_size = msg_words * sizeof(data[0]);

	msg.data = data;
	msg.size = msg_size;

	LOG_DBG("Sending signature verification request");
	LOG_DBG("sig=%p hash=%p", sig, hash);
	LOG_DBG("Mailbox command: 0x%x", cmd.command);

	ret = api->send(dev, MAILBOX_TEE, &msg);
	if (ret != 0) {
		LOG_ERR("Failed to send mailbox command: %d", ret);
		return ret;
	}

	response_words = MAX_MAILBOX_DATA_LEN;
	ret = mbox_synaprot_poll_response(dev, data, &response_words,
						       K_MSEC(1000));

	if (ret < 0) {
		LOG_ERR("Secure Island response failed: %d", ret);
		return ret;
	}

	if (response_words < 2U) {
		LOG_ERR("Incomplete mailbox response");
		return -EIO;
	}

	LOG_DBG("Secure Island response: 0x%x", data[1]);
	if (data[1] != 0U) {
		LOG_ERR("Signature verification FAILED: 0x%x",
			data[1]);

		return -1;
	}

	LOG_INF("Signature verification PASSED");

	return 0;
}
