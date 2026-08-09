/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_PINCTRL_SYNA_7650_PINCTRL_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_PINCTRL_SYNA_7650_PINCTRL_H_

#define SOC_PIN_NAME_SHIFT                  16
#define MCU_PIN_NAME_SHIFT                  8
#define SOC_PINMUX_FUNCTION_SHIFT           4

#ifndef __DTS__

/*
 * \brief Enumeration for driver return statuses.
 *
 * This enumeration defines various return and error codes used by
 * the Pinmux driver to indicate success, failure, and specific error conditions.
 */
typedef enum {
/* Pinmux Driver */
    PINMUX_OK                       = 0,   /**< Successful */
    PINMUX_ERROR                    = 1,   /**< Non-specific error code */
    PINMUX_ERROR_UNSUPPORTED        = 2,   /**< The module (or part of it) is not supported */
    PINMUX_ERROR_PARAMETER          = 3,   /**< Invalid driver electrical parameter */
    PINMUX_ERROR_INVALID_FUNCTION   = 4,   /**< Invalid Function */
    PINMUX_ERROR_PIN_NOT_AVAILABLE  = 50   /**< Pin not available */
} pinmux_status_en;

/*******************************************************************************
*                               Type Definitons
*******************************************************************************/
typedef enum
{
    MCU_GPIO0 = 0,   /**< MCU GPIO pin 0 */
    MCU_GPIO1 = 1,   /**< MCU GPIO pin 1 */
    MCU_GPIO2 = 2,   /**< MCU GPIO pin 2 */
    MCU_GPIO3 = 3,   /**< MCU GPIO pin 3 */
    MCU_GPIO4 = 4,   /**< MCU GPIO pin 4 */
    MCU_GPIO5 = 5,   /**< MCU GPIO pin 5 */
    MCU_GPIO6 = 6,   /**< MCU GPIO pin 6 */
    MCU_GPIO7 = 7,   /**< MCU GPIO pin 7 */
    MCU_GPIO8 = 8,   /**< MCU GPIO pin 8 */
    MCU_GPIO9 = 9,   /**< MCU GPIO pin 9 */
    MCU_GPIO10 = 10, /**< MCU GPIO pin 10 */
    MCU_GPIO11 = 11, /**< MCU GPIO pin 11 */
    MCU_GPIO12 = 12, /**< MCU GPIO pin 12 */
    MCU_GPIO13 = 13, /**< MCU GPIO pin 13 */
    MCU_GPIO14 = 14, /**< MCU GPIO pin 14 */
    MCU_GPIO15 = 15, /**< MCU GPIO pin 15 */
    MCU_GPIO16 = 16, /**< MCU GPIO pin 16 */
    MCU_GPIO17 = 17, /**< MCU GPIO pin 17 */
    MCU_GPIO18 = 18, /**< MCU GPIO pin 18 */
    MCU_GPIO19 = 19, /**< MCU GPIO pin 19 */
    MCU_GPIO20 = 20, /**< MCU GPIO pin 20 */
    MCU_GPIO21 = 21, /**< MCU GPIO pin 21 */
    MCU_GPIO22 = 22, /**< MCU GPIO pin 22 */
    MCU_GPIO23 = 23, /**< MCU GPIO pin 23 */
    MCU_GPIO24 = 24, /**< MCU GPIO pin 24 */
    MCU_GPIO25 = 25, /**< MCU GPIO pin 25 */
    MCU_GPIO26 = 26, /**< MCU GPIO pin 26 */
    MCU_GPIO27 = 27, /**< MCU GPIO pin 27 */
    MCU_GPIO28 = 28, /**< MCU GPIO pin 28 */
    MCU_GPIO29 = 29, /**< MCU GPIO pin 29 */
    MCU_GPIO30 = 30, /**< MCU GPIO pin 30 */
    MCU_GPIO31 = 31, /**< MCU GPIO pin 31 */

    MCU_GPADC_IN0 = 32, /**< GPADC input channel 0 */
    MCU_GPADC_IN1 = 33, /**< GPADC input channel 1 */
    MCU_GPADC_IN2 = 34, /**< GPADC input channel 2 */
    MCU_GPADC_IN3 = 35, /**< GPADC input channel 3 */
    MCU_GPADC_IN4 = 36, /**< GPADC input channel 4 */
    MCU_GPADC_IN5 = 37, /**< GPADC input channel 5 */
    MCU_GPADC_IN6 = 38, /**< GPADC input channel 6 */
    MCU_GPADC_IN7 = 39, /**< GPADC input channel 7 */

    MCU_GPIO_MAX   = 40, /**< Maximum number of GPIO instances */
}mcu_gpio_pin_t;

