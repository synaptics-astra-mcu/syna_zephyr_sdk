/**
* SPDX-License-Identifier: Apache-2.0
*
* Copyright 2025 Synaptics Incorporated
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/


/**
 * @file	dhl_lib.c
 *
 * @brief	This file implements the DHL DDR Memory initialization
 *
 * @details
 */

/*----------------------------------------------------------------------------
Include files
*---------------------------------------------------------------------------*/
#include <stddef.h>
#include "dhl_lib.h"
#include "dhl.h"
#include "ddr_params.h"
#include <zephyr/kernel.h>

/**
 * @brief DDR setting size
 */
#define DDR_SETTING_SIZE    1024
unsigned char saved_ddr_settings[DDR_SETTING_SIZE];

/**
 * @brief DDR parameter structure
 */
struct ddr_param {
    int8_t type;
    const uint8_t *param_table;
    uint32_t param_table_size;
};

#ifndef CONFIG_DDR_TYPE
#define CONFIG_DDR_TYPE 1
#endif
#define SOCREG_REGION(addr)			((addr-0xF0000000 + 0xD0000000))
#define MEMMAP_DDR_REG_BASE_ADDRESS		0xf7e40000
#define MEMMAP_MCTRLSS_REG_BASE_ADDRESS		0xf7e50000
#define MEMMAP_MC_DFI0_REG_BASE_ADDRESS		0xf7e52000
#define MEMMAP_CHIP_CTRL_REG_BASE_ADDRESS	0xf7e10000

/**
 * @brief Array of DDR parameters for different DDR types
 */
static struct ddr_param ddr_params[] = {
#if CONFIG_DDR_TYPE == 0
    {DIAG_DHL_DDR_TYPE_DDR3, NULL, 0},
#else
    {DIAG_DHL_DDR_TYPE_DDR3, NULL, 0},
#endif
#if CONFIG_DDR_TYPE == 1
    {DIAG_DHL_DDR_TYPE_DDR4, ddr_params_data, ddr_params_data_len},
#else
    {DIAG_DHL_DDR_TYPE_DDR4, NULL, 0},
#endif
#if CONFIG_DDR_TYPE == 2
    {DIAG_DHL_DDR_TYPE_LPDDR4, NULL, 0}
#else
    {DIAG_DHL_DDR_TYPE_LPDDR4, NULL, 0}
#endif
};

/**
 * @brief Put character to console
 *
 * @param c Character to be put
 * @return int Number of characters put
 */

static void ddr_putc(char c)
{
    //LOG_INFO(LOG_MOD_SYSTEM, "%c", c);
}

/**
 * @brief Initialize DDR memory
 *
 * @param config Pointer to DDR configuration structure
 * @return int32_t ddr_init_result_t on success, -1 on failure
 */
ddr_init_result_t sys_ddr_init(const ddr_config_t *config, int real)
{
    int ret;

    //LOG_INFO(LOG_MOD_SYSTEM, "Init %s @ %d\n", config->type ? (config->type == DIAG_DHL_DDR_TYPE_DDR4 ? "DDR4" : "LPDDR4") : "DDR3", CONFIG_DDR_RATE);
    if (ddr_params[config->type].param_table == NULL) {
        //LOG_ERROR(LOG_MOD_SYSTEM, "No valid params for DDR type %d\n", config->type);
        return DDR_INIT_ERROR_UNSUPPORTED;
    }

    ret = dhl_set_putc_func(ddr_putc);
    if (ret != 0) {
        //LOG_ERROR(LOG_MOD_SYSTEM, "dhl_set_putc_func failed rc %d\n", ret);
        return DDR_INIT_ERROR_CALLBACK;
    }

    ret = dhl_ddr_set_reg_bases(SOCREG_REGION(MEMMAP_DDR_REG_BASE_ADDRESS),
                                SOCREG_REGION(MEMMAP_MCTRLSS_REG_BASE_ADDRESS),
                                SOCREG_REGION(MEMMAP_MC_DFI0_REG_BASE_ADDRESS),
                                SOCREG_REGION(MEMMAP_CHIP_CTRL_REG_BASE_ADDRESS));

    if (ret != 0) {
        //LOG_ERROR(LOG_MOD_SYSTEM, "dhl_ddr_set_reg_bases failed rc %d\n", ret);
        return DDR_INIT_ERROR_REG_BASE;
    }

    ret = dhl_ddr_init(config->type,
                    ddr_params[config->type].param_table, ddr_params[config->type].param_table_size,
                    0, saved_ddr_settings, sizeof(saved_ddr_settings), 0);
    if (ret != 0) {
        //LOG_ERROR(LOG_MOD_SYSTEM, "dhl_ddr_init failed rc %d\n", ret);
        return DDR_INIT_ERROR_DHL_INIT;
    }
    return DDR_INIT_SUCCESS;
}
