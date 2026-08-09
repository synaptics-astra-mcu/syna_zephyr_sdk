/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <inttypes.h>
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/arch/common/sys_io.h>
#include <zephyr/dt-bindings/pinctrl/syna-srw1500-pinctrl.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(syna_7650, CONFIG_PINCTRL_LOG_LEVEL);

struct pinctrl_syna_controller {
	uintptr_t mux;
	uintptr_t cfg;
};

struct soc_mcu_mapping {
	gpio_name_t soc_pin;
	soc_function_pinmux_en soc_function;
	uint8_t mcu_pin;
};

struct gci_pinmux_map {
	uint8_t reg_index;
	uint8_t start_bit;
	bool valid;
};

#define S7650_GET_ADDR_OR_NONE(nodelabel, field)                     \
	(DT_NODE_EXISTS(DT_NODELABEL(nodelabel)) ?                    \
	 DT_REG_ADDR_BY_NAME_OR(DT_NODELABEL(nodelabel), field, 0) : 0)

#define S7650_GET_PINCTRL(nodelabel) {                               \
	S7650_GET_ADDR_OR_NONE(nodelabel, pinmux),                    \
	S7650_GET_ADDR_OR_NONE(nodelabel, pincfg) }

static const struct pinctrl_syna_controller pinctrl_syna_ctrl[] = {
	S7650_GET_PINCTRL(pinctrl_gbl),
	S7650_GET_PINCTRL(pinctrl_sm),
};

#define S7650_GCI_BASE_ADDR            0x59031000U
#define S7650_GCI_INDIRECT_ADDR_OFFSET 0x040U
#define S7650_GCI_CHIP_CTRL_OFFSET     0x200U
#define S7650_GGC_MAP_SIZE             0x500U
#define S7650_GPADC_PINMUX_CTL_OFFSET  0x418U

#define S7650_PINMUX_CTRL(pinmux) 0U
#define S7650_PINMUX_MASK(pinmux) 0xFU
#define S7650_PINMUX_MODE(pinmux) ((uint32_t)(pinmux) & 0xFU)
#define S7650_PINMUX_MCU_PIN(pinmux) \
	(((uint32_t)(pinmux) >> MCU_PIN_NAME_SHIFT) & 0xFFU)
#define S7650_PINMUX_REG(pinmux) \
	(S7650_PINMUX_MCU_PIN(pinmux) > MCU_GPIO31 ? \
	 S7650_GPADC_PINMUX_CTL_OFFSET : ((S7650_PINMUX_MCU_PIN(pinmux) / 8U) * 4U))
#define S7650_PINMUX_BIT(pinmux) \
	((S7650_PINMUX_MCU_PIN(pinmux) % 8U) * 4U)

static uint32_t s7650_cfg_offset(uint32_t mcu_pin)
{
	uint32_t index;
	uint32_t offset;

	if (mcu_pin >= MCU_GPIO_MAX) {
		return UINT32_MAX; /* Invalid pin */
	}

	if (mcu_pin > MCU_GPIO31) {
		/* GPADC pins: separate register bank at fixed offset */
		index  = ((mcu_pin - MCU_GPADC_IN0) / 4) * 4;
		offset = 0x41C - 0x10;
	} else {
		/* GPIO pins: one 4-byte pad config register per pin */
		index  = ((mcu_pin / 4) * 4);
		offset = 0x0;
	}
	return (index + offset);
}

#define S7650_PINMUX_CFG(pinmux) s7650_cfg_offset(S7650_PINMUX_MCU_PIN(pinmux))

#define S7650_PIN_POS(pinmux) (((S7650_PINMUX_MCU_PIN(pinmux)) % 4U) * 8U)

