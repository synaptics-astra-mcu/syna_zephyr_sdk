/****************************************************************************
**
**  Name:          upio_sr110.c
**
**  Description:   Contains platform GPIO operation API for SR110
**
**
**  Copyright (c) 2019-2020, Broadcom, All Rights Reserved.
**  Broadcom Bluetooth Core. Proprietary and confidential.
******************************************************************************/
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <stdio.h>
#include "upio.h"
#define debug_printf(...) printf(__VA_ARGS__)
// Zephyr GPIO device and pin definitions (adjust as needed for your board)
#define ZEPHYR_BT_GPIO_NODE DT_NODELABEL(gpioa)
#define ZEPHYR_BT_GPIO_DEV DEVICE_DT_GET(ZEPHYR_BT_GPIO_NODE)
#define ZEPHYR_BT_REGON_PIN 25 // Example: GPIO_25, adjust as needed
#define ZEPHYR_BT_WAKE_PIN  98 // Example: GPIO_98, adjust as needed
#define ZEPHYR_HOST_WAKE_PIN 99 // Example: GPIO_99, adjust as needed

#define CONFIG_SR110_RDK_REV_C
// Define meaningful constants based on RDK revision
#ifdef CONFIG_SR110_RDK_REV_C
#define BT_REGON_GPIO_PIN       ZEPHYR_BT_REGON_PIN
#define BT_GPIO_PORT            0      // GPIO port for BT pins
#else
#define BT_REGON_GPIO_PIN       ZEPHYR_BT_REGON_PIN
#define BT_GPIO_PORT            1      // GPIO port for BT pins
#endif

#define BT_WAKE_GPIO_PIN        ZEPHYR_BT_WAKE_PIN
#define HOST_WAKE_GPIO_PIN      ZEPHYR_HOST_WAKE_PIN
#define INIT_DELAY_MS           100         // Initialization delay
#define SYSTEM_BOOT_DELAY_MS    2000        // System boot completion wait

/******************************************************
 *               Variable Definitions
 ******************************************************/

static bool upio_is_init = false;
static bool gpio_bt_regon_configured = false;
static uint8_t bt_regon_state = 0;

/******************************************************
 *               Private Function Declarations
 ******************************************************/
static uint8_t configure_bt_regon_gpio(void);
#ifndef CONFIG_SR110_RDK_REV_C
static void configure_swire_data_to_gpio43(void);
#endif

/*******************************************************************************
 **  UPIO Driver functions
 *******************************************************************************/

/*****************************************************************************
 ** Function         configure_swire_data_to_gpio43
 ** Description      Configure SWIRE_DATA pin to GPIO43 for BT_REGON (Rev B only)
 ** Returns          nothing
 *****************************************************************************/
#ifndef CONFIG_SR110_RDK_REV_C
static void configure_swire_data_to_gpio43(void)
{
    debug_printf("SWIRE_DATA pin to GPIO43 configuration...\n");
    /* HW_REG_FIELD_WRITE(GLOBAL_BASE_ADDRESS, GLOBAL_SWIRE_CTRL, SWIRE_POWER_DOWN, 0); */
    /* HW_REG_FIELD_WRITE(GLOBAL_BASE_ADDRESS, GLOBAL_SWIRE_CTRL, DAT_PAD_GPIO_SEL, 1); */
}
#endif

/*****************************************************************************
 ** Function         configure_bt_regon_gpio
 ** Description      Configure BT_REGON GPIO (GPIO43 for Rev B, GPIO25 for Rev C)
 ** Returns          0 on success, error code on failure
 *****************************************************************************/
static uint8_t configure_bt_regon_gpio(void)
{
    uint8_t rc;
   

    #ifdef LEGACY_GPIO // Legacy GPIO config, replaced by Zephyr API
     gpio_pin_config_t pin_cfg;

    // Configure GPIO pin properties
    pin_cfg.out_en        = true;
    pin_cfg.int_en        = false;
    pin_cfg.int_mask      = false;
    pin_cfg.edge_int      = true;
    pin_cfg.both_edge_int = false;
    pin_cfg.level         = 1;
    pin_cfg.debounce      = false;
    rc = gpio_pin_set_config(BT_GPIO_PORT, BT_REGON_GPIO_PIN, &pin_cfg); // replaced by Zephyr API
    if (rc != GPIO_OK) {
        #ifdef CONFIG_SR110_RDK_REV_C
        debug_printf("Failed to configure GPIO 25, rc=%d\n", rc);
        #else
        debug_printf("Failed to configure GPIO 43, rc=%d\n", rc);
        #endif
        return rc;
    }
    #else
    int zrc = gpio_pin_configure(ZEPHYR_BT_GPIO_DEV, BT_REGON_GPIO_PIN, GPIO_OUTPUT_ACTIVE);
    // Example: To enable edge or level interrupt in Zephyr, uncomment and adjust as needed:
    // gpio_pin_interrupt_configure(ZEPHYR_BT_GPIO_DEV, BT_REGON_GPIO_PIN, GPIO_INT_EDGE_RISING);
    // gpio_pin_interrupt_configure(ZEPHYR_BT_GPIO_DEV, BT_REGON_GPIO_PIN, GPIO_INT_LEVEL_HIGH);
    if (zrc != 0) {
        debug_printf("Failed to configure BT_REGON GPIO (pin %d), rc=%d", BT_REGON_GPIO_PIN, zrc);
        return zrc;
    }
    #endif

#ifdef CONFIG_SR110_RDK_REV_C
    debug_printf("GPIO 25 successfully configured for BT_REGON\n");
#else
    debug_printf("GPIO 43 successfully configured for BT_REGON\n");
#endif
    return 0;
}