typedef enum
{
    SOC_FUNCTION_0 = 0,
    SOC_FUNCTION_1,
    SOC_FUNCTION_2,
    SOC_FUNCTION_3,
    SOC_FUNCTION_4,
    SOC_FUNCTION_5,
    SOC_FUNCTION_6,
    SOC_FUNCTION_7,
    SOC_FUNCTION_8,
    SOC_FUNCTION_9,
    SOC_FUNCTION_10,
    SOC_FUNCTION_11,

    SOC_FUNCTION_MAX 
}soc_function_pinmux_en;

typedef enum {
    GPIO_0,
    GPIO_1,
    GPIO_2,
    GPIO_3,
    GPIO_4,
    GPIO_5,
    GPIO_6,
    GPIO_7,

    BT_CLK_REQ,
    BT_DEV_WAKE,
    BT_HOST_WAKE,
    BT_I2S_CLK,
    BT_I2S_DI,
    BT_I2S_DO,
    BT_I2S_WS,
    BT_UART_CTS_N,

    SDIO_CLK,
    SDIO_CMD,
    SDIO_DATA_0,
    SDIO_DATA_1,
    SDIO_DATA_2,
    SDIO_DATA_3,
    BT_UART_RTS_N,
    BT_UART_RXD,

    RF_SW_CTRL_6,
    RF_SW_CTRL_7,
    RF_SW_CTRL_0,
    RF_SW_CTRL_1,
    RF_SW_CTRL_2,
    RF_SW_CTRL_3,
    RF_SW_CTRL_4,
    RF_SW_CTRL_5,

    MCU_GPIO_0,
    MCU_GPIO_1,
    MCU_GPIO_2,
    MCU_GPIO_3,
    MCU_GPIO_4,
    MCU_GPIO_5,
    MCU_GPIO_6,
    MCU_GPIO_7,

    MCU_GPIO_8,
    MCU_GPIO_9,
    MCU_GPIO_10,
    MCU_GPIO_11,
    MCU_GPIO_12,
    MCU_GPIO_13,
    BT_UART_TXD,

    GPADC_IN0,
    GPADC_IN1,
    GPADC_IN2,
    GPADC_IN3,
    GPADC_IN4,
    GPADC_IN5,
    GPADC_IN6,
    GPADC_IN7,

    UNUSED,

    GPIO_MAX
} gpio_name_t;

typedef enum
{
    MCU_MODE_0 = 0,
    MCU_MODE_1,
    MCU_MODE_2,
    MCU_MODE_3,
    MCU_MODE_4,
    MCU_MODE_5,
    MCU_MODE_6,
    MCU_MODE_7,

    MCU_MODE_MAX //Invalid mode
}mcu_mode_pinmux_en;

#endif /* __DTS__ */

/**
 * @brief Pin configuration bit field.
 *
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
#define SRW1500_MCU_PINMUX(pin, func, mode)			\
		((pin  << MCU_PIN_NAME_SHIFT) |				\
	 	 (func << SOC_PINMUX_FUNCTION_SHIFT) |		\
	 	 (mode))

#define SRW1500_GBL_PINMUX(mcu_pin, func, mode) SRW1500_MCU_PINMUX(mcu_pin, func, mode)

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_PINCTRL_SYNA_7650_PINCTRL_H_ */