static const struct soc_mcu_mapping soc_function_map[] = {
	{GPIO_0, 		SOC_FUNCTION_11, 	MCU_GPIO30},
	{GPIO_1, 		SOC_FUNCTION_11, 	MCU_GPIO31},
	{GPIO_2, 		SOC_FUNCTION_11, 	MCU_GPIO22},
	{GPIO_3, 		SOC_FUNCTION_11, 	MCU_GPIO23},
	{GPIO_4, 		SOC_FUNCTION_11, 	MCU_GPIO24},
	{GPIO_5, 		SOC_FUNCTION_11, 	MCU_GPIO25},
	{GPIO_6, 		SOC_FUNCTION_11, 	MCU_GPIO28},
	{GPIO_7, 		SOC_FUNCTION_11, 	MCU_GPIO29},
	{BT_CLK_REQ, 	SOC_FUNCTION_11, 	MCU_GPIO9},
	{BT_DEV_WAKE, 	SOC_FUNCTION_11, 	MCU_GPIO10},
	{BT_HOST_WAKE, 	SOC_FUNCTION_11, 	MCU_GPIO0},
	{BT_I2S_CLK, 	SOC_FUNCTION_11, 	MCU_GPIO4},
	{BT_I2S_DI, 	SOC_FUNCTION_11, 	MCU_GPIO1},
	{BT_I2S_DO, 	SOC_FUNCTION_11, 	MCU_GPIO3},
	{BT_I2S_WS, 	SOC_FUNCTION_11, 	MCU_GPIO2},
	{BT_UART_CTS_N, SOC_FUNCTION_11, 	MCU_GPIO8},
	{SDIO_CLK, 		SOC_FUNCTION_11, 	MCU_GPIO14},
	{SDIO_CMD, 		SOC_FUNCTION_11, 	MCU_GPIO15},
	{SDIO_DATA_0, 	SOC_FUNCTION_11, 	MCU_GPIO16},
	{SDIO_DATA_1, 	SOC_FUNCTION_11, 	MCU_GPIO17},
	{SDIO_DATA_2, 	SOC_FUNCTION_11, 	MCU_GPIO18},
	{SDIO_DATA_3, 	SOC_FUNCTION_11, 	MCU_GPIO19},
	{BT_UART_RTS_N, SOC_FUNCTION_11, 	MCU_GPIO7},
	{BT_UART_RXD, 	SOC_FUNCTION_11, 	MCU_GPIO5},
	{RF_SW_CTRL_6, 	SOC_FUNCTION_11, 	MCU_GPIO20},
	{RF_SW_CTRL_7, 	SOC_FUNCTION_11, 	MCU_GPIO13},
	{RF_SW_CTRL_0, 	SOC_FUNCTION_11, 	MCU_GPIO26},
	{RF_SW_CTRL_1, 	SOC_FUNCTION_11, 	MCU_GPIO27},
	{RF_SW_CTRL_2, 	SOC_FUNCTION_11, 	MCU_GPIO26},
	{RF_SW_CTRL_3, 	SOC_FUNCTION_11, 	MCU_GPIO27},
	{RF_SW_CTRL_4, 	SOC_FUNCTION_11, 	MCU_GPIO20},
	{RF_SW_CTRL_5, 	SOC_FUNCTION_11, 	MCU_GPIO21},
	{MCU_GPIO_11, 	SOC_FUNCTION_11, 	MCU_GPIO2},
	{MCU_GPIO_12, 	SOC_FUNCTION_11, 	MCU_GPIO1},
	{MCU_GPIO_11, 	SOC_FUNCTION_1, 	MCU_GPIO11},
	{MCU_GPIO_12, 	SOC_FUNCTION_1, 	MCU_GPIO12},
	{BT_UART_TXD, 	SOC_FUNCTION_11, 	MCU_GPIO6},
	{GPADC_IN0, 	SOC_FUNCTION_MAX, 	MCU_GPADC_IN0},
	{GPADC_IN1, 	SOC_FUNCTION_MAX, 	MCU_GPADC_IN1},
	{GPADC_IN2, 	SOC_FUNCTION_MAX, 	MCU_GPADC_IN2},
	{GPADC_IN3, 	SOC_FUNCTION_MAX, 	MCU_GPADC_IN3},
	{GPADC_IN4, 	SOC_FUNCTION_MAX, 	MCU_GPADC_IN4},
	{GPADC_IN5, 	SOC_FUNCTION_MAX, 	MCU_GPADC_IN5},
	{GPADC_IN6, 	SOC_FUNCTION_MAX, 	MCU_GPADC_IN6},
	{GPADC_IN7, 	SOC_FUNCTION_MAX, 	MCU_GPADC_IN7},

};

