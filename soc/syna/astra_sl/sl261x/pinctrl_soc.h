/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SYNAPTICS_SL261X_PINCTRL_SOC_H_
#define SYNAPTICS_SL261X_PINCTRL_SOC_H_

#include <zephyr/devicetree.h>
#include <zephyr/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct pinctrl_soc_pin {
	uint32_t pinmux;
	uint32_t pincfg;
	uint32_t flags;
};

typedef struct pinctrl_soc_pin pinctrl_soc_pin_t;

#define SLXXX_DRV_STRENGTH_SHIFT	0
#define SLXXX_INPUT_ENABLE_SHIFT	4
#define SLXXX_PULL_ENABLE_SHIFT		5
#define SLXXX_PULL_UP_SHIFT		6
#define SLXXX_SLEW_RATE_SHIFT		7
#define SLXXX_STRONG_PULL_UP		8
#define SLXXX_SCHMITT_TRIG_SHIFT	9

#define SLXXX_DRV_STRENGTH_MASK		(0xf << SLXXX_DRV_STRENGTH_SHIFT)
#define SLXXX_INPUT_ENABLE_MASK		(0x1 << SLXXX_INPUT_ENABLE_SHIFT)
#define SLXXX_PULL_ENABLE_MASK		(0x1 << SLXXX_PULL_ENABLE_SHIFT)
#define SLXXX_SLEW_RATE_MASK		(0x1 << SLXXX_SLEW_RATE_SHIFT)
#define SLXXX_SCHMITT_TRIG_MASK		(0x1 << SLXXX_SCHMITT_TRIG_SHIFT)

#define SLXXX_DT_PINCFG_FLAGS(node)                                                  \
	((DT_NODE_HAS_PROP(node, input_enable) << SLXXX_INPUT_ENABLE_SHIFT) |        \
	 (DT_NODE_HAS_PROP(node, input_disable) << SLXXX_INPUT_ENABLE_SHIFT) |       \
	 (DT_PROP_OR(node, bias_disable, 0) << SLXXX_PULL_ENABLE_SHIFT) |            \
	 (DT_PROP_OR(node, bias_enable, 0) << SLXXX_PULL_ENABLE_SHIFT) |             \
	 (DT_PROP_OR(node, bias_pull_up, 0) << SLXXX_PULL_ENABLE_SHIFT) |            \
	 (DT_PROP_OR(node, bias_pull_down, 0) << SLXXX_PULL_ENABLE_SHIFT) |          \
	 (DT_NODE_HAS_PROP(node, drive_strength) << SLXXX_DRV_STRENGTH_SHIFT) |      \
	 (DT_NODE_HAS_PROP(node, slew_rate) << SLXXX_SLEW_RATE_SHIFT) |              \
	 (DT_NODE_HAS_PROP(node, input_schmitt_enable) << SLXXX_SCHMITT_TRIG_SHIFT))

#define SLXXX_DT_PINCFG_INIT(node)                                                   \
	((!DT_PROP_OR(node, input_disable, 0) << SLXXX_INPUT_ENABLE_SHIFT) |         \
	 (DT_PROP_OR(node, bias_pull_up, 0) << SLXXX_PULL_UP_SHIFT) |                \
	 (DT_PROP_OR(node, bias_pull_up, 0) << SLXXX_PULL_ENABLE_SHIFT) |            \
	 (DT_PROP_OR(node, bias_pull_down, 0) << SLXXX_PULL_ENABLE_SHIFT) |          \
	 (DT_PROP_OR(node, drive_strength, 0) << SLXXX_DRV_STRENGTH_SHIFT) |         \
	 (DT_PROP_OR(node, slew_rate, 0) << SLXXX_SLEW_RATE_SHIFT) |                 \
	 (DT_PROP_OR(node, input_schmitt_enable, 0) << SLXXX_SCHMITT_TRIG_SHIFT))

#define SLXXX_DT_PINMUX_INIT(node_id)	DT_PROP(node_id, pinmux)

#define Z_PINCTRL_STATE_PIN_INIT(node_id, prop, idx)                                 \
	{                                                                            \
		.pinmux = SLXXX_DT_PINMUX_INIT(DT_PROP_BY_IDX(node_id, prop, idx)),  \
		.pincfg = SLXXX_DT_PINCFG_INIT(DT_PROP_BY_IDX(node_id, prop, idx)),  \
		.flags = SLXXX_DT_PINCFG_FLAGS(DT_PROP_BY_IDX(node_id, prop, idx)),  \
	},

#define Z_PINCTRL_STATE_PINS_INIT(node_id, prop)                                     \
	{ DT_FOREACH_PROP_ELEM(node_id, prop, Z_PINCTRL_STATE_PIN_INIT) }

#ifdef __cplusplus
}
#endif

#endif /* SYNAPTICS_SL261X_PINCTRL_SOC_H_ */
