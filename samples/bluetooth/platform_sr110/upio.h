/*
 * Copyright (c) 2026 Synaptics Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef UPIO_H
#define UPIO_H

#include <stdint.h>

/* UPIO types */
typedef uint8_t tUPIO_TYPE;
typedef uint32_t tUPIO;
typedef uint8_t tUPIO_STATE;
typedef void tUPIO_CBACK;

#define UPIO_GENERAL  0
#define UPIO_LED      1
#define UPIO_BUTTON   2

#define UPIO_OFF  0
#define UPIO_ON   1

/* GPIO pin assignments */
#define BT_REG_ON_GPIO      0
#define HCILP_BT_WAKE_GPIO  1
#define HCILP_HOST_WAKE_GPIO 2

#define UDRV_API

/* tUPIO_CONFIG - direction/interrupt config */
typedef uint8_t tUPIO_CONFIG;
#define UPIO_OUT          0
#define UPIO_IN           1
#define UPIO_POLL         0
#define UPIO_INT          2

void UPIO_Init(void *p_cfg);
void UPIO_DeInit(void);
void UPIO_Set(tUPIO_TYPE type, tUPIO pio, tUPIO_STATE state);
tUPIO_STATE UPIO_Read(tUPIO_TYPE type, tUPIO pio);
void UPIO_Config(tUPIO_TYPE type, tUPIO pio, tUPIO_CONFIG config, tUPIO_CBACK *cback);
void UPIO_Set_CTS_Low(void);
void UPIO_Restore_CTS_Setting(void);
void UPIO_Set_Host_LPO(void);

#endif /* UPIO_H */