static struct gci_pinmux_map s7650_get_gci_pinmux_map(gpio_name_t soc_pin)
{
	if (soc_pin <= RF_SW_CTRL_5) {
		return (struct gci_pinmux_map){
			.reg_index = (uint8_t)soc_pin / 8U,
			.start_bit = ((uint8_t)soc_pin % 8U) * 4U,
			.valid = true,
		};
	}

	switch (soc_pin) {
	case MCU_GPIO_11:
		return (struct gci_pinmux_map){
			.reg_index = 27U,
			.start_bit = 12U,
			.valid = true,
		};
	case MCU_GPIO_12:
		return (struct gci_pinmux_map){
			.reg_index = 27U,
			.start_bit = 16U,
			.valid = true,
		};
	case MCU_GPIO_13:
		return (struct gci_pinmux_map){
			.reg_index = 27U,
			.start_bit = 20U,
			.valid = true,
		};
	case BT_UART_TXD:
		return (struct gci_pinmux_map){
			.reg_index = 27U,
			.start_bit = 24U,
			.valid = true,
		};
	default:
		return (struct gci_pinmux_map){ .valid = false };
	}
}

static bool s7650_lookup_soc_pin(uint8_t mcu_pin, uint8_t soc_function,
				 gpio_name_t *soc_pin)
{
	for (size_t i = 0; i < ARRAY_SIZE(soc_function_map); i++) {
		if ((soc_function_map[i].mcu_pin == mcu_pin) &&
		    (soc_function_map[i].soc_function == soc_function)) {
			*soc_pin = soc_function_map[i].soc_pin;
			return true;
		}
	}
	return false;
}

static void s7650_set_soc_pinmux(gpio_name_t soc_pin, uint8_t soc_function)
{
	struct gci_pinmux_map map = s7650_get_gci_pinmux_map(soc_pin);
	uint32_t value;
	uint32_t mask;

	if (!map.valid) {
		return;
	}

	mask = 0xFU << map.start_bit;
	sys_write32(map.reg_index, S7650_GCI_BASE_ADDR + S7650_GCI_INDIRECT_ADDR_OFFSET);
	value = sys_read32(S7650_GCI_BASE_ADDR + S7650_GCI_CHIP_CTRL_OFFSET);
	value = (value & ~mask) | (((uint32_t)soc_function & 0xFU) << map.start_bit);
	sys_write32(value, S7650_GCI_BASE_ADDR + S7650_GCI_CHIP_CTRL_OFFSET);
}

static uint32_t s7650_pinmux_get(const pinctrl_soc_pin_t *soc_pin)
{
#if defined(SRW1500_DT_PINCFG_FLAGS)
	return soc_pin->pinmux;
#else
	return (uint32_t)(*soc_pin);
#endif
}

static uint32_t s7650_pincfg_get(const pinctrl_soc_pin_t *soc_pin)
{
#if defined(SRW1500_DT_PINCFG_FLAGS)
	return soc_pin->pincfg;
#else
	ARG_UNUSED(soc_pin);
	return 0U;
#endif
}

static uint32_t s7650_flags_get(const pinctrl_soc_pin_t *soc_pin)
{
#if defined(SRW1500_DT_PINCFG_FLAGS)
	return soc_pin->flags;
#else
	ARG_UNUSED(soc_pin);
	return 0U;
#endif
}

static void pinctrl_cfg(const pinctrl_soc_pin_t *soc_pin, uint32_t *value,
			uint32_t mask)
{
	uint32_t flags = s7650_flags_get(soc_pin);
	uint32_t pincfg = s7650_pincfg_get(soc_pin);

	if ((flags & mask) != 0U) {
		*value &= ~mask;
		*value |= pincfg & mask;
	}
}