/*****************************************************************************
 **
 ** Function         UPIO_Init
 **
 ** Description
 **      Initialize the GPIO service.
 **      This function is typically called once upon system startup.
 **
 ** Returns          nothing
 **
 *****************************************************************************/
UDRV_API void UPIO_Init(void *p_cfg)
{
    uint8_t rc;

    if (upio_is_init) {
        debug_printf("UPIO already initialized\n");
        return;
    }

    debug_printf("Starting UPIO initialization...\n");

    // Initialize GPIO module first
    #ifdef LEGACY_GPIO // Legacy GPIO init, replaced by Zephyr API
    gpio_init(BT_GPIO_PORT); // replaced by Zephyr API
    debug_printf("GPIO module initialized for port %d\n", BT_GPIO_PORT);
    #else
    if (!device_is_ready(ZEPHYR_BT_GPIO_DEV)) {
        debug_printf("BT GPIO device not ready");
        return;
    }
    #endif

    // Wait for system to fully boot before reconfiguring SWIRE_DATA pin
    debug_printf("Waiting for system boot completion before SWIRE_DATA reconfiguration...\n");
    // Zephyr alternative to vTaskDelay
    k_msleep(SYSTEM_BOOT_DELAY_MS); // Zephyr sleep
    // vTaskDelay(pdMS_TO_TICKS(SYSTEM_BOOT_DELAY_MS)); // FreeRTOS legacy

    // Configure BT_REGON GPIO based on RDK revision
    if (!gpio_bt_regon_configured) {
#ifndef CONFIG_SR110_RDK_REV_C
        // Rev B: Configure SWIRE_DATA to GPIO43 for BT_REGON
        configure_swire_data_to_gpio43();
#endif

        rc = configure_bt_regon_gpio();
        if (rc == 0) {
            gpio_bt_regon_configured = true;
#ifdef CONFIG_SR110_RDK_REV_C
            debug_printf("GPIO 25 successfully configured for BT_REGON\n");
#else
            debug_printf("SWIRE_DATA successfully reconfigured to GPIO 43\n");
#endif
        } else {
#ifdef CONFIG_SR110_RDK_REV_C
            debug_printf("Failed to configure GPIO 25 for BT_REGON, rc=%d\n", rc);
#else
            debug_printf("Failed to reconfigure SWIRE_DATA to GPIO 43, rc=%d\n", rc);
#endif
            return;
        }
    }

    // Initialize BT_REGON: reset sequence
    #ifdef LEGACY_GPIO // Legacy GPIO write, replaced by Zephyr API
    rc = gpio_pin_write(BT_GPIO_PORT, BT_REGON_GPIO_PIN, 0); // replaced by Zephyr API
    if (rc != GPIO_OK) {
        #ifdef CONFIG_SR110_RDK_REV_C
        debug_printf("Failed to write GPIO 25 low, rc=%d\n", rc);
        #else
        debug_printf("Failed to write GPIO 43 low, rc=%d\n", rc);
        #endif
        return;
    }
    #else
    gpio_pin_set(ZEPHYR_BT_GPIO_DEV, BT_REGON_GPIO_PIN, 0);
    #endif

    // Zephyr alternative to vTaskDelay
    k_msleep(INIT_DELAY_MS); // Zephyr sleep
    // vTaskDelay(pdMS_TO_TICKS(INIT_DELAY_MS)); // FreeRTOS legacy

    #ifdef LEGACY_GPIO // Legacy GPIO write, replaced by Zephyr API
    rc = gpio_pin_write(BT_GPIO_PORT, BT_REGON_GPIO_PIN, 1); // replaced by Zephyr API
    if (rc != GPIO_OK) {
        #ifdef CONFIG_SR110_RDK_REV_C
        debug_printf("Failed to write GPIO 25 high, rc=%d\n", rc);
        #else
        debug_printf("Failed to write GPIO 43 high, rc=%d\n", rc);
        #endif
        return;
    }
    #else
    gpio_pin_set(ZEPHYR_BT_GPIO_DEV, BT_REGON_GPIO_PIN, 1);
    #endif

    bt_regon_state = 1;
    upio_is_init = true;

    debug_printf("UPIO initialization completed successfully\n");
}

