/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SYNAPTICS_SRW1500_PINCTRL_SOC_H_
#define SYNAPTICS_SRW1500_PINCTRL_SOC_H_

#include <zephyr/devicetree.h>
#include <zephyr/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct pinctrl_soc_pin {
	uint32_t pinmux;
	uint32_t pincfg;
	uint32_t flags;
	uint32_t port;
};

typedef struct pinctrl_soc_pin pinctrl_soc_pin_t;

#define SRW1500_DRV_STRENGTH_SHIFT 1U
#define SRW1500_INPUT_ENABLE_SHIFT 0U
#define SRW1500_PULL_ENABLE_SHIFT  3U
#define SRW1500_SCHMITT_TRIG_SHIFT 5U

#define SRW1500_DRV_STRENGTH_MASK (0x3U << SRW1500_DRV_STRENGTH_SHIFT)
#define SRW1500_INPUT_ENABLE_MASK (0x1U << SRW1500_INPUT_ENABLE_SHIFT)
#define SRW1500_PULL_ENABLE_MASK  (0x3U << SRW1500_PULL_ENABLE_SHIFT)
#define SRW1500_SCHMITT_TRIG_MASK (0x1U << SRW1500_SCHMITT_TRIG_SHIFT)

#define SRW1500_DT_PINCFG_FLAGS(node)                                                 \
	((DT_NODE_HAS_PROP(node, input_enable) << SRW1500_INPUT_ENABLE_SHIFT) |        \
	 (DT_NODE_HAS_PROP(node, input_disable) << SRW1500_INPUT_ENABLE_SHIFT) |       \
	 (DT_PROP_OR(node, bias_disable, 0) << SRW1500_PULL_ENABLE_SHIFT) |            \
	 (DT_PROP_OR(node, bias_enable, 0) << SRW1500_PULL_ENABLE_SHIFT) |             \
	 (DT_PROP_OR(node, bias_pull_up, 0) << SRW1500_PULL_ENABLE_SHIFT) |            \
	 (DT_PROP_OR(node, bias_pull_down, 0) << SRW1500_PULL_ENABLE_SHIFT) |          \
	 (DT_NODE_HAS_PROP(node, drive_strength) << SRW1500_DRV_STRENGTH_SHIFT) |      \
	 (DT_NODE_HAS_PROP(node, input_schmitt_enable) << SRW1500_SCHMITT_TRIG_SHIFT))

#define SRW1500_DT_PINCFG_INIT(node)                                                  \
	((!DT_PROP_OR(node, input_disable, 0) << SRW1500_INPUT_ENABLE_SHIFT) |         \
	 (DT_PROP_OR(node, bias_pull_up, 0) << SRW1500_PULL_ENABLE_SHIFT) |            \
	 (DT_PROP_OR(node, bias_pull_down, 0) << SRW1500_PULL_ENABLE_SHIFT) |          \
	 (DT_PROP_OR(node, drive_strength, 0) << SRW1500_DRV_STRENGTH_SHIFT) |         \
	 (DT_PROP_OR(node, input_schmitt_enable, 0) << SRW1500_SCHMITT_TRIG_SHIFT))

#define SRW1500_DT_PINMUX_INIT(node_id) DT_PROP_BY_IDX(node_id, pinmux, 0)
#define SRW1500_DT_PORT_INIT(node_id)	DT_PROP_OR(node_id, port, 0)

#define Z_PINCTRL_STATE_PIN_INIT(node_id, prop, idx)                                \
	{                                                                           \
		.pinmux = SRW1500_DT_PINMUX_INIT(DT_PROP_BY_IDX(node_id, prop, idx)),  \
		.pincfg = SRW1500_DT_PINCFG_INIT(DT_PROP_BY_IDX(node_id, prop, idx)),  \
		.flags = SRW1500_DT_PINCFG_FLAGS(DT_PROP_BY_IDX(node_id, prop, idx)),  \
		.port = SRW1500_DT_PORT_INIT(DT_PROP_BY_IDX(node_id, prop, idx)),      \
	},

#define Z_PINCTRL_STATE_PINS_INIT(node_id, prop)                                    \
	{ DT_FOREACH_PROP_ELEM(node_id, prop, Z_PINCTRL_STATE_PIN_INIT) }

#ifdef __cplusplus
}
#endif

#endif /* SYNAPTICS_SRW1500_PINCTRL_SOC_H_ */
