/*----------------------------------------------------------------------------
 * (C) Copyright 2025 Synaptics Incorporated. All rights reserved.
 *
 * This program is the proprietary software of Synaptics and/or its licensors,
 * and only be used, duplicated, modified or distributed under the authorized
 * license from Synaptics.
 * ANY USE OF THE SOFTWARE OTHER THAN AS AUTHORIZED UNDER THIS LICENSE OR
 * COPYRIGHT LAW IS PROHIBITED.
 *---------------------------------------------------------------------------*/

/**
 * @file	ddr_params.h
 *
 * @brief	DDR Parameters
 *
 * @details	This header provides the DDR parameters for DDR memory initialization
 *		and management using the DHL library.
 */

#ifndef _DDR_PARAMS_H_
#define _DDR_PARAMS_H_

#ifdef __cplusplus
extern "C" {
#endif

#define CONFIG_BOARD_SL2610_RDK	1
#define CONFIG_DDR4_3200_2X8	1


#if CONFIG_BOARD_SL2610_FPGA
#include "FPGA/init_ddr4_3200_1x16_fpga.h"
#elif CONFIG_BOARD_SL2610_PEK
#if CONFIG_DDR4_3200_2X8
#include "PEK/init_ddr4_3200_2x8_pek.h"
#elif CONFIG_DDR4_1600_2X8
#include "PEK/init_ddr4_1600_2x8_pek.h"
#endif
#elif CONFIG_BOARD_SL2610_RDK
#if CONFIG_DDR4_3200_2X8
#include "RDK/init_ddr4_3200_2x8_rdk.h"
#elif CONFIG_DDR4_1600_2X8
#include "RDK/init_ddr4_1600_2x8_rdk.h"
#else
#error "Unsupported DDR type"
#endif // CONFIG_DDR4_1600_2X8 || CONFIG_DDR4_3200_2X8
#else
#error "Unsupported board"
#endif // CONFIG_BOARD_SL2610_FPGA

#ifdef __cplusplus
}
#endif

#endif /* _DDR_PARAMS_H_ */