/*****************************************************************************
 **
 ** Function         UPIO_DeInit
 **
 ** Description
 **      DeInit the GPIO service.
 **      This function is typically called once stop system down.
 **
 ** Returns          nothing
 **
 *****************************************************************************/
UDRV_API void UPIO_DeInit(void)
{
    if (upio_is_init && gpio_bt_regon_configured) {
        #ifdef LEGACY_GPIO // Legacy GPIO write, replaced by Zephyr API
        uint8_t rc = gpio_pin_write(BT_GPIO_PORT, BT_REGON_GPIO_PIN, 0); // replaced by Zephyr API
        if (rc != GPIO_OK) {
            debug_printf("Failed to reset BT_REGON during deinit, rc=%d\n", rc);
        } else {
            debug_printf("BT_REGON reset during deinit\n");
        }
        #else
        gpio_pin_set(ZEPHYR_BT_GPIO_DEV, BT_REGON_GPIO_PIN, 0);
        #endif
        bt_regon_state = 0;
    }

    gpio_bt_regon_configured = false;
    upio_is_init = false;
    debug_printf("UPIO deinitialized\n");
}

/*****************************************************************************
 **
 ** Function         UPIO_Set
 **
 ** Description
 **      This function sets one or more GPIO devices to the given state.
 **      Multiple GPIOs of the same type can be masked together to set more
 **      than one GPIO. This function can only be used on types UPIO_LED and
 **      UPIO_GENERAL.
 **
 ** Input Parameters:
 **      type    The type of device.
 **      pio     Indicates the particular GPIOs.
 **      state   The desired state.
 **
 ** Output Parameter:
 **      None.
 **
 ** Returns:
 **      None.
 **
 *****************************************************************************/
UDRV_API void UPIO_Set(tUPIO_TYPE type, tUPIO pio, tUPIO_STATE state)
{
    uint8_t rc;
    uint8_t gpio_value = (state == UPIO_ON) ? 1 : 0;

    // Zephyr logging alternative
    debug_printf("UPIO_Set type=%d, pio=%d, state=%s", type, pio, (state == UPIO_OFF) ? "OFF" : "ON");
    // debug_printf("UPIO_Set type=%d, pio=%d, state=%s\n", type, pio, (state == UPIO_OFF) ? "OFF" : "ON");

    if (!upio_is_init) {
        debug_printf("UPIO_Set: UPIO not initialized"); // Zephyr log
        // debug_printf("UPIO_Set: UPIO not initialized\n");
        return;
    }

    if (type == UPIO_GENERAL) {
        switch (pio) {
            case BT_REG_ON_GPIO:
                if (gpio_bt_regon_configured) {
                    #ifdef LEGACY_GPIO // Legacy GPIO write, replaced by Zephyr API
                    rc = gpio_pin_write(BT_GPIO_PORT, BT_REGON_GPIO_PIN, gpio_value); // replaced by Zephyr API
                    if (rc == GPIO_OK) {
                        bt_regon_state = gpio_value;
                        #ifdef CONFIG_SR110_RDK_REV_C
                        debug_printf("BT_REGON (GPIO25) set to %d\n", gpio_value);
                        #else
                        debug_printf("BT_REGON (GPIO43) set to %d\n", gpio_value);
                        #endif
                    } else {
                        #ifdef CONFIG_SR110_RDK_REV_C
                        debug_printf("Failed to set BT_REGON (GPIO25), rc=%d\n", rc);
                        #else
                        debug_printf("Failed to set BT_REGON (GPIO43), rc=%d\n", rc);
                        #endif
                    }
                    #else
                    gpio_pin_set(ZEPHYR_BT_GPIO_DEV, BT_REGON_GPIO_PIN, gpio_value);
                    #endif
                } else {
#ifdef CONFIG_SR110_RDK_REV_C
                    debug_printf("BT_REGON GPIO25 not configured\n");
#else
                    debug_printf("BT_REGON GPIO43 not configured\n");
#endif
                }
                break;

            case HCILP_BT_WAKE_GPIO:
                // TODO: Implement BT_WAKE GPIO control when needed
                debug_printf("BT_WAKE GPIO control not implemented\n");
                break;

            default:
                debug_printf("Unsupported GPIO pio=%d", pio); // Zephyr log
                // debug_printf("Unsupported GPIO pio=%d\n", pio);
                break;
        }
    } else {
        debug_printf("Unsupported UPIO type=%d", type); // Zephyr log
        // debug_printf("Unsupported UPIO type=%d\n", type);
    }
}