static void pinctrl_configure_pin(mm_reg_t mux, mm_reg_t cfg,
				  const pinctrl_soc_pin_t *soc_pin)
{
	mm_reg_t pinmux_base;
	mm_reg_t pincfg_base;
	gpio_name_t soc_pin_name;
	uint8_t mcu_pin;
	uint8_t soc_function;
	uint32_t value = 0, current_value = 0;
	uint32_t reg;
	uint32_t bit;
	uint32_t mode;
	uint32_t mask;
	uint32_t pinmux;
	uint32_t pos;

	/* Extract pin configuration from devicetree */
	pinmux = s7650_pinmux_get(soc_pin);
	mcu_pin = S7650_PINMUX_MCU_PIN(pinmux);
	soc_function = (pinmux >> SOC_PINMUX_FUNCTION_SHIFT) & 0xFU;

	if (S7650_PINMUX_CTRL(pinmux) == 0U) {
		pinmux_base = mux;
		pincfg_base = cfg;
	}

	/* Step 1: Configure electrical parameters (pad config) first */
	if (s7650_flags_get(soc_pin) != 0U) {
		uint32_t value = 0U;
		uint32_t mask;
		uintptr_t addr;

		reg = S7650_PINMUX_CFG(pinmux);

		if (reg == UINT32_MAX) {
			LOG_DBG("skip pincfg for unsupported mcu pin %u", mcu_pin);
		} else {
			pos = S7650_PIN_POS(pinmux);

			mask = 0xFFU << pos;
			addr = pincfg_base + reg;

			current_value = sys_read32(addr);

			pinctrl_cfg(soc_pin, &value, SRW1500_DRV_STRENGTH_MASK);
			pinctrl_cfg(soc_pin, &value, SRW1500_PULL_ENABLE_MASK);
			pinctrl_cfg(soc_pin, &value, SRW1500_INPUT_ENABLE_MASK);
			pinctrl_cfg(soc_pin, &value, SRW1500_SCHMITT_TRIG_MASK);

			current_value &= ~mask;
			current_value |= ((value & 0xFFU) << pos);

			sys_write32(current_value, addr);

			LOG_DBG("pincfg 0x%08x -> 0x%" PRIxPTR,
				current_value, addr);
		}
	}

	/* Step 2: Configure pin function (SOC pinmux) */
	if ((soc_function != SOC_FUNCTION_MAX) &&
	    s7650_lookup_soc_pin(mcu_pin, soc_function, &soc_pin_name)) {
		s7650_set_soc_pinmux(soc_pin_name, soc_function);
	}

	/* Step 3: Configure MCU pin mux */
	mask = S7650_PINMUX_MASK(pinmux);
	if (mask != 0U) {
		reg = S7650_PINMUX_REG(pinmux);
		bit = S7650_PINMUX_BIT(pinmux);
		mode = S7650_PINMUX_MODE(pinmux);

		value = sys_read32(pinmux_base + reg);
		value = (value & ~(mask << bit)) | (mode << bit);
		sys_write32(value, pinmux_base + reg);
		LOG_DBG("pinctrl 0x%x -> 0x%" PRIxPTR, value, pinmux_base + reg);
	}
}

int pinctrl_configure_pins(const pinctrl_soc_pin_t *pins, uint8_t pin_cnt,
			   uintptr_t reg)
{
	mm_reg_t mux;
	mm_reg_t cfg;

	ARG_UNUSED(reg);

	if ((pinctrl_syna_ctrl[0].mux == 0U) || (pinctrl_syna_ctrl[0].cfg == 0U)) {
		LOG_ERR("missing GGC pinctrl register base (pinmux=0x%" PRIxPTR ", pincfg=0x%" PRIxPTR ")",
			pinctrl_syna_ctrl[0].mux, pinctrl_syna_ctrl[0].cfg);
		return -ENODEV;
	}

	device_map(&mux, pinctrl_syna_ctrl[0].mux, S7650_GGC_MAP_SIZE, K_MEM_CACHE_NONE);
	device_map(&cfg, pinctrl_syna_ctrl[0].cfg, S7650_GGC_MAP_SIZE, K_MEM_CACHE_NONE);

	for (uint8_t i = 0U; i < pin_cnt; i++) {
		pinctrl_configure_pin(mux, cfg, &pins[i]);
	}

	device_unmap(mux, S7650_GGC_MAP_SIZE);
	device_unmap(cfg, S7650_GGC_MAP_SIZE);

	return 0;
}