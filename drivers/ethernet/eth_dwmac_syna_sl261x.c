/*
 * Driver for Synopsys DesignWare MAC
 *
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Synaptics SL261x specific glue.
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(dwmac_plat, CONFIG_ETHERNET_LOG_LEVEL);

#define DT_DRV_COMPAT syna_dwmac_sl261x

#include <sys/types.h>
#include <zephyr/kernel.h>
#include <zephyr/net/ethernet.h>
#include <ethernet/eth.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/sys/crc.h>
#include <zephyr/irq.h>

#include "eth_dwmac_priv.h"

PINCTRL_DT_INST_DEFINE(0);
static const struct pinctrl_dev_config *eth0_pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(0);
static struct net_eth_mac_config mac_cfg = NET_ETH_MAC_DT_INST_CONFIG_INIT(0);

uint32_t phys_hi32(void *addr)
{
	return 0;
}

uint32_t phys_lo32(void *addr)
{
#if defined(CONFIG_DDR)
	return (uintptr_t)addr & 0x0fffffff; /* DDR */
#else
	return (uintptr_t)addr | 0xf0000000; /* NPU */
#endif
}

int dwmac_bus_init(struct dwmac_priv *p)
{
	int ret;

	ret = pinctrl_apply_state(eth0_pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		LOG_ERR("Could not configure ethernet pins");
		return ret;
	}

	p->base_addr = DT_REG_ADDR(DT_INST_PARENT(0));

	return 0;
}

#if defined(CONFIG_DDR)
#define __desc_mem __aligned(4) __attribute__((section("DDR_NC")))
#else
#define __desc_mem __aligned(4) __attribute__((section("NPU_NC")))
#endif

/* Descriptor rings in uncached memory */
static struct dwmac_dma_desc dwmac_tx_descs[NB_TX_DESCS] __desc_mem;
static struct dwmac_dma_desc dwmac_rx_descs[NB_RX_DESCS] __desc_mem;

int dwmac_platform_init(struct dwmac_priv *p)
{
	int ret;

	p->tx_descs = (struct dwmac_dma_desc *)dwmac_tx_descs;
	p->rx_descs = (struct dwmac_dma_desc *)dwmac_rx_descs;

	/* basic configuration for this platform */
	REG_WRITE(MAC_CONF, MAC_CONF_ACS | MAC_CONF_DM);
	REG_WRITE(DMA_SYSBUS_MODE, 3 << 24 | 3 << 16);
	REG_WRITE(MAC_RXQ_CTRL0, 0x2);
	REG_WRITE(MTL_TXQn_OPERATION_MODE(0), 0x001f000a);
	REG_WRITE(MTL_RXQn_OPERATION_MODE(0), 0x01f00038);
	REG_WRITE(DMA_CHn_CTRL(0), DMA_CHn_CTRL_PBLx8);

	/* set up IRQs (still masked for now) */
	IRQ_CONNECT(DT_INST_IRQN(0), DT_INST_IRQ(0, priority), dwmac_isr, DEVICE_DT_INST_GET(0), 0);
	irq_enable(DT_INST_IRQN(0));

	/* retrieve MAC address */
	ret = net_eth_mac_load(&mac_cfg, p->mac_addr);
	if (ret == -ENODATA) {
		uint8_t unique_device_ID_12_bytes[12];
		uint32_t result_mac_32_bits;

		/**
		 * Set MAC address locally administered bit (LAA) as this is not assigned by the
		 * manufacturer
		 */
		p->mac_addr[0] = 0x02;
		p->mac_addr[1] = 0x06;
		p->mac_addr[2] = 0x0a;

		/* Nothing defined by the user, use device id */
		hwinfo_get_device_id(unique_device_ID_12_bytes, 12);
		result_mac_32_bits = crc32_ieee((uint8_t *)unique_device_ID_12_bytes, 12);
		memcpy(&p->mac_addr[3], &result_mac_32_bits, 3);

		ret = 0;
	}

	if (ret < 0) {
		LOG_ERR("Failed to load MAC address (%d)", ret);
		return ret;
	}

	return 0;
}

/* Our private device instance */
static struct dwmac_priv dwmac_instance;

ETH_NET_DEVICE_DT_INST_DEFINE(0, dwmac_probe, NULL, &dwmac_instance, NULL,
			      CONFIG_ETH_INIT_PRIORITY, &dwmac_api, NET_ETH_MTU);
