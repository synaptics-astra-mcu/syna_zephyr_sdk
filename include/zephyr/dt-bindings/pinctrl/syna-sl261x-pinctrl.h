/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_PINCTRL_SYNA_SL261X_PINCTRL_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_PINCTRL_SYNA_SL261X_PINCTRL_H_

/**
 * @brief Pin controller id, bit position, mode, mask and offset of pinmux
 *        register, offset of configuration register
 */
#define SLXXX_CTRL_SHIFT 0U
#define SLXXX_CTRL_MASK  0x07U
#define SLXXX_BIT_SHIFT  3U
#define SLXXX_BIT_MASK   0x1FU
#define SLXXX_MODE_SHIFT 8U
#define SLXXX_MODE_MASK  0x07U
#define SLXXX_MASK_SHIFT 11U
#define SLXXX_MASK_MASK  0x0FU
#define SLXXX_REG_SHIFT  16U
#define SLXXX_REG_MASK   0xFFU
#define SLXXX_CFG_SHIFT  24U
#define SLXXX_CFG_MASK   0xFFU

/**
 * @brief Pin configuration bit field. Set mask to 0 for fixed pins (XSPI)

 * Fields:
 *
 * - ctrl  [  0 :  2 ]
 * - bit   [  3 :  7 ]
 * - mode  [  8 : 10 ]
 * - mask  [ 11 : 14 ]
 * - reg   [ 16 : 23 ]
 * - cfg   [ 24 : 31 ]
 *
 * @param ctrl Controller (0 = Global, 1 = SM)
 * @param bit  Bit offset inside register (0..27)
 * @param mode Muxing option (0..7)
 * @param mask Bit mask for muxing option (1..7)
 * @param reg  Register offset relative to pinmux base address (0..255)
 * @param cfg  Register offset relative to pincfg base address (0..255)
 */
#define SLXXX_PINMUX(ctrl, reg, bit, mode, mask, cfg)       \
	((((ctrl) & SLXXX_CTRL_MASK) << SLXXX_CTRL_SHIFT) | \
	 (((reg)  & SLXXX_REG_MASK)  << SLXXX_REG_SHIFT)  | \
	 (((bit)  & SLXXX_BIT_MASK)  << SLXXX_BIT_SHIFT)  | \
	 (((mode) & SLXXX_MODE_MASK) << SLXXX_MODE_SHIFT) | \
	 (((mask) & SLXXX_MASK_MASK) << SLXXX_MASK_SHIFT) | \
	 (((cfg)  & SLXXX_CFG_MASK)  << SLXXX_CFG_SHIFT))

#define SLXXX_GBL_PINMUX(reg, bit, mode, cfg)	SLXXX_PINMUX(0, reg, bit, mode, 7, cfg)
#define SLXXX_SM_PINMUX(reg, bit, mode, cfg)	SLXXX_PINMUX(1, reg, bit, mode, 7, cfg)

#define SLXXX_PINMUX_CTRL(pinmux)	(((pinmux) >> SLXXX_CTRL_SHIFT) & SLXXX_CTRL_MASK)
#define SLXXX_PINMUX_REG(pinmux)	(((pinmux) >> SLXXX_REG_SHIFT) & SLXXX_REG_MASK)
#define SLXXX_PINMUX_BIT(pinmux)	(((pinmux) >> SLXXX_BIT_SHIFT) & SLXXX_BIT_MASK)
#define SLXXX_PINMUX_MODE(pinmux)	(((pinmux) >> SLXXX_MODE_SHIFT) & SLXXX_MODE_MASK)
#define SLXXX_PINMUX_MASK(pinmux)	(((pinmux) >> SLXXX_MASK_SHIFT) & SLXXX_MASK_MASK)
#define SLXXX_PINMUX_CFG(pinmux)	(((pinmux) >> SLXXX_CFG_SHIFT) & SLXXX_CFG_MASK)

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_PINCTRL_SYNA_SL261X_PINCTRL_H_ */
