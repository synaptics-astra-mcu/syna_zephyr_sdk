/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT syna_mbox_sr100

#include <stdint.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/irq.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/sys/barrier.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(mbox_syna_sr100, CONFIG_MBOX_LOG_LEVEL);

#define SYNA_MBOX_LOCK_FREE_VAL	0xffffffff
#define SYNA_MBOX_MAX_CHANNELS	8
#define SYNA_MBOX_CH_OFFSET	16

#define SYNA_MBOX_M55		0
#define SYNA_MBOX_M4		1

#define SYNA_MBOX_CHANNEL_M4(data, channel) (data->base + (((channel) + SYNA_MBOX_CH_OFFSET) * 4))
#define SYNA_MBOX_CHANNEL_M55(data, channel) (data->base + ((channel) * 4))

struct syna_mbox_config {
	void (*irq_config_func)(const struct device *dev);
};

struct syna_mbox_data {
	mbox_callback_t cb[SYNA_MBOX_MAX_CHANNELS];
	void *user_data[SYNA_MBOX_MAX_CHANNELS];
	uint32_t this_core_id;
	uint32_t other_core_id;
	uint32_t shm_size;
	uint8_t *m55_cpu_shm;
	uint8_t *m4_cpu_shm;
	uintptr_t base;
	uintptr_t lock;
	uint32_t channels;
};

static bool syna_mbox_lock_try_take(uintptr_t lock_addr, uint32_t owner)
{
	/* LDREX/STREX (used by atomic operations) are illegal on device memory. */
	barrier_dmem_fence_full();
	if (sys_read32(lock_addr) != SYNA_MBOX_LOCK_FREE_VAL) {
		return false;
	}
	sys_write32(owner, lock_addr);
	barrier_dmem_fence_full();

	/* Verify we actually won the lock (guards against simultaneous writes) */
	return sys_read32(lock_addr) == owner;
}

static void syna_mbox_lock_release(uintptr_t lock_addr)
{
	barrier_dmem_fence_full();
	sys_write32(SYNA_MBOX_LOCK_FREE_VAL, lock_addr);
	barrier_dmem_fence_full();
}

static void syna_mbox_isr(const struct device *dev, int channel)
{
	struct syna_mbox_data *dev_data = (struct syna_mbox_data *)dev->data;
	struct mbox_msg msg;
	k_timepoint_t end;

	/* Clear the trigger interrupt */
	if (dev_data->this_core_id == SYNA_MBOX_M55) {
		sys_write32(0, SYNA_MBOX_CHANNEL_M55(dev_data, channel));
	} else {
		sys_write32(0, SYNA_MBOX_CHANNEL_M4(dev_data, channel));
	}

	end = sys_timepoint_calc(K_MSEC(1)); /* do not stay in ISR for too long */
	/* Take the ownership of the shared memory */
	do {
		if (sys_timepoint_expired(end)) {
			return;
		}
	} while (!syna_mbox_lock_try_take(dev_data->lock, dev_data->this_core_id));

	if (dev_data->cb[channel]) {
		msg.data = (dev_data->this_core_id == SYNA_MBOX_M55) ?
			   (const void *)dev_data->m55_cpu_shm :
			   (const void *)dev_data->m4_cpu_shm;
		msg.size = dev_data->shm_size;

		dev_data->cb[channel](dev, dev_data->other_core_id, dev_data->user_data[channel],
				      &msg);
	}

	/* Unlock the shared memory */
	syna_mbox_lock_release(dev_data->lock);
}

static int syna_mbox_send(const struct device *dev, mbox_channel_id_t channel,
			  const struct mbox_msg *msg)
{
	struct syna_mbox_data *dev_data = (struct syna_mbox_data *)dev->data;
	uint32_t mtu = dev_data->shm_size;

	if (channel >= dev_data->channels) {
		LOG_ERR("Invalid channel");
		return -EINVAL;
	}

	if (msg != NULL && msg->data != NULL && msg->size > mtu) {
		LOG_ERR("Message size %d exceeds shared memory region %d", msg->size, mtu);
		return -EMSGSIZE;
	}

	/* Try to lock the shared memory */
	while (!syna_mbox_lock_try_take(dev_data->lock, dev_data->this_core_id)) {
		k_usleep(10);
	}

	/* Copy data into the other core's receive region */
	if (msg != NULL && msg->data != NULL) {
		uint8_t *dest = (dev_data->other_core_id == SYNA_MBOX_M55) ? dev_data->m55_cpu_shm
									   : dev_data->m4_cpu_shm;
		memcpy(dest, msg->data, msg->size);
	}

	syna_mbox_lock_release(dev_data->lock);

	/* Generate interrupt in the remote core */
	if (dev_data->this_core_id == SYNA_MBOX_M55) {
		LOG_DBG("Generating interrupt on remote CPU 1 from CPU 0");
		sys_write32(1, SYNA_MBOX_CHANNEL_M4(dev_data, channel));
	} else {
		LOG_DBG("Generating interrupt on remote CPU 0 from CPU 1");
		sys_write32(1, SYNA_MBOX_CHANNEL_M55(dev_data, channel));
	}

	return 0;
}

static int syna_mbox_register_callback(const struct device *dev, mbox_channel_id_t channel,
					mbox_callback_t cb, void *user_data)
{
	struct syna_mbox_data *dev_data = (struct syna_mbox_data *)dev->data;
	uint32_t key;

	if (channel >= dev_data->channels) {
		LOG_ERR("Invalid channel");
		return -EINVAL;
	}

	if (!cb) {
		LOG_ERR("Must provide callback");
		return -EINVAL;
	}

	key = irq_lock();

	dev_data->cb[channel] = cb;
	dev_data->user_data[channel] = user_data;

	irq_unlock(key);

	return 0;
}