void UPIO_Set_CTS_Low(void)
{
    debug_printf("UPIO_Set_CTS_Low: Not implemented"); // Zephyr log
    // debug_printf("UPIO_Set_CTS_Low: Not implemented\n");
}

void UPIO_Restore_CTS_Setting(void)
{
    debug_printf("UPIO_Restore_CTS_Setting: Not implemented"); // Zephyr log
    // debug_printf("UPIO_Restore_CTS_Setting: Not implemented\n");
}

void UPIO_Set_Host_LPO(void)
{
    debug_printf("UPIO_Set_Host_LPO: Not implemented"); // Zephyr log
    // debug_printf("UPIO_Set_Host_LPO: Not implemented\n");
}

/*****************************************************************************
 **
 ** Function         UPIO_Read
 **
 ** Description
 **      Read the state of a GPIO. This function can be used for any type of
 **      device. Parameter pio can only indicate a single GPIO; multiple GPIOs
 **      cannot be masked together.
 **
 ** Input Parameters:
 **      Type:	The type of device.
 **      pio:    Indicates the particular GUPIO.
 **
 ** Output Parameter:
 **      None.
 **
 ** Returns:
 **      State of GPIO (UPIO_ON or UPIO_OFF).
 **
 *****************************************************************************/
UDRV_API tUPIO_STATE UPIO_Read(tUPIO_TYPE type, tUPIO pio)
{
    uint32_t gpio_val;
    uint8_t rc;

    if (!upio_is_init) {
        debug_printf("UPIO_Read: UPIO not initialized"); // Zephyr log
        // debug_printf("UPIO_Read: UPIO not initialized\n");
        return UPIO_OFF;
    }

    if (type == UPIO_GENERAL) {
        switch (pio) {
            case BT_REG_ON_GPIO:
                if (gpio_bt_regon_configured) {
                    #ifdef LEGACY_GPIO // Legacy GPIO read, replaced by Zephyr API
                    rc = gpio_pin_read(BT_GPIO_PORT, BT_REGON_GPIO_PIN, &gpio_val); // replaced by Zephyr API
                    if (rc == GPIO_OK) {
                        #ifdef CONFIG_SR110_RDK_REV_C
                        debug_printf("BT_REGON (GPIO25) read value: %d\n", gpio_val);
                        #else
                        debug_printf("BT_REGON (GPIO43) read value: %d\n", gpio_val);
                        #endif
                        return gpio_val ? UPIO_ON : UPIO_OFF;
                    } else {
                        #ifdef CONFIG_SR110_RDK_REV_C
                        debug_printf("Failed to read BT_REGON (GPIO25), rc=%d\n", rc);
                        #else
                        debug_printf("Failed to read BT_REGON (GPIO43), rc=%d\n", rc);
                        #endif
                    }
                    #else
                    int val = gpio_pin_get(ZEPHYR_BT_GPIO_DEV, BT_REGON_GPIO_PIN);
                    if (val < 0) {
                        debug_printf("Failed to read BT_REGON GPIO (pin %d), rc=%d", BT_REGON_GPIO_PIN, val);
                        return UPIO_OFF;
                    }
                    return val ? UPIO_ON : UPIO_OFF;
                    #endif
                } else {
#ifdef CONFIG_SR110_RDK_REV_C
                    debug_printf("BT_REGON GPIO25 not configured for read\n");
#else
                    debug_printf("BT_REGON GPIO43 not configured for read\n");
#endif
                }
                break;

            default:
                debug_printf("UPIO_Read: Unsupported GPIO pio=%d", pio); // Zephyr log
                // debug_printf("UPIO_Read: Unsupported GPIO pio=%d\n", pio);
                break;
        }
    } else {
        debug_printf("UPIO_Read: Unsupported UPIO type=%d", type); // Zephyr log
        // debug_printf("UPIO_Read: Unsupported UPIO type=%d\n", type);
    }

    return UPIO_OFF;
}

/*****************************************************************************
 **
 ** Function         UPIO_Config
 **
 ** Description      - Configure GPIOs of type UPIO_GENERAL as inputs or outputs
 **                  - Configure GPIOs to be polled or interrupt driven
 **
 **
 ** Output Parameter:
 **      None.
 **
 ** Returns:
 **      None.
 **
 *****************************************************************************/
UDRV_API void UPIO_Config(tUPIO_TYPE type, tUPIO pio, tUPIO_CONFIG config, tUPIO_CBACK *cback)
{
    debug_printf("UPIO_Config: type=%d, pio=%d", type, pio); // Zephyr log
    // debug_printf("UPIO_Config: type=%d, pio=%d\n", type, pio);
    // TODO: Implement configuration if needed
}
