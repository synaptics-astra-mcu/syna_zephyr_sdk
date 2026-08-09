/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "arm_security_common.h"
#include <zephyr/devicetree.h>
#include <zephyr/drivers/security/arm_security.h>
#include <zephyr/dt-bindings/security/srw1500-ppc.h>

LOG_MODULE_REGISTER(arm_ppc, CONFIG_LOG_DEFAULT_LEVEL);

struct arm_ppc_periph_cfg {
	uint8_t ns_pos;
	uint8_t priv_pos;
	uint8_t sresp_pos;
	uint8_t flags;
};

#define ARM_PPC_F_NONSEC BIT(0)
#define ARM_PPC_F_USER   BIT(1)
#define ARM_PPC_F_ERR    BIT(2)

struct arm_ppc_config {
	uint32_t ns_reg;
	uint32_t priv_reg;
	uint32_t sresp_reg;
	const struct arm_ppc_periph_cfg *periphs;
	uint8_t periph_count;
};

int arm_ppc_config_periph(const struct device *dev, uint8_t ns_pos,
			  bool nonsecure, uint8_t priv_pos, bool user_ok,
			  uint8_t sresp_pos, bool error_response)
{
	const struct arm_ppc_config *cfg = dev->config;

	arm_set_bit(cfg->ns_reg, ns_pos, nonsecure);
	arm_set_bit(cfg->priv_reg, priv_pos, user_ok);
	arm_set_bit(cfg->sresp_reg, sresp_pos, error_response);
	return 0;
}

static int arm_ppc_init(const struct device *dev)
{
	const struct arm_ppc_config *cfg = dev->config;

	for (uint8_t i = 0; i < cfg->periph_count; i++) {
		const struct arm_ppc_periph_cfg *p = &cfg->periphs[i];

		arm_ppc_config_periph(dev, p->ns_pos, !!(p->flags & ARM_PPC_F_NONSEC),
				       p->priv_pos, !!(p->flags & ARM_PPC_F_USER),
				       p->sresp_pos, !!(p->flags & ARM_PPC_F_ERR));
	}

	arm_barrier();
	return 0;
}

#define PPC_CFG_EMPTY(node_label, ns, priv, sresp) \
	static const struct arm_ppc_config node_label##_cfg = { \
		.ns_reg = (ns), \
		.priv_reg = (priv), \
		.sresp_reg = (sresp), \
		.periphs = NULL, \
		.periph_count = 0U, \
	}; \
	DEVICE_DT_DEFINE(DT_NODELABEL(node_label), arm_ppc_init, NULL, NULL, \
			 &node_label##_cfg, PRE_KERNEL_1, \
			 CONFIG_ARM_SECURITY_INIT_PRIORITY, NULL)

#define PPC_CFG_TABLE(node_label, ns, priv, sresp, table) \
	static const struct arm_ppc_config node_label##_cfg = { \
		.ns_reg = (ns), \
		.priv_reg = (priv), \
		.sresp_reg = (sresp), \
		.periphs = (table), \
		.periph_count = ARRAY_SIZE(table), \
	}; \
	DEVICE_DT_DEFINE(DT_NODELABEL(node_label), arm_ppc_init, NULL, NULL, \
			 &node_label##_cfg, PRE_KERNEL_1, \
			 CONFIG_ARM_SECURITY_INIT_PRIORITY, NULL)

/*
 * Keep all PPC peripherals secure except SPROT mailbox and OTP.
 */
static const struct arm_ppc_periph_cfg srw1500_ppc_p2_periphs[] = {
	{
		.ns_pos = SRW1500_PPC_P2_SPROT_MBOX_BIT,
		.priv_pos = SRW1500_PPC_P2_SPROT_MBOX_BIT,
		.sresp_pos = SRW1500_PPC_SECCTRL_SRESP_BIT,
		.flags = ARM_PPC_F_NONSEC | ARM_PPC_F_USER | ARM_PPC_F_ERR,
	},
	{
		.ns_pos = SRW1500_PPC_P2_SPROT_OTP_BIT,
		.priv_pos = SRW1500_PPC_P2_SPROT_OTP_BIT,
		.sresp_pos = SRW1500_PPC_SECCTRL_SRESP_BIT,
		.flags = ARM_PPC_F_NONSEC | ARM_PPC_F_USER | ARM_PPC_F_ERR,
	},
};

#if DT_NODE_HAS_STATUS(DT_NODELABEL(ppc_p0), okay)
PPC_CFG_EMPTY(ppc_p0, SRW1500_PPC_P0_NS_REG, SRW1500_PPC_P0_PRIV_REG,
	SRW1500_PPC_SECCTRL_SRESP_REG);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(ppc_p1), okay)
PPC_CFG_EMPTY(ppc_p1, SRW1500_PPC_P1_NS_REG, SRW1500_PPC_P1_PRIV_REG,
	SRW1500_PPC_SECCTRL_SRESP_REG);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(ppc_p2), okay)
PPC_CFG_TABLE(ppc_p2, SRW1500_PPC_P2_NS_REG, SRW1500_PPC_P2_PRIV_REG,
	SRW1500_PPC_SECCTRL_SRESP_REG, srw1500_ppc_p2_periphs);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(ppc_exp0), okay)
PPC_CFG_EMPTY(ppc_exp0, SRW1500_PPC_EXP0_NS_REG, SRW1500_PPC_EXP0_PRIV_REG,
	SRW1500_PPC_EXP_SRESP_REG);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(ppc_exp1), okay)
PPC_CFG_EMPTY(ppc_exp1, SRW1500_PPC_EXP1_NS_REG, SRW1500_PPC_EXP1_PRIV_REG,
	SRW1500_PPC_EXP_SRESP_REG);
#endif