static int syna_mbox_mtu_get(const struct device *dev)
{
	struct syna_mbox_data *data = (struct syna_mbox_data *)dev->data;

	return data->shm_size;
}

static uint32_t syna_mbox_max_channels_get(const struct device *dev)
{
	struct syna_mbox_data *data = (struct syna_mbox_data *)dev->data;

	return data->channels;
}

static int syna_mbox_set_enabled(const struct device *dev, mbox_channel_id_t channel, bool enable)
{
	/* The synaptics mbox is always enabled. */

	ARG_UNUSED(dev);
	ARG_UNUSED(enable);
	ARG_UNUSED(channel);

	return 0;
}

static int syna_mbox_init(const struct device *dev)
{
	struct syna_mbox_data *data = (struct syna_mbox_data *)dev->data;
	struct syna_mbox_config *cfg = (struct syna_mbox_config *)dev->config;
	k_timepoint_t end;

	if (data->channels > SYNA_MBOX_MAX_CHANNELS) {
		return -EINVAL;
	}

#if defined(CONFIG_SOC_SR100_M4)
	data->this_core_id = SYNA_MBOX_M4;
#else
	data->this_core_id = SYNA_MBOX_M55;
#endif
	data->other_core_id = (data->this_core_id == SYNA_MBOX_M55) ? SYNA_MBOX_M4 : SYNA_MBOX_M55;

	/* M55 is responsible to initialize the lock of the shared memory */
	if (data->this_core_id == SYNA_MBOX_M55) {
		syna_mbox_lock_release(data->lock);
	} else {
		/* M4 waits for the initialization from M55, takes the lock & releases it */
		LOG_DBG("Waiting for M55 to sync");
		end = sys_timepoint_calc(K_MSEC(100));
		do {
			k_busy_wait(10);
			if (sys_timepoint_expired(end)) {
				return -ETIMEDOUT;
			}
		} while (!syna_mbox_lock_try_take(data->lock, data->this_core_id));

		syna_mbox_lock_release(data->lock);
		LOG_DBG("Synchronization done");
	}

	cfg->irq_config_func(dev);

	return 0;
}

static DEVICE_API(mbox, syna_mbox_driver_api) = {
	.send = syna_mbox_send,
	.register_callback = syna_mbox_register_callback,
	.mtu_get = syna_mbox_mtu_get,
	.max_channels_get = syna_mbox_max_channels_get,
	.set_enabled = syna_mbox_set_enabled,
};

#define SYNA_MBOX_IRQ_DEFINE(n, inst)                                                             \
	static void syna_mbox_##inst##_##n##_isr(const struct device *dev)                        \
	{                                                                                         \
		syna_mbox_isr(dev, n);                                                            \
	}
#define SYNA_MBOX_IRQ_CONNECT(n, inst)                                                            \
	IRQ_CONNECT(DT_INST_IRQ_BY_IDX(inst, n, irq), DT_INST_IRQ_BY_IDX(inst, n, priority),      \
	            syna_mbox_##inst##_##n##_isr, DEVICE_DT_INST_GET(inst), 0);                   \
	irq_enable(DT_INST_IRQ_BY_IDX(inst, n, irq));

#define SYNA_MBOX_SHM_SIZE_BY_IDX(inst) DT_INST_PROP(inst, shared_memory_size)
#define SYNA_MBOX_SHM_ADDR_BY_IDX(inst) DT_REG_ADDR(DT_PHANDLE(DT_DRV_INST(inst), shared_memory))

#define SYNA_MBOX_INIT(inst)                                                                      \
	LISTIFY(DT_NUM_IRQS(DT_DRV_INST(inst)), SYNA_MBOX_IRQ_DEFINE, (), inst)                   \
	static void syna_mbox_##inst##_irq_config_func(const struct device *dev)                  \
	{                                                                                         \
		LISTIFY(DT_NUM_IRQS(DT_DRV_INST(inst)), SYNA_MBOX_IRQ_CONNECT, (), inst);         \
	}                                                                                         \
	static struct syna_mbox_config syna_mbox_device_cfg_##inst = {                            \
		.irq_config_func = syna_mbox_##inst##_irq_config_func,                            \
	};                                                                                        \
	static struct syna_mbox_data syna_mbox_device_data_##inst = {                             \
		.shm_size = SYNA_MBOX_SHM_SIZE_BY_IDX(inst) / 2,                                  \
		.m55_cpu_shm = (uint8_t *)SYNA_MBOX_SHM_ADDR_BY_IDX(inst),                        \
		.m4_cpu_shm = (uint8_t *)SYNA_MBOX_SHM_ADDR_BY_IDX(inst) +                        \
					(SYNA_MBOX_SHM_SIZE_BY_IDX(inst) / 2),                    \
		.base = DT_INST_REG_ADDR_BY_IDX(inst, 0),                                         \
		.lock = DT_INST_REG_ADDR_BY_IDX(inst, 1),                                         \
		.channels = DT_NUM_IRQS(DT_DRV_INST(inst)),                                       \
	};                                                                                        \
	DEVICE_DT_INST_DEFINE(inst, &syna_mbox_init, NULL, &syna_mbox_device_data_##inst,         \
			      &syna_mbox_device_cfg_##inst, PRE_KERNEL_2,                         \
			      CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &syna_mbox_driver_api);

DT_INST_FOREACH_STATUS_OKAY(SYNA_MBOX_INIT)